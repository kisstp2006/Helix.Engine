#include <Helix/Engine/Jobs/ManagedJobs.h>

#include <Helix/Core/Containers/HashMap.h>
#include <Helix/Core/Logging/Log.h>
#include <Helix/Core/Memory/Allocator.h>
#include <Helix/Engine/Transform/Hierarchy.h>
#include <Helix/Engine/Transform/Implementation/TransformAccess.h>
#include <Helix/Jobs/Jobs.h>
#include <Helix/Platform/Threading/Thread.h>

#include <algorithm>
#include <cstring>
#include <mutex>

namespace helix::engine
{
    namespace
    {
        LogChannel JobsLog{"Engine"};

        u64 Pack(jobs::JobHandle h) { return h.IsValid() ? (u64(h.generation) << 32) | h.index : 0; }
        jobs::JobHandle Unpack(u64 v) { return jobs::JobHandle{u32(v), u32(v >> 32)}; }

        void EnsureJobs()
        {
            if (!jobs::IsInitialized())
                jobs::Initialize();
        }

        // Heap memory with its size in front (Helix.Core's allocators take the size back).
        struct Front
        {
            u64 total;
            u32 offset;
            u32 alignment;
        };

        void* HeapAlloc(usize size, usize alignment)
        {
            alignment = alignment < 16 ? 16 : alignment;
            usize total = size + alignment + sizeof(Front);
            auto* raw = static_cast<u8*>(HeapAllocator().Allocate(total, 16));
            if (!raw)
                return nullptr;
            uptr at = (uptr(raw) + sizeof(Front) + alignment - 1) & ~uptr(alignment - 1);
            auto* front = reinterpret_cast<Front*>(at - sizeof(Front));
            front->total = total;
            front->offset = u32(at - uptr(raw));
            front->alignment = u32(alignment);
            return reinterpret_cast<void*>(at);
        }

        void HeapFree(void* p)
        {
            if (!p)
                return;
            auto* front = reinterpret_cast<Front*>(uptr(p) - sizeof(Front));
            HeapAllocator().Free(static_cast<u8*>(p) - front->offset, usize(front->total), 16);
        }

        // Temp: blocks of an arena rewound each frame; large ones from the heap, freed at the end of the frame.
        constexpr usize kTempBlock = 1u << 20;
        constexpr usize kTempLarge = 1u << 18;
        constexpr u32 kTempJobFrames = 4;

        struct Memory
        {
            std::mutex lock;
            Array<u8*> blocks;
            usize block = 0;
            usize used = 0;
            Array<void*> large;
            u32 frame = 0;
            // TempJob allocations by address: the frame they were made in, whether they were reported.
            struct Age
            {
                u32 frame;
                bool reported;
            };
            HashMap<u64, Age> tempJob;
            i32 live[8] = {};
        };

        Memory& Mem()
        {
            static Memory m;
            return m;
        }

        void* TempAlloc(usize size, usize alignment)
        {
            Memory& m = Mem();
            if (size > kTempLarge)
            {
                void* p = HeapAlloc(size, alignment);
                m.large.Push(p);
                return p;
            }
            alignment = alignment < 16 ? 16 : alignment;
            for (;;)
            {
                if (m.block < m.blocks.Size())
                {
                    uptr base = uptr(m.blocks[m.block]);
                    uptr at = (base + m.used + alignment - 1) & ~uptr(alignment - 1);
                    if (at + size <= base + kTempBlock)
                    {
                        m.used = at + size - base;
                        return reinterpret_cast<void*>(at);
                    }
                    m.block++;
                    m.used = 0;
                    continue;
                }
                m.blocks.Push(static_cast<u8*>(HeapAlloc(kTempBlock, 64)));
            }
        }

        // ------------------------------------------------------------------------------------------- jobs

        struct TransformJob
        {
            jobs::JobFunction entry;
            void* block;
            TransformJobItem* items;
            u32* groupStarts;
        };

        void RunTransformGroups(void* data, u32 begin, u32 end)
        {
            auto* job = static_cast<TransformJob*>(data);
            for (u32 g = begin; g < end; g++)
                for (u32 i = job->groupStarts[g]; i < job->groupStarts[g + 1]; i++)
                    job->entry(job->block, i, i + 1);
        }

        struct AccessArray
        {
            Array<EntityId> transforms;
            i32 capacity = 0;
        };

        AccessArray* ArrayOf(u64 a) { return reinterpret_cast<AccessArray*>(uptr(a)); }

