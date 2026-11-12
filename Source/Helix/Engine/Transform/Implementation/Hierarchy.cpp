#include <Helix/Engine/Object/Implementation/ObjectAccess.h>
#include <Helix/Engine/Transform/Implementation/TransformAccess.h>

#include <Helix/Core/Memory/PoolAllocator.h>
#include <Helix/Jobs/Jobs.h>
#include <Helix/Math/Simd.h>

#include <cstring>

// Under AddressSanitizer every block gets its own allocation, so a stale node pointer is reported.
#if defined(__SANITIZE_ADDRESS__)
#define HX_TRANSFORM_POOLS 0
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HX_TRANSFORM_POOLS 0
#endif
#endif
#ifndef HX_TRANSFORM_POOLS
#define HX_TRANSFORM_POOLS 1
#endif

namespace helix::engine
{
    namespace
    {
        // The arrays of a block in order; each starts on 16 bytes.
        constexpr u32 kColumns = 10;
        constexpr usize kColumnSize[kColumns] = {sizeof(Matrix4x4), sizeof(Quaternion), sizeof(LocalTRS), sizeof(Transform*),
                                                 sizeof(i32),       sizeof(u32),        sizeof(u32),      sizeof(u32),
                                                 sizeof(u32),       sizeof(u8)};
        constexpr usize kBlockAlignment = 64;

        constexpr usize Align16(usize v) { return (v + 15) & ~usize(15); }

        constexpr usize BlockSize(u32 capacity)
        {
            usize total = 0;
            for (usize s : kColumnSize)
                total += Align16(s * capacity);
            return total;
        }

        struct Columns
        {
            char* p[kColumns];
        };

        Columns ColumnsOf(const TransformHierarchy& h)
        {
            return {{reinterpret_cast<char*>(h.world), reinterpret_cast<char*>(h.worldRotation), reinterpret_cast<char*>(h.local),
                     reinterpret_cast<char*>(h.transforms), reinterpret_cast<char*>(h.parent), reinterpret_cast<char*>(h.size),
                     reinterpret_cast<char*>(h.children), reinterpret_cast<char*>(h.interest), reinterpret_cast<char*>(h.changed),
                     reinterpret_cast<char*>(h.flags)}};
        }

        void Bind(TransformHierarchy& h, void* block, u32 capacity)
        {
            char* at = static_cast<char*>(block);
            char* p[kColumns];
            for (u32 k = 0; k < kColumns; k++)
            {
                p[k] = at;
                at += Align16(kColumnSize[k] * capacity);
            }
            h.world = reinterpret_cast<Matrix4x4*>(p[0]);
            h.worldRotation = reinterpret_cast<Quaternion*>(p[1]);
            h.local = reinterpret_cast<LocalTRS*>(p[2]);
            h.transforms = reinterpret_cast<Transform**>(p[3]);
            h.parent = reinterpret_cast<i32*>(p[4]);
            h.size = reinterpret_cast<u32*>(p[5]);
            h.children = reinterpret_cast<u32*>(p[6]);
            h.interest = reinterpret_cast<u32*>(p[7]);
            h.changed = reinterpret_cast<u32*>(p[8]);
            h.flags = reinterpret_cast<u8*>(p[9]);
            h.block = block;
            h.capacity = capacity;
        }

        // A header and, behind it, the block of a one node hierarchy: one allocation for the common case.
        constexpr usize kHeaderSpace = (sizeof(TransformHierarchy) + kBlockAlignment - 1) & ~(kBlockAlignment - 1);
        constexpr usize kSlotSize = kHeaderSpace + BlockSize(1);

        struct State
        {
#if HX_TRANSFORM_POOLS
            // Most hierarchies are one GameObject: a header with room for a one node block, from a pool.
            PoolAllocator slots{kSlotSize, kBlockAlignment, 256};
#endif
            // The roots per scene slot.
            struct Roots
            {
                TransformHierarchy* first = nullptr;
                TransformHierarchy* last = nullptr;
                u32 count = 0;
            };
            Array<Roots> roots;
            // Slot 1 is the scene the SceneManager starts with.
            u32 newRootScene = 1;
            Array<TransformHierarchy*> worldList;
            Array<TransformHierarchy*> changeList;
            Array<TransformHierarchy*> work;
            Array<String> systems;
            u32 activating = 0;
        };

