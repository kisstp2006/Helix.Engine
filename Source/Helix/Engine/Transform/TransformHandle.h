#pragma once

#include <Helix/Engine/Transform/Transform.h>

// TransformHandle: a transform by its EntityId, for code that keeps transforms by value. The Transform members are
// reached through ->; a handle of a destroyed transform resolves to null.
namespace helix::engine
{
    struct TransformChildEnumerator;
    struct TransformChildRange;

    struct TransformHandle
    {
        EntityId id;

        static constexpr TransformHandle None() { return {}; }
        Transform* Get() const { return EntityIdToObject<Transform>(id); }
        Transform* operator->() const { return Get(); }
        bool IsValid() const { return Get() != nullptr; }
        bool HasParent() const
        {
            Transform* t = Get();
            return t && t->parent();
        }
        TransformHandle parent() const
        {
            Transform* t = Get();
            Transform* p = t ? t->parent() : nullptr;
            return p ? TransformHandle{p->GetEntityId()} : None();
        }
        friend bool operator==(TransformHandle a, TransformHandle b) { return a.id == b.id; }

        using ChildEnumerator = TransformChildEnumerator;
        using ChildRange = TransformChildRange;
        // The direct children in order; changing the hierarchy while walking it ends the walk early or skips nodes.
        ChildEnumerator GetDirectChildrenEnumerator() const;
        ChildRange DirectChildren() const;
    };

    // Walks the direct children of a TransformHandle.
    struct TransformChildEnumerator
    {
        const TransformHierarchy* hierarchy = nullptr;
        u32 next = 0, end = 0;
        TransformHandle current;

        TransformHandle Current() const { return current; }
        bool MoveNext()
        {
            while (hierarchy && next < end)
            {
                u32 i = next;
                next += hierarchy->size[i];
                if (!(hierarchy->flags[i] & TransformFlags::Dead))
                {
                    current = TransformHandle{hierarchy->transforms[i]->GetEntityId()};
                    return true;
                }
            }
            current = TransformHandle::None();
            return false;
        }
    };

    struct TransformChildRange
    {
        TransformChildEnumerator first;
        struct Iterator
        {
            TransformChildEnumerator e;
            bool done;
            TransformHandle operator*() const { return e.Current(); }
            Iterator& operator++()
            {
                done = !e.MoveNext();
                return *this;
            }
            bool operator!=(const Iterator& o) const { return done != o.done; }
        };
        Iterator begin() const
        {
            Iterator i{first, false};
            i.done = !i.e.MoveNext();
            return i;
        }
        Iterator end() const { return {first, true}; }
    };

    inline TransformChildEnumerator TransformHandle::GetDirectChildrenEnumerator() const
    {
        TransformChildEnumerator e;
        if (Transform* t = Get())
        {
            const TransformHierarchy& h = t->Hierarchy();
            e.hierarchy = &h;
            e.next = t->NodeIndex() + 1;
            e.end = t->NodeIndex() + h.size[t->NodeIndex()];
        }
        return e;
    }
    inline TransformChildRange TransformHandle::DirectChildren() const { return {GetDirectChildrenEnumerator()}; }

    inline TransformHandle GameObject::transformHandle() const
    {
        Transform* t = transform();
        return t ? TransformHandle{t->GetEntityId()} : TransformHandle::None();
    }

    inline TransformHandle Component::transformHandle() const
    {
        Transform* t = transform();
        return t ? TransformHandle{t->GetEntityId()} : TransformHandle::None();
    }
}