        // The world matrix and rotation of a node from the local values up its parents (no shared state touched).
        void WorldOf(const TransformHierarchy& h, u32 node, Matrix4x4& matrix, Quaternion& rotation)
        {
            matrix = Matrix4x4::identity;
            rotation = Quaternion::identity;
            u32 chain[64];
            u32 n = 0;
            for (i32 i = i32(node); i >= 0 && n < 64; i = h.parent[i])
                chain[n++] = u32(i);
            for (u32 k = n; k-- > 0;)
            {
                const LocalTRS& l = h.local[chain[k]];
                matrix = matrix * Matrix4x4::TRS(l.position, l.rotation, l.scale);
                rotation = rotation * l.rotation;
            }
        }

        TransformHierarchy* H(u64 h) { return reinterpret_cast<TransformHierarchy*>(uptr(h)); }
    }

    // ---------------------------------------------------------------------------------------------- jobs

    u64 JobsNative::Schedule(u64 entry, u64 block, i32 size, i32 count, i32 batch, bool parallel, u64 dependency)
    {
        EnsureJobs();
        void* copy = HeapAlloc(usize(size), 16);
        std::memcpy(copy, reinterpret_cast<const void*>(uptr(block)), usize(size));
        auto function = reinterpret_cast<jobs::JobFunction>(uptr(entry));
        jobs::JobHandle h = parallel ? jobs::ScheduleParallelFor(function, copy, u32(count), u32(batch > 0 ? batch : 1), Unpack(dependency))
                                     : jobs::Schedule(function, copy, Unpack(dependency));
        if (parallel && !h.IsValid())
        {
            HeapFree(copy);
            return dependency;
        }
        return Pack(jobs::Schedule([copy] { HeapFree(copy); }, h));
    }

    u64 JobsNative::ScheduleTransforms(u64 entry, u64 block, i32 size, i32 itemsOffset, u64 transforms, u64 dependency)
    {
        EnsureJobs();
        AccessArray* a = ArrayOf(transforms);
        u32 n = a ? u32(a->transforms.Size()) : 0;
        // Items grouped by root hierarchy (one worker per root); invalid transforms run alone.
        auto* items = static_cast<TransformJobItem*>(HeapAlloc(sizeof(TransformJobItem) * (n ? n : 1), 16));
        for (u32 i = 0; i < n; i++)
        {
            Transform* t = EntityIdToObject<Transform>(a->transforms[i]);
            items[i] = t ? TransformJobItem{&t->Hierarchy(), t->NodeIndex(), i32(i)} : TransformJobItem{nullptr, 0, i32(i)};
        }
        std::stable_sort(items, items + n, [](const TransformJobItem& x, const TransformJobItem& y) { return uptr(x.hierarchy) < uptr(y.hierarchy); });
        auto* starts = static_cast<u32*>(HeapAlloc(sizeof(u32) * (n + 1), 16));
        u32 groups = 0;
        for (u32 i = 0; i < n; i++)
            if (i == 0 || items[i].hierarchy != items[i - 1].hierarchy || !items[i].hierarchy)
                starts[groups++] = i;
        starts[groups] = n;
        void* copy = HeapAlloc(usize(size), 16);
        std::memcpy(copy, reinterpret_cast<const void*>(uptr(block)), usize(size));
        u64 itemsAddress = u64(uptr(items));
        std::memcpy(static_cast<u8*>(copy) + itemsOffset, &itemsAddress, sizeof(itemsAddress));
        auto* job = static_cast<TransformJob*>(HeapAlloc(sizeof(TransformJob), 16));
        *job = {reinterpret_cast<jobs::JobFunction>(uptr(entry)), copy, items, starts};
        jobs::JobHandle h = groups ? jobs::ScheduleParallelFor(&RunTransformGroups, job, groups, 1, Unpack(dependency)) : Unpack(dependency);
        return Pack(jobs::Schedule(
            [job] {
                HeapFree(job->block);
                HeapFree(job->items);
                HeapFree(job->groupStarts);
                HeapFree(job);
            },
            h));
    }

    u64 JobsNative::Combine(u64 a, u64 b)
    {
        EnsureJobs();
        return Pack(jobs::CombineDependencies(Unpack(a), Unpack(b)));
    }

    u64 JobsNative::CombineMany(u64 handles, i32 count)
    {
        EnsureJobs();
        Array<jobs::JobHandle> list;
        const u64* h = reinterpret_cast<const u64*>(uptr(handles));
        for (i32 i = 0; i < count; i++)
            list.Push(Unpack(h[i]));
        return Pack(jobs::CombineDependencies(Span<const jobs::JobHandle>(list.Data(), list.Size())));
    }