        // A plain global: no guard on every use (nothing makes transforms before main).
        State gState;
        State& S() { return gState; }

        void* EmbeddedBlock(TransformHierarchy& h) { return reinterpret_cast<char*>(&h) + kHeaderSpace; }

        // A one node block lives behind the header; larger ones on the heap.
        void* AllocateBlock(TransformHierarchy& h, u32 capacity)
        {
            return capacity == 1 ? EmbeddedBlock(h) : HeapAllocator().Allocate(BlockSize(capacity), kBlockAlignment);
        }

        void FreeBlock(TransformHierarchy& h, void* block, u32 capacity)
        {
            if (block != EmbeddedBlock(h))
                HeapAllocator().Free(block, BlockSize(capacity), kBlockAlignment);
        }

        TransformHierarchy* NewHierarchy(u32 capacity)
        {
#if HX_TRANSFORM_POOLS
            void* memory = S().slots.Allocate(kSlotSize, kBlockAlignment);
#else
            void* memory = HeapAllocator().Allocate(kSlotSize, kBlockAlignment);
#endif
            TransformHierarchy* h = new (memory) TransformHierarchy();
            Bind(*h, AllocateBlock(*h, capacity), capacity);
            return h;
        }

        // ------------------------------------------------------------------------------------------------ lists

        void AddTo(Array<TransformHierarchy*>& list, TransformHierarchy& h, u32 TransformHierarchy::* slot)
        {
            if (h.*slot != ~0u)
                return;
            h.*slot = u32(list.Size());
            list.Push(&h);
        }

        void RemoveFrom(Array<TransformHierarchy*>& list, TransformHierarchy& h, u32 TransformHierarchy::* slot)
        {
            u32 s = h.*slot;
            if (s == ~0u)
                return;
            TransformHierarchy* last = list.Back();
            list[s] = last;
            last->*slot = s;
            list.Pop();
            h.*slot = ~0u;
        }

        void NoteState(TransformHierarchy& h, bool dirty, u32 changed)
        {
            if (dirty)
            {
                h.worldDirty = true;
                AddTo(S().worldList, h, &TransformHierarchy::worldSlot);
            }
            if (changed)
            {
                h.changedSystems |= changed;
                AddTo(S().changeList, h, &TransformHierarchy::changeSlot);
            }
        }

        State::Roots& RootsOf(u32 scene)
        {
            Array<State::Roots>& roots = S().roots;
            if (scene >= roots.Size())
                roots.Resize(scene + 1);
            return roots[scene];
        }

        // Before `before` (a root of the same scene; null: last).
        void LinkRoot(TransformHierarchy& h, TransformHierarchy* before)
        {
            State::Roots& s = RootsOf(h.scene);
            h.linked = true;
            s.count++;
            h.nextRoot = before;
            h.previousRoot = before ? before->previousRoot : s.last;
            if (h.previousRoot)
                h.previousRoot->nextRoot = &h;
            else
                s.first = &h;
            if (before)
                before->previousRoot = &h;
            else
                s.last = &h;
        }

        void UnlinkRoot(TransformHierarchy& h)
        {
            if (!h.linked)
                return;
            State::Roots& s = RootsOf(h.scene);
            (h.previousRoot ? h.previousRoot->nextRoot : s.first) = h.nextRoot;
            (h.nextRoot ? h.nextRoot->previousRoot : s.last) = h.previousRoot;
            h.previousRoot = h.nextRoot = nullptr;
            h.linked = false;
            s.count--;
        }

        void FreeHierarchy(TransformHierarchy* h)
        {
            State& s = S();
            UnlinkRoot(*h);
            RemoveFrom(s.worldList, *h, &TransformHierarchy::worldSlot);
            RemoveFrom(s.changeList, *h, &TransformHierarchy::changeSlot);
            FreeBlock(*h, h->block, h->capacity);
            h->~TransformHierarchy();
#if HX_TRANSFORM_POOLS
            s.slots.Free(h, kSlotSize, kBlockAlignment);
#else
            HeapAllocator().Free(h, kSlotSize, kBlockAlignment);
#endif
        }

