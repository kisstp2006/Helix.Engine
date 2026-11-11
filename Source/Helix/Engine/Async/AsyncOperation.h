#pragma once

#include <Helix/Core/Basics/Types.h>

// AsyncOperation: a scene load or unload, an asset load or an unload of unused assets in progress. A handle into one
// table of operations: an operation that finished long ago reads as done.
namespace helix::engine
{
    class Object;

    struct AsyncOperation
    {
        u32 id = 0;

        bool IsValid() const { return id != 0; }
        bool isDone() const;
        // 0 to 1; a load waiting for allowSceneActivation stays at 0.9.
        f32 progress() const;
        // Loads with a higher priority go first.
        i32 priority() const;
        void SetPriority(i32 value) const;
        bool allowSceneActivation() const;
        void SetAllowSceneActivation(bool value) const;
        // Called when the operation is done; at once when it already is.
        void AddCompleted(void (*callback)(void* user, AsyncOperation op), void* user = nullptr) const;
    };

    // Resources.LoadAsync: the operation and the asset it loads (null until it is done, and when nothing loaded).
    struct ResourceRequest : AsyncOperation
    {
        Object* asset() const;
    };
}