    void JobsNative::Complete(u64 handle)
    {
        if (handle && jobs::IsInitialized())
            jobs::Complete(Unpack(handle));
    }

    bool JobsNative::IsCompleted(u64 handle) { return !handle || !jobs::IsInitialized() || jobs::IsCompleted(Unpack(handle)); }
    i32 JobsNative::WorkerCount()
    {
        EnsureJobs();
        return i32(jobs::WorkerCount());
    }
    i32 JobsNative::MaximumWorkerCount() { return i32(platform::LogicalCoreCount() > 1 ? platform::LogicalCoreCount() - 1 : 0); }

    // ---------------------------------------------------------------------------------------------- memory

    u64 JobsNative::Allocate(i64 size, i32 alignment, i32 allocator)
    {
        if (size < 0)
            return 0;
        Memory& m = Mem();
        std::lock_guard<std::mutex> guard(m.lock);
        usize s = usize(size ? size : 1), a = usize(alignment > 0 ? alignment : 16);
        void* p = nullptr;
        switch (ScriptAllocator(allocator))
        {
        case ScriptAllocator::Temp:
            p = TempAlloc(s, a);
            break;
        case ScriptAllocator::TempJob:
            p = HeapAlloc(s, a);
            m.tempJob.Set(u64(uptr(p)), Memory::Age{m.frame, false});
            break;
        case ScriptAllocator::Persistent:
        case ScriptAllocator::AudioKernel:
        case ScriptAllocator::Domain:
            p = HeapAlloc(s, a);
            break;
        default:
            HX_LOG_ERROR(JobsLog, "Invalid allocator {}", allocator);
            return 0;
        }
        if (allocator >= 0 && allocator < 8)
            m.live[allocator]++;
        return u64(uptr(p));
    }

    void JobsNative::Free(u64 pointer, i32 allocator)
    {
        if (!pointer)
            return;
        Memory& m = Mem();
        std::lock_guard<std::mutex> guard(m.lock);
        if (allocator >= 0 && allocator < 8)
            m.live[allocator]--;
        switch (ScriptAllocator(allocator))
        {
        case ScriptAllocator::Temp:
            // The arena goes at the end of the frame.
            break;
        case ScriptAllocator::TempJob:
            m.tempJob.Remove(pointer);
            HeapFree(reinterpret_cast<void*>(uptr(pointer)));
            break;
        default:
            HeapFree(reinterpret_cast<void*>(uptr(pointer)));
            break;
        }
    }

    void JobsNative::EndFrame()
    {
        Memory& m = Mem();
        std::lock_guard<std::mutex> guard(m.lock);
        for (void* p : m.large)
            HeapFree(p);
        m.large.Clear();
        m.block = 0;
        m.used = 0;
        m.live[i32(ScriptAllocator::Temp)] = 0;
        m.frame++;
        u32 old = 0;
        for (auto e : m.tempJob)
            if (!e.value.reported && m.frame - e.value.frame > kTempJobFrames)
            {
                e.value.reported = true;
                old++;
            }
        if (old)
            HX_LOG_WARNING(JobsLog, "Internal: JobTempAlloc has allocations that are more than the maximum lifespan of 4 frames old - this is not allowed and likely a leak ({} allocations)", old);
    }

    i32 JobsNative::LiveAllocations(i32 allocator)
    {
        Memory& m = Mem();
        std::lock_guard<std::mutex> guard(m.lock);
        return allocator >= 0 && allocator < 8 ? m.live[allocator] : 0;
    }

    // ---------------------------------------------------------------------------------------------- transforms

    u64 TransformAccessNative::Create(i32 capacity)
    {
        auto* a = new AccessArray();
        a->capacity = capacity > 0 ? capacity : 0;
        a->transforms.Reserve(usize(a->capacity));
        return u64(uptr(a));
    }

    void TransformAccessNative::Destroy(u64 array) { delete ArrayOf(array); }
    i32 TransformAccessNative::Length(u64 array) { return ArrayOf(array) ? i32(ArrayOf(array)->transforms.Size()) : 0; }
    i32 TransformAccessNative::Capacity(u64 array) { return ArrayOf(array) ? std::max(ArrayOf(array)->capacity, Length(array)) : 0; }