        // ------------------------------------------------------------------------------------------------ nodes

        void MoveNodes(TransformHierarchy& h, u32 from, u32 to, u32 n)
        {
            if (!n || from == to)
                return;
            Columns c = ColumnsOf(h);
            for (u32 k = 0; k < kColumns; k++)
                std::memmove(c.p[k] + to * kColumnSize[k], c.p[k] + from * kColumnSize[k], n * kColumnSize[k]);
        }

        void CopyNodes(TransformHierarchy& d, u32 to, const TransformHierarchy& s, u32 from, u32 n)
        {
            Columns cd = ColumnsOf(d), cs = ColumnsOf(s);
            for (u32 k = 0; k < kColumns; k++)
                std::memcpy(cd.p[k] + to * kColumnSize[k], cs.p[k] + from * kColumnSize[k], n * kColumnSize[k]);
        }

        // The Transform objects of nodes [from, to) learn their block and index.
        void Reindex(TransformHierarchy& h, u32 from, u32 to)
        {
            for (u32 j = from; j < to; j++)
                if (Transform* t = h.transforms[j])
                {
                    TransformAccess::Block(*t) = &h;
                    TransformAccess::Index(*t) = j;
                }
        }

        void Reallocate(TransformHierarchy& h, u32 capacity)
        {
            if (capacity == h.capacity)
                return;
            void* old = h.block;
            u32 oldCapacity = h.capacity;
            TransformHierarchy copy = h;
            Bind(h, AllocateBlock(h, capacity), capacity);
            CopyNodes(h, 0, copy, 0, h.count);
            FreeBlock(h, old, oldCapacity);
        }

        // Removes node i and its subtree, fixing parents, sizes, child counts and the objects' indices.
        void RemoveRange(TransformHierarchy& h, u32 i)
        {
            u32 n = h.size[i];
            i32 p = h.parent[i];
            if (p >= 0 && !(h.flags[i] & TransformFlags::Dead))
                h.children[p]--;
            for (i32 a = p; a >= 0; a = h.parent[a])
                h.size[a] -= n;
            MoveNodes(h, i + n, i, h.count - (i + n));
            h.count -= n;
            for (u32 j = i; j < h.count; j++)
                if (h.parent[j] > i32(i))
                    h.parent[j] -= i32(n);
            Reindex(h, i, h.count);
        }

        // Node i's subtree as a hierarchy of its own, not linked as a root (h itself when i is its root).
        TransformHierarchy* Cut(TransformHierarchy& h, u32 i)
        {
            if (i == 0)
            {
                UnlinkRoot(h);
                return &h;
            }
            u32 n = h.size[i];
            TransformHierarchy* cut = NewHierarchy(n);
            cut->scene = h.scene;
            CopyNodes(*cut, 0, h, i, n);
            cut->count = n;
            cut->parent[0] = -1;
            bool dirty = false;
            u32 changed = 0;
            for (u32 j = 0; j < n; j++)
            {
                if (j)
                    cut->parent[j] -= i32(i);
                dirty |= (cut->flags[j] & TransformFlags::WorldDirty) != 0;
                changed |= cut->changed[j];
            }
            NoteState(*cut, dirty, changed);
            Reindex(*cut, 0, n);
            RemoveRange(h, i);
            return cut;
        }

        // All of `src` under node p of h as its live child number k (past the end: the last); returns its first node.
        u32 InsertUnder(TransformHierarchy& h, u32 p, u32 k, const TransformHierarchy& src)
        {
            u32 n = src.count;
            u32 end = p + h.size[p];
            u32 pos = p + 1;
            // As the last child (the common case) without walking the siblings.
            if (k >= h.children[p])
                pos = end;
            else
                for (; pos < end; pos += h.size[pos])
                    if (!(h.flags[pos] & TransformFlags::Dead) && k-- == 0)
                        break;
            if (h.count + n > h.capacity)
                Reallocate(h, Max(h.count + n, h.capacity * 2));
            MoveNodes(h, pos, pos + n, h.count - pos);
            for (u32 j = pos + n; j < h.count + n; j++)
                if (h.parent[j] >= i32(pos))
                    h.parent[j] += i32(n);
            CopyNodes(h, pos, src, 0, n);
            bool dirty = false;
            for (u32 j = 0; j < n; j++)
            {
                h.parent[pos + j] = j ? h.parent[pos + j] + i32(pos) : i32(p);
                dirty |= (h.flags[pos + j] & TransformFlags::WorldDirty) != 0;
            }
            h.count += n;
            for (i32 a = i32(p); a >= 0; a = h.parent[a])
                h.size[a] += n;
            h.children[p]++;
            Reindex(h, pos, h.count);
            NoteState(h, dirty, src.changedSystems);
            return pos;
        }

