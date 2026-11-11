#include <Helix/Engine/Object/Implementation/ObjectAccess.h>

#include <Helix/Core/Basics/Assert.h>
#include <Helix/Core/Memory/PoolAllocator.h>

// Under AddressSanitizer every object gets its own allocation, so a use after destroy is reported instead of reading a
// pool block that another object now holds.
#if defined(__SANITIZE_ADDRESS__)
#define HX_OBJECT_POOLS 0
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HX_OBJECT_POOLS 0
#endif
#endif
#ifndef HX_OBJECT_POOLS
#define HX_OBJECT_POOLS 1
#endif

namespace helix::engine
{
    namespace
    {
        struct Registry;
        void Publish(Registry& r);

        struct Registry
        {
            Array<ClassInfo> classes;
            // Depth first numbers and the class holding each number.
            Array<u32> byOrder;
            // The order and subtree size of each class, by index (compact copies for loops).
            Array<u32> orders, sizes;
            HashMap<i32, u32> byId;
            // A pool per class that has objects.
            Array<PoolAllocator*> pools;

            ~Registry()
            {
                for (PoolAllocator* p : pools)
                    delete p;
            }
        };

        Registry& Classes()
        {
            static Registry r;
            return r;
        }

        // Numbers the classes depth first from the roots, children in registration order.
        void Renumber(Registry& r)
        {
                r.byOrder.Clear();
            r.orders.Resize(r.classes.Size());
            r.sizes.Resize(r.classes.Size());
            u32 n = u32(r.classes.Size());
            Array<u32> stack;
            for (u32 root = n; root-- > 0;)
                if (r.classes[root].base == kNoClass)
                    stack.Push(root);
            // Iterative depth first walk: push children in reverse so the first registered comes first.
            Array<u32> exit;
            exit.Resize(n, 0u);
            while (!stack.Empty())
            {
                u32 top = stack.Back();
                stack.Pop();
                if (top & 0x80000000u)
                {
                    u32 k = top & 0x7FFFFFFFu;
                    r.classes[k].orderEnd = u32(r.byOrder.Size());
                    r.sizes[k] = r.classes[k].orderEnd - r.classes[k].order;
                    continue;
                }
                r.classes[top].order = r.orders[top] = u32(r.byOrder.Size());
                r.byOrder.Push(top);
                stack.Push(top | 0x80000000u);
                for (u32 c = n; c-- > 0;)
                    if (r.classes[c].base == top)
                        stack.Push(c);
            }
        }

        struct Table
        {
            // Slot 0 is never used, so EntityId 0 is None.
            Array<detail::Slot> slots;
            u32 freeHead = 0;
            u32 live = 0;
            // The objects of each class (exactly that class), dense.
            Array<Array<Object*>> perClass;
        };

        Table& Objects()
        {
            static Table t = [] {
                Table table;
                table.slots.Push(detail::Slot());
                return table;
            }();
            return t;
        }
    }

    detail::ClassTables detail::gClassTables;
    detail::ObjectTables detail::gObjectTables;

    namespace
    {
        void Publish(Registry& r)
        {
            detail::gClassTables = {r.classes.Data(), r.orders.Data(), r.sizes.Data(), r.byOrder.Data(), u32(r.classes.Size())};
        }

        void PublishSlots(Table& t) { detail::gObjectTables = {t.slots.Data(), u32(t.slots.Size()), t.perClass.Data()}; }
    }

    u32 RegisterClass(ClassInfo info)
    {
        Registry& r = Classes();
        HX_ASSERT(info.base == kNoClass || info.base < r.classes.Size());
        HX_ASSERT(!r.byId.Find(info.classId));
        u32 index = u32(r.classes.Size());
        r.byId.Set(info.classId, index);
        // 64 objects a block: small classes share cache lines and pages with their kind.
        r.pools.Push(HX_OBJECT_POOLS && info.create ? new PoolAllocator(info.size, info.alignment, 64) : nullptr);
        r.classes.Push(Move(info));
        Renumber(r);
        Publish(r);
        Objects().perClass.Resize(r.classes.Size());
        PublishSlots(Objects());
        return index;
    }


    u32 FindClassById(i32 classId)
    {
        const u32* i = Classes().byId.Find(classId);
        return i ? *i : kNoClass;
    }

    u32 FindClassByName(StringView name)
    {
        const Registry& r = Classes();
        for (u32 i = 0; i < r.classes.Size(); i++)
            if (r.classes[i].name == name)
                return i;
        return kNoClass;
    }

    Object* AddObject(Object* object, u32 klass) { return ObjectAccess::Add(object, klass); }