    void TransformAccessNative::SetCapacity(u64 array, i32 capacity)
    {
        if (AccessArray* a = ArrayOf(array))
        {
            a->capacity = std::max(capacity, i32(a->transforms.Size()));
            a->transforms.Reserve(usize(a->capacity));
        }
    }

    void TransformAccessNative::Add(u64 array, Transform* transform) { AddId(array, transform ? transform->GetEntityId() : EntityId()); }

    void TransformAccessNative::AddId(u64 array, EntityId transform)
    {
        if (AccessArray* a = ArrayOf(array))
        {
            a->transforms.Push(transform);
            a->capacity = std::max(a->capacity, i32(a->transforms.Size()));
        }
    }

    void TransformAccessNative::RemoveAtSwapBack(u64 array, i32 index)
    {
        AccessArray* a = ArrayOf(array);
        if (!a || index < 0 || usize(index) >= a->transforms.Size())
            return;
        a->transforms[usize(index)] = a->transforms.Back();
        a->transforms.Pop();
    }

    Transform* TransformAccessNative::Get(u64 array, i32 index) { return EntityIdToObject<Transform>(GetId(array, index)); }

    EntityId TransformAccessNative::GetId(u64 array, i32 index)
    {
        AccessArray* a = ArrayOf(array);
        return a && index >= 0 && usize(index) < a->transforms.Size() ? a->transforms[usize(index)] : EntityId();
    }

    void TransformAccessNative::Set(u64 array, i32 index, Transform* transform) { SetId(array, index, transform ? transform->GetEntityId() : EntityId()); }

    void TransformAccessNative::SetId(u64 array, i32 index, EntityId transform)
    {
        AccessArray* a = ArrayOf(array);
        if (a && index >= 0 && usize(index) < a->transforms.Size())
            a->transforms[usize(index)] = transform;
    }

    void TransformAccessNative::Clear(u64 array)
    {
        if (AccessArray* a = ArrayOf(array))
            a->transforms.Clear();
    }

    bool TransformAccessNative::IsValid(u64 hierarchy, u32 node)
    {
        TransformHierarchy* h = H(hierarchy);
        return h && node < h->count && !(h->flags[node] & TransformFlags::Dead);
    }

    Vector3 TransformAccessNative::LocalPosition(u64 hierarchy, u32 node) { return IsValid(hierarchy, node) ? H(hierarchy)->local[node].position : Vector3(); }
    Quaternion TransformAccessNative::LocalRotation(u64 hierarchy, u32 node) { return IsValid(hierarchy, node) ? H(hierarchy)->local[node].rotation : Quaternion::identity; }
    Vector3 TransformAccessNative::LocalScale(u64 hierarchy, u32 node) { return IsValid(hierarchy, node) ? H(hierarchy)->local[node].scale : Vector3(1, 1, 1); }

    Matrix4x4 TransformAccessNative::LocalToWorld(u64 hierarchy, u32 node)
    {
        Matrix4x4 m = Matrix4x4::identity;
        Quaternion r;
        if (IsValid(hierarchy, node))
            WorldOf(*H(hierarchy), node, m, r);
        return m;
    }

    Quaternion TransformAccessNative::WorldRotation(u64 hierarchy, u32 node)
    {
        Matrix4x4 m;
        Quaternion r = Quaternion::identity;
        if (IsValid(hierarchy, node))
            WorldOf(*H(hierarchy), node, m, r);
        return r;
    }

    void TransformAccessNative::SetLocal(u64 hierarchy, u32 node, Vector3 position, Quaternion rotation, Vector3 scale)
    {
        if (!IsValid(hierarchy, node))
            return;
        TransformHierarchy& h = *H(hierarchy);
        h.local[node] = LocalTRS{position, rotation, scale};
        TransformAccess::MarkChangedFromWorker(h, node);
    }

    void TransformAccessNative::SetWorldPositionAndRotation(u64 hierarchy, u32 node, Vector3 position, Quaternion rotation)
    {
        if (!IsValid(hierarchy, node))
            return;
        TransformHierarchy& h = *H(hierarchy);
        Matrix4x4 parent = Matrix4x4::identity;
        Quaternion parentRotation = Quaternion::identity;
        if (h.parent[node] >= 0)
            WorldOf(h, u32(h.parent[node]), parent, parentRotation);
        LocalTRS& l = h.local[node];
        l.position = parent.inverse().MultiplyPoint3x4(position);
        l.rotation = Quaternion::Inverse(parentRotation) * rotation;
        TransformAccess::MarkChangedFromWorker(h, node);
    }
}