        TransformHierarchy* RootAt(u32 scene, u32 index)
        {
            State::Roots& roots = RootsOf(scene);
            if (index >= roots.count)
                return nullptr;
            TransformHierarchy* r = roots.first;
            while (r && index--)
                r = r->nextRoot;
            return r;
        }

        // world = parent world * TRS(local), both affine: the rotation and scale columns from the quaternion, then
        // three multiply-adds per column on the parent's columns.
        HX_FORCE_INLINE void ComputeWorld(const LocalTRS& l, const Matrix4x4* parent, const Quaternion* parentRotation,
                                          Matrix4x4& world, Quaternion& rotation)
        {
            const Quaternion& q = l.rotation;
            f32 x2 = q.x * 2, y2 = q.y * 2, z2 = q.z * 2;
            f32 xx = q.x * x2, yy = q.y * y2, zz = q.z * z2, xy = q.x * y2, xz = q.x * z2, yz = q.y * z2;
            f32 wx = q.w * x2, wy = q.w * y2, wz = q.w * z2;
            const Vector3& s = l.scale;
            const Vector3& t = l.position;
            f32* out = world.Data();
            if (!parent)
            {
                simd::Store(out + 0, simd::Set((1 - (yy + zz)) * s.x, (xy + wz) * s.x, (xz - wy) * s.x, 0));
                simd::Store(out + 4, simd::Set((xy - wz) * s.y, (1 - (xx + zz)) * s.y, (yz + wx) * s.y, 0));
                simd::Store(out + 8, simd::Set((xz + wy) * s.z, (yz - wx) * s.z, (1 - (xx + yy)) * s.z, 0));
                simd::Store(out + 12, simd::Set(t.x, t.y, t.z, 1));
                rotation = q;
                return;
            }
            const f32* pm = parent->Data();
            simd::float4 p0 = simd::Load(pm), p1 = simd::Load(pm + 4), p2 = simd::Load(pm + 8), p3 = simd::Load(pm + 12);
            auto column = [&](f32 a, f32 b, f32 c) {
                return simd::MulAdd(p2, simd::Splat(c), simd::MulAdd(p1, simd::Splat(b), simd::Mul(p0, simd::Splat(a))));
            };
            simd::Store(out + 0, column((1 - (yy + zz)) * s.x, (xy + wz) * s.x, (xz - wy) * s.x));
            simd::Store(out + 4, column((xy - wz) * s.y, (1 - (xx + zz)) * s.y, (yz + wx) * s.y));
            simd::Store(out + 8, column((xz + wy) * s.z, (yz - wx) * s.z, (1 - (xx + yy)) * s.z));
            simd::Store(out + 12, simd::Add(column(t.x, t.y, t.z), p3));
            rotation = *parentRotation * q;
        }

        void ComputeNode(TransformHierarchy& h, u32 i)
        {
            i32 p = h.parent[i];
            ComputeWorld(h.local[i], p < 0 ? nullptr : &h.world[p], p < 0 ? nullptr : &h.worldRotation[p], h.world[i], h.worldRotation[i]);
            h.flags[i] &= u8(~TransformFlags::WorldDirty);
        }

        void VisitChanges(TransformHierarchy& h, TransformVisitor visit, void* user, u32 bit)
        {
            UpdateWorldMatrices(h);
            u32 remaining = 0;
            for (u32 i = 0; i < h.count; i++)
            {
                u32 c = h.changed[i];
                if (c & bit)
                {
                    if (!(h.flags[i] & TransformFlags::Dead))
                        visit(h, i, user);
                    c &= ~bit;
                    h.changed[i] = c;
                }
                remaining |= c;
            }
            h.changedSystems = remaining;
        }
    }