    void ReserveObjects(u32 count)
    {
        Table& t = Objects();
        t.slots.Reserve(t.slots.Size() + count);
        PublishSlots(t);
    }

    Object* ObjectAccess::Add(Object* object, u32 klass)
    {
        Table& t = Objects();
        u32 slot;
        if (t.freeHead)
        {
            slot = t.freeHead;
            t.freeHead = t.slots[slot].nextFree;
        }
        else
        {
            slot = u32(t.slots.Size());
            t.slots.Push(detail::Slot());
            PublishSlots(t);
        }
        detail::Slot& s = t.slots[slot];
        s.object = object;
        object->id_ = EntityId::Make(slot, s.generation);
        object->classIndex_ = klass;
        Array<Object*>& list = t.perClass[klass];
        object->listPosition_ = u32(list.Size());
        list.Push(object);
        t.live++;
        return object;
    }

    namespace
    {
        void* Allocate(u32 klass, const ClassInfo& c)
        {
#if HX_OBJECT_POOLS
            return Classes().pools[klass]->Allocate(c.size, c.alignment);
#else
            (void)klass;
            return HeapAllocator().Allocate(c.size, c.alignment);
#endif
        }
    }

    Object* NewObjectOfClass(u32 klass)
    {
        if (klass >= ClassCount())
            return nullptr;
        const ClassInfo& c = GetClass(klass);
        if (!c.create)
            return nullptr;
        Object* o = c.create(Allocate(klass, c));
        ObjectAccess::SetPooled(o);
        return AddObject(o, klass);
    }

    Object* CloneObject(const Object& original)
    {
        u32 klass = original.ClassIndex();
        const ClassInfo& c = GetClass(klass);
        if (!c.clone)
            return nullptr;
        Object* o = c.clone(Allocate(klass, c), original);
        ObjectAccess::SetPooled(o);
        return AddObject(o, klass);
    }

    void ObjectAccess::Remove(Object* object)
    {
        Table& t = Objects();
        u32 slot = object->id_.Slot();
        detail::Slot& s = t.slots[slot];
        HX_ASSERT(s.object == object);
        s.object = nullptr;
        // A slot whose generation would wrap is retired, so an old id never resolves to a new object.
        if (++s.generation != 0)
        {
            s.nextFree = t.freeHead;
            t.freeHead = slot;
        }
        Array<Object*>& list = t.perClass[object->classIndex_];
        Object* last = list.Back();
        list[object->listPosition_] = last;
        last->listPosition_ = object->listPosition_;
        list.Pop();
        t.live--;
        object->id_ = EntityId();
    }

    void DeleteObject(Object* object)
    {
        if (!object)
            return;
        u32 klass = object->classIndex_;
        bool pooled = object->pooled_;
        ObjectAccess::Remove(object);
        if (!pooled)
        {
            delete object;
            return;
        }
        const ClassInfo& c = GetClass(klass);
        object->~Object();
#if HX_OBJECT_POOLS
        Classes().pools[klass]->Free(object, c.size, c.alignment);
#else
        HeapAllocator().Free(object, c.size, c.alignment);
#endif
    }

    bool EntityIdIsValid(EntityId id) { return EntityIdToObject(id) != nullptr; }

    void EntityIdsToObjectList(Span<const EntityId> ids, Array<Object*>& out)
    {
        out.Clear();
        out.Reserve(ids.Size());
        for (EntityId id : ids)
            out.Push(EntityIdToObject(id));
    }

    void EntityIdsToValidArray(Span<const EntityId> ids, Array<EntityId>& out)
    {
        out.Clear();
        for (EntityId id : ids)
            if (EntityIdToObject(id))
                out.Push(id);
    }

    u32 ObjectCount() { return Objects().live; }

    void FindObjectsByType(u32 klass, FindObjectsInactive inactive, Array<Object*>& out)
    {
        out.Clear();
        bool include = inactive == FindObjectsInactive::Include;
        ForEachObjectOfClass(klass, [&](Object* o) {
            if (IsFindable(o, include))
                out.Push(o);
            return true;
        });
    }

    Object* FindAnyObjectByType(u32 klass, FindObjectsInactive inactive)
    {
        Object* found = nullptr;
        bool include = inactive == FindObjectsInactive::Include;
        ForEachObjectOfClass(klass, [&](Object* o) {
            if (!IsFindable(o, include))
                return true;
            found = o;
            return false;
        });
        return found;
    }

    void FindObjectsOfTypeAll(u32 klass, Array<Object*>& out)
    {
        out.Clear();
        ForEachObjectOfClass(klass, [&](Object* o) {
            out.Push(o);
            return true;
        });
    }
}
