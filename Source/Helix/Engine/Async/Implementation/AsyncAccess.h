#pragma once

#include <Helix/Core/Containers/Array.h>
#include <Helix/Engine/Async/AsyncOperation.h>
#include <Helix/Engine/Object/EntityId.h>

namespace helix::engine
{
    struct OperationState
    {
        bool done = false;
        f32 progress = 0;
        i32 priority = 0;
        bool allowActivation = true;
        // The asset of a ResourceRequest.
        EntityId result;
        // Told when the priority changes (asset loads reorder their queue).
        void (*priorityChanged)(u32 id, i32 priority) = nullptr;
        struct Callback
        {
            void (*callback)(void*, AsyncOperation);
            void* user;
        };
        Array<Callback> completed = Array<Callback>();
    };

    struct AsyncAccess
    {
        static u32 New(bool allowActivation = true, i32 priority = 0);
        // Null for operations that are done and forgotten.
        static OperationState* Find(u32 id);
        // Marks the operation done and calls its callbacks.
        static void Finish(u32 id);
    };
}
