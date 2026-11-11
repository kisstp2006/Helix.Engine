#include <Helix/Core/Containers/HashMap.h>
#include <Helix/Engine/Async/Implementation/AsyncAccess.h>
#include <Helix/Engine/Object/Object.h>

namespace helix::engine
{
    namespace
    {
        struct Operations
        {
            HashMap<u32, OperationState> table;
            // Finished operations, oldest first: only the last ones are kept.
            Array<u32> finished;
            u32 next = 0;
        };

        Operations& Ops()
        {
            static Operations s;
            return s;
        }
    }

    u32 AsyncAccess::New(bool allowActivation, i32 priority)
    {
        Operations& s = Ops();
        u32 id = ++s.next;
        if (id == 0)
            id = ++s.next;
        OperationState& op = s.table.Set(id, OperationState());
        op.allowActivation = allowActivation;
        op.priority = priority;
        return id;
    }

    OperationState* AsyncAccess::Find(u32 id) { return Ops().table.Find(id); }

    void AsyncAccess::Finish(u32 id)
    {
        Operations& s = Ops();
        OperationState* op = s.table.Find(id);
        if (!op || op->done)
            return;
        op->done = true;
        op->progress = 1;
        Array<OperationState::Callback> callbacks = Move(op->completed);
        for (const OperationState::Callback& c : callbacks)
            c.callback(c.user, AsyncOperation{id});
        s.finished.Push(id);
        // Long finished operations are forgotten (they read as done).
        if (s.finished.Size() > 256)
        {
            s.table.Remove(s.finished[0]);
            s.finished.RemoveAt(0);
        }
    }

    bool AsyncOperation::isDone() const
    {
        const OperationState* op = AsyncAccess::Find(id);
        return !op || op->done;
    }

    f32 AsyncOperation::progress() const
    {
        const OperationState* op = AsyncAccess::Find(id);
        return op ? op->progress : 1.0f;
    }

    i32 AsyncOperation::priority() const
    {
        const OperationState* op = AsyncAccess::Find(id);
        return op ? op->priority : 0;
    }

    void AsyncOperation::SetPriority(i32 value) const
    {
        OperationState* op = AsyncAccess::Find(id);
        if (!op || op->priority == value)
            return;
        op->priority = value;
        if (op->priorityChanged && !op->done)
            op->priorityChanged(id, value);
    }

    bool AsyncOperation::allowSceneActivation() const
    {
        const OperationState* op = AsyncAccess::Find(id);
        return !op || op->allowActivation;
    }

    void AsyncOperation::SetAllowSceneActivation(bool value) const
    {
        if (OperationState* op = AsyncAccess::Find(id))
            op->allowActivation = value;
    }

    void AsyncOperation::AddCompleted(void (*callback)(void*, AsyncOperation), void* user) const
    {
        OperationState* op = AsyncAccess::Find(id);
        if (!op || op->done)
        {
            callback(user, *this);
            return;
        }
        op->completed.Push({callback, user});
    }

    Object* ResourceRequest::asset() const
    {
        const OperationState* op = AsyncAccess::Find(id);
        return op && op->done ? EntityIdToObject(op->result) : nullptr;
    }
}