    // ---------------------------------------------------------------------------------------------- TransformAccess

    void TransformAccess::CreateRoot(Transform* t)
    {
        TransformHierarchy* h = NewHierarchy(1);
        h->count = 1;
        // Identity local values: the world values are known, the node starts clean.
        h->world[0] = Matrix4x4::identity;
        h->worldRotation[0] = Quaternion::identity;
        h->local[0] = LocalTRS();
        h->transforms[0] = t;
        h->parent[0] = -1;
        h->size[0] = 1;
        h->children[0] = 0;
        h->interest[0] = 0;
        h->changed[0] = 0;
        h->flags[0] = TransformFlags::HasChanged;
        t->hierarchy_ = h;
        t->index_ = 0;
        h->scene = S().newRootScene;
        LinkRoot(*h, nullptr);
    }

    void TransformAccess::Release(Transform* t)
    {
        TransformHierarchy* hp = t->hierarchy_;
        if (!hp)
            return;
        TransformHierarchy& h = *hp;
        u32 i = t->index_;
        i32 p = h.parent[i];
        if (p >= 0)
        {
            Transform* pt = h.transforms[p];
            GameObject* pg = pt ? pt->gameObject() : nullptr;
            if (pg && ObjectAccess::Destroying(pg))
            {
                h.flags[i] |= TransformFlags::Dead;
                h.transforms[i] = nullptr;
                h.children[p]--;
                t->hierarchy_ = nullptr;
                return;
            }
        }
        // Live children would be left without their parent's node: they become roots.
        for (;;)
        {
            u32 child = ~0u;
            for (u32 j = i + 1, end = i + h.size[i]; j < end && child == ~0u; j += h.size[j])
                if (!(h.flags[j] & TransformFlags::Dead))
                    child = j;
            if (child == ~0u)
                break;
            Place(h.transforms[child], nullptr, ~0u);
            MarkChanged(*h.transforms[child]->hierarchy_, 0);
        }
        h.transforms[i] = nullptr;
        t->hierarchy_ = nullptr;
        if (i == 0)
            FreeHierarchy(&h);
        else
            RemoveRange(h, i);
    }

    void TransformAccess::Place(Transform* t, Transform* parent, u32 sibling)
    {
        TransformHierarchy* cut = Cut(*t->hierarchy_, t->index_);
        if (!parent)
        {
            LinkRoot(*cut, RootAt(cut->scene, sibling));
            return;
        }
        InsertUnder(*parent->hierarchy_, parent->index_, sibling, *cut);
        FreeHierarchy(cut);
    }

    Vector3 TransformAccess::ScaleOf(const Matrix4x4& m, Quaternion rotation)
    {
        // The diagonal of inverse(rotation) * m (upper 3x3).
        Matrix4x4 r = Matrix4x4::Rotate(Quaternion::Inverse(rotation));
        Vector3 s;
        for (int c = 0; c < 3; c++)
            s[c] = r(c, 0) * m(0, c) + r(c, 1) * m(1, c) + r(c, 2) * m(2, c);
        return s;
    }

    void TransformAccess::SetWorld(Transform* t, const Matrix4x4& world, Quaternion rotation)
    {
        TransformHierarchy& h = *t->hierarchy_;
        u32 i = t->index_;
        i32 p = h.parent[i];
        Matrix4x4 local = world;
        Quaternion localRotation = rotation;
        if (p >= 0)
        {
            Transform* pt = h.transforms[p];
            local = pt->localToWorldMatrix().inverse() * world;
            localRotation = Quaternion::Inverse(pt->rotation()) * rotation;
        }
        LocalTRS& l = h.local[i];
        l.position = local.GetPosition();
        l.rotation = localRotation;
        l.scale = ScaleOf(local, localRotation);
        MarkChanged(h, i);
    }

    void TransformAccess::MarkChanged(TransformHierarchy& h, u32 i)
    {
        // Local copies: writes through the u8 flags could alias the block's pointers otherwise.
        u8* __restrict flags = h.flags;
        u32* __restrict changedMask = h.changed;
        const u32* __restrict interest = h.interest;
        u32 changed = 0;
        for (u32 j = i, end = i + h.size[i]; j < end; j++)
        {
            flags[j] |= TransformFlags::WorldDirty | TransformFlags::HasChanged;
            u32 c = changedMask[j] | interest[j];
            changedMask[j] = c;
            changed |= c;
        }
        if (!h.worldDirty || h.worldSlot == ~0u)
            NoteState(h, true, 0);
        if (changed)
            NoteState(h, false, changed);
    }

