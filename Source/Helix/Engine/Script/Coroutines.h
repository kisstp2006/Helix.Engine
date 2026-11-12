#pragma once

#include <Helix/Core/Basics/Types.h>

// Coroutines of both languages on one scheduler. A language runs its own routines (a C# enumerator, a C++20
// coroutine) and says what each yielded; the scheduler keeps them in the wait list of their point in the frame and
// resumes them there:
//
//   after every Update of the frame (ScriptRunDelayedDynamicFrameRate): the next frame, scaled and real seconds,
//   polled waits (custom yield instructions, async operations);
//   after each fixed step (ScriptRunDelayedFixedFrameRate): the fixed update;
//   at the end of the frame, after rendering (PresentAfterDraw): the end of the frame;
//   at once when the coroutine it waits for ends.
//
// A coroutine stops when its GameObject becomes inactive in the hierarchy, when its behaviour is destroyed, by
// StopCoroutine and StopAllCoroutines; disabling the behaviour does not stop it. A wait without a routine (an
// Awaitable's) has no owner and only ends when its wait does or it is stopped.
namespace helix::engine
{
    class GameBehaviour;

    enum class CoroutineWait : u32
    {
        // The routine ended.
        Ended,
        NextFrame,
        Seconds,
        RealSeconds,
        FixedUpdate,
        EndOfFrame,
        // The language's poll function answers whether it keeps waiting, once per frame after Update.
        Poll,
        // Another coroutine (`inner`): resumed when it ends.
        Coroutine,
    };

    // What a routine yielded, filled by its language when it runs.
    struct CoroutineYield
    {
        u32 wait = u32(CoroutineWait::Ended);
        u32 reserved = 0;
        f64 seconds = 0;
        u64 inner = 0;
    };

    // A language's routines: run one to its next yield, poll its wait, free it. `state` is the language's.
    struct CoroutineLanguage
    {
        void (*resume)(u64 state, u64 handle, CoroutineYield* out) = nullptr;
        u8 (*poll)(u64 state, u64 handle) = nullptr;
        void (*release)(u64 state) = nullptr;
    };

    enum class CoroutinePoint : u32
    {
        FixedUpdate,
        Update,
        EndOfFrame,
    };

    u32 RegisterCoroutineLanguage(const CoroutineLanguage& language);

    // Starts a routine of `owner` and runs it to its first yield. 0 when it ended there, or when the owner's
    // GameObject is inactive (the reference's error is logged; the routine is released).
    u64 StartCoroutine(GameBehaviour* owner, u32 language, u64 state);
    // A wait without a routine to run first (an Awaitable's): resumed once `wait` is over.
    u64 StartCoroutineWait(u32 language, u64 state, const CoroutineYield& wait);
    void StopCoroutine(u64 handle);
    void StopAllCoroutines(const GameBehaviour* owner);
    bool IsCoroutineRunning(u64 handle);
    // The routines of one point of the frame (the player loop runs them).
    void RunCoroutines(CoroutinePoint point);
    u32 CoroutineCount();

    // Work a language queued for the main thread (the continuations of C# Tasks and Awaitables), run once per frame
    // in the Update phase (ScriptRunDelayedTasks), in the order the runners were added.
    void AddDelayedTaskRunner(void (*run)());
    void RunDelayedTasks();
}
