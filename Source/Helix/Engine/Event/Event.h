#pragma once

#include <Helix/Core/Containers/Array.h>

// Events of the engine (scene loads, the application's life): callbacks as a function and a user pointer. Invoked on the
// main thread unless an event says otherwise.
namespace helix::engine
{
    template <class... Args>
    class Event
    {
    public:
        using Callback = void (*)(void* user, Args... args);

        // An id for Remove.
        u32 Add(Callback callback, void* user = nullptr)
        {
            entries_.Push({callback, user, ++next_});
            return next_;
        }

        void Remove(u32 id)
        {
            for (usize i = 0; i < entries_.Size(); i++)
                if (entries_[i].id == id)
                {
                    entries_.RemoveAt(i);
                    return;
                }
        }

        bool Empty() const { return entries_.Empty(); }

        void Invoke(Args... args) const
        {
            if (entries_.Empty())
                return;
            // A copy: callbacks may add or remove callbacks.
            Array<Entry> now = entries_;
            for (const Entry& e : now)
                e.callback(e.user, args...);
        }

    private:
        struct Entry
        {
            Callback callback;
            void* user;
            u32 id;
        };
        Array<Entry> entries_;
        u32 next_ = 0;
    };

    // An event whose callbacks answer: Ask is true unless one of them says false (every callback is asked).
    template <class... Args>
    class Query
    {
    public:
        using Callback = bool (*)(void* user, Args... args);

        u32 Add(Callback callback, void* user = nullptr)
        {
            entries_.Push({callback, user, ++next_});
            return next_;
        }

        void Remove(u32 id)
        {
            for (usize i = 0; i < entries_.Size(); i++)
                if (entries_[i].id == id)
                {
                    entries_.RemoveAt(i);
                    return;
                }
        }

        bool Ask(Args... args) const
        {
            Array<Entry> now = entries_;
            bool yes = true;
            for (const Entry& e : now)
                yes = e.callback(e.user, args...) && yes;
            return yes;
        }

    private:
        struct Entry
        {
            Callback callback;
            void* user;
            u32 id;
        };
        Array<Entry> entries_;
        u32 next_ = 0;
    };
}