    void TransformAccess::BuildClone(Span<Transform* const> originals, Span<Transform* const> clones, u32 scene)
    {
        u32 n = u32(originals.Size());
        if (!n)
            return;
        const TransformHierarchy& o = *originals[0]->hierarchy_;
        u32 base = originals[0]->index_;
        // Original node (relative to the copied root) to clone node.
        Array<u32> map;
        map.Resize(o.size[base], ~0u);
        TransformHierarchy* h = NewHierarchy(n);
        h->count = n;
        for (u32 k = 0; k < n; k++)
        {
            u32 oi = originals[k]->index_;
            map[oi - base] = k;
            h->world[k] = Matrix4x4::identity;
            h->worldRotation[k] = Quaternion::identity;
            h->local[k] = o.local[oi];
            h->transforms[k] = clones[k];
            h->parent[k] = k ? i32(map[u32(o.parent[oi]) - base]) : -1;
            h->size[k] = 1;
            h->children[k] = 0;
            h->interest[k] = 0;
            h->changed[k] = 0;
            h->flags[k] = TransformFlags::WorldDirty | TransformFlags::HasChanged;
        }
        // Children come after their parents: sizes add up backwards.
        for (u32 k = n; k-- > 1;)
        {
            h->size[h->parent[k]] += h->size[k];
            h->children[h->parent[k]]++;
        }
        Reindex(*h, 0, n);
        NoteState(*h, true, 0);
        h->scene = scene;
        LinkRoot(*h, nullptr);
    }

    void TransformAccess::BuildLoaded(Span<Transform* const> nodes, Span<const i32> parents, u32 scene)
    {
        u32 n = u32(nodes.Size());
        if (!n)
            return;
        TransformHierarchy* h = NewHierarchy(n);
        h->count = n;
        u32 changed = 0;
        for (u32 k = 0; k < n; k++)
        {
            Transform* t = nodes[k];
            TransformHierarchy* own = t->hierarchy_;
            h->world[k] = Matrix4x4::identity;
            h->worldRotation[k] = Quaternion::identity;
            h->local[k] = own->local[t->index_];
            h->transforms[k] = t;
            h->parent[k] = k ? parents[k] : -1;
            h->size[k] = 1;
            h->children[k] = 0;
            h->interest[k] = own->interest[t->index_];
            // Each node's world matrix changes now that it hangs under its parent: every system that follows it hears
            // of that (a renderer enabled before the hierarchy was built would keep its local matrix otherwise).
            h->changed[k] = h->interest[k] | own->changed[t->index_];
            changed |= h->changed[k];
            h->flags[k] = TransformFlags::WorldDirty | TransformFlags::HasChanged;
            // Each was a root of its own since it was made.
            FreeHierarchy(own);
        }
        for (u32 k = n; k-- > 1;)
        {
            h->size[h->parent[k]] += h->size[k];
            h->children[h->parent[k]]++;
        }
        Reindex(*h, 0, n);
        NoteState(*h, true, changed);
        h->scene = scene;
        LinkRoot(*h, nullptr);
    }

    u32& TransformAccess::NewRootScene() { return S().newRootScene; }

    void TransformAccess::MoveToScene(TransformHierarchy& h, u32 scene)
    {
        UnlinkRoot(h);
        h.scene = scene;
        LinkRoot(h, nullptr);
    }

    void TransformAccess::Resize(TransformHierarchy& h, u32 capacity) { Reallocate(h, Max(capacity, Max(h.count, 1u))); }

    u32& TransformAccess::Activating() { return S().activating; }

    void Transform::RefreshWorld() const
    {
        TransformHierarchy& h = *hierarchy_;
        // Up to the first clean ancestor, then down.
        u32 chain[64];
        u32 n = 0;
        for (i32 j = i32(index_); j >= 0 && (h.flags[j] & TransformFlags::WorldDirty); j = h.parent[j])
        {
            if (n == 64)
            {
                UpdateWorldMatrices(h);
                return;
            }
            chain[n++] = u32(j);
        }
        while (n)
            engine::ComputeNode(h, chain[--n]);
    }

