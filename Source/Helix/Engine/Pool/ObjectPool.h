#pragma once

#include <Helix/Core/Containers/Array.h>
#include <Helix/Core/Logging/Log.h>

// Pools of reusable items (typically GameObjects made from a prefab): Get takes an inactive one or makes one, Release
// hands it back; beyond maxSize released items are destroyed. The callbacks are function pointers with a user pointer.
namespace helix::engine
{
    inline LogChannel PoolLog{"Engine"};

    template <class T>
    struct PoolCallbacks
    {
        T (*create)(void* user) = nullptr;
        void (*onGet)(void* user, T& item) = nullptr;
        void (*onRelease)(void* user, T& item) = nullptr;
        void (*onDestroy)(void* user, T& item) = nullptr;
        void* user = nullptr;
    };

    template <class T>
    class ObjectPool
    {
    public:
        // collectionCheck refuses releasing an item that is already in the pool (a linear search).
        explicit ObjectPool(const PoolCallbacks<T>& callbacks, bool collectionCheck = true, u32 defaultCapacity = 10, u32 maxSize = 10000)
            : callbacks_(callbacks), check_(collectionCheck), maxSize_(maxSize > 0 ? maxSize : 1)
        {
            inactive_.Reserve(defaultCapacity);
        }
        ObjectPool(const ObjectPool&) = delete;
        ObjectPool& operator=(const ObjectPool&) = delete;
        ~ObjectPool() { Clear(); }

        T Get()
        {
            T item;
            if (inactive_.Empty())
            {
                item = callbacks_.create(callbacks_.user);
                all_++;
            }
            else
            {
                item = inactive_.Back();
                inactive_.Pop();
            }
            if (callbacks_.onGet)
                callbacks_.onGet(callbacks_.user, item);
            return item;
        }

        void Release(T item)
        {
            if (check_)
                for (const T& i : inactive_)
                    if (i == item)
                    {
                        HX_LOG_ERROR(PoolLog, "Trying to release an object that has already been released to the pool.");
                        return;
                    }
            if (callbacks_.onRelease)
                callbacks_.onRelease(callbacks_.user, item);
            if (inactive_.Size() < maxSize_)
                inactive_.Push(item);
            else
            {
                if (callbacks_.onDestroy)
                    callbacks_.onDestroy(callbacks_.user, item);
                all_--;
            }
        }

        // Destroys the items in the pool; the ones handed out are forgotten.
        void Clear()
        {
            if (callbacks_.onDestroy)
                for (T& i : inactive_)
                    callbacks_.onDestroy(callbacks_.user, i);
            inactive_.Clear();
            all_ = 0;
        }
        void Dispose() { Clear(); }

        u32 CountAll() const { return all_; }
        u32 CountActive() const { return all_ - CountInactive(); }
        u32 CountInactive() const { return u32(inactive_.Size()); }

    private:
        PoolCallbacks<T> callbacks_;
        bool check_;
        u32 maxSize_;
        u32 all_ = 0;
        Array<T> inactive_;
    };

    // The same contract over a linked list of nodes that are kept when items are taken (no storage regrows).
    template <class T>
    class LinkedPool
    {
    public:
        explicit LinkedPool(const PoolCallbacks<T>& callbacks, bool collectionCheck = true, u32 maxSize = 10000)
            : callbacks_(callbacks), check_(collectionCheck), maxSize_(maxSize > 0 ? maxSize : 1)
        {
        }
        LinkedPool(const LinkedPool&) = delete;
        LinkedPool& operator=(const LinkedPool&) = delete;
        ~LinkedPool()
        {
            Clear();
            while (spare_)
            {
                Node* n = spare_;
                spare_ = n->next;
                delete n;
            }
        }

        T Get()
        {
            T item;
            if (!head_)
            {
                item = callbacks_.create(callbacks_.user);
                all_++;
            }
            else
            {
                Node* n = head_;
                head_ = n->next;
                item = n->item;
                n->next = spare_;
                spare_ = n;
                inactive_--;
            }
            if (callbacks_.onGet)
                callbacks_.onGet(callbacks_.user, item);
            return item;
        }

        void Release(T item)
        {
            if (check_)
                for (Node* n = head_; n; n = n->next)
                    if (n->item == item)
                    {
                        HX_LOG_ERROR(PoolLog, "Trying to release an object that has already been released to the pool.");
                        return;
                    }
            if (callbacks_.onRelease)
                callbacks_.onRelease(callbacks_.user, item);
            if (inactive_ < maxSize_)
            {
                Node* n = spare_ ? spare_ : new Node();
                spare_ = n == spare_ ? n->next : spare_;
                n->item = item;
                n->next = head_;
                head_ = n;
                inactive_++;
            }
            else
            {
                if (callbacks_.onDestroy)
                    callbacks_.onDestroy(callbacks_.user, item);
                all_--;
            }
        }

        void Clear()
        {
            while (head_)
            {
                Node* n = head_;
                head_ = n->next;
                if (callbacks_.onDestroy)
                    callbacks_.onDestroy(callbacks_.user, n->item);
                n->next = spare_;
                spare_ = n;
            }
            inactive_ = 0;
            all_ = 0;
        }
        void Dispose() { Clear(); }

        u32 CountAll() const { return all_; }
        u32 CountActive() const { return all_ - inactive_; }
        u32 CountInactive() const { return inactive_; }

    private:
        struct Node
        {
            T item;
            Node* next = nullptr;
        };
        PoolCallbacks<T> callbacks_;
        bool check_;
        u32 maxSize_;
        u32 all_ = 0;
        u32 inactive_ = 0;
        Node* head_ = nullptr;
        Node* spare_ = nullptr;
    };
}