    // ---------------------------------------------------------------------------------------------- public

    void UpdateWorldMatrices(TransformHierarchy& h)
    {
        if (!h.worldDirty)
            return;
        u8* __restrict flags = h.flags;
        const LocalTRS* __restrict local = h.local;
        const i32* __restrict parent = h.parent;
        Matrix4x4* __restrict world = h.world;
        Quaternion* __restrict rotation = h.worldRotation;
        for (u32 i = 0, n = h.count; i < n; i++)
            if (flags[i] & TransformFlags::WorldDirty)
            {
                i32 p = parent[i];
                ComputeWorld(local[i], p < 0 ? nullptr : &world[p], p < 0 ? nullptr : &rotation[p], world[i], rotation[i]);
                flags[i] &= u8(~TransformFlags::WorldDirty);
            }
        h.worldDirty = false;
    }

    void UpdateWorldMatrices()
    {
        Array<TransformHierarchy*>& list = S().worldList;
        u32 n = u32(list.Size());
        if (n >= 32 && jobs::IsInitialized())
        {
            struct Job
            {
                TransformHierarchy* const* hierarchies;
                void operator()(u32 i) const { UpdateWorldMatrices(*hierarchies[i]); }
            };
            jobs::Complete(jobs::ParallelFor(n, 8, Job{list.Data()}));
        }
        else
            for (TransformHierarchy* h : list)
                UpdateWorldMatrices(*h);
        for (TransformHierarchy* h : list)
            h->worldSlot = ~0u;
        list.Clear();
    }

    u32 RegisterTransformSystem(StringView name)
    {
        Array<String>& systems = S().systems;
        for (u32 k = 0; k < systems.Size(); k++)
            if (systems[k] == name)
                return k;
        if (systems.Size() == kMaxTransformSystems)
            return kNoTransformSystem;
        systems.Push(String(name));
        return u32(systems.Size() - 1);
    }

    void SetTransformInterest(Transform* t, u32 system, bool interested)
    {
        if (!t || system >= kMaxTransformSystems)
            return;
        TransformHierarchy& h = t->Hierarchy();
        u32 bit = 1u << system;
        if (interested)
            h.interest[t->NodeIndex()] |= bit;
        else
        {
            h.interest[t->NodeIndex()] &= ~bit;
            h.changed[t->NodeIndex()] &= ~bit;
        }
    }

    bool TransformInterest(const Transform* t, u32 system)
    {
        return t && system < kMaxTransformSystems && (t->Hierarchy().interest[t->NodeIndex()] & (1u << system));
    }

    void DispatchTransformChanges(u32 system, TransformVisitor visit, void* user, bool parallel)
    {
        if (system >= kMaxTransformSystems)
            return;
        State& s = S();
        u32 bit = 1u << system;
        s.work.Clear();
        for (TransformHierarchy* h : s.changeList)
            if (h->changedSystems & bit)
                s.work.Push(h);
        u32 n = u32(s.work.Size());
        if (parallel && n >= 4 && jobs::IsInitialized())
        {
            struct Job
            {
                TransformHierarchy* const* hierarchies;
                TransformVisitor visit;
                void* user;
                u32 bit;
                void operator()(u32 i) const { VisitChanges(*hierarchies[i], visit, user, bit); }
            };
            jobs::Complete(jobs::ParallelFor(n, 4, Job{s.work.Data(), visit, user, bit}));
        }
        else
            for (TransformHierarchy* h : s.work)
                VisitChanges(*h, visit, user, bit);
        // Hierarchies without pending changes leave the list.
        for (u32 k = 0; k < s.changeList.Size();)
        {
            TransformHierarchy& h = *s.changeList[k];
            if (h.changedSystems)
                k++;
            else
                RemoveFrom(s.changeList, h, &TransformHierarchy::changeSlot);
        }
    }

    TransformHierarchy* FirstRootHierarchy(u32 scene) { return RootsOf(scene).first; }
    u32 RootHierarchyCount(u32 scene) { return RootsOf(scene).count; }
}
