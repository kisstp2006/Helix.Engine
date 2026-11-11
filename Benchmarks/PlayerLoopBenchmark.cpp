#include <Helix/Engine/PlayerLoop/PlayerLoop.h>
#include <Helix/Engine/Time/Time.h>
#include <Helix/Testing/Bench.h>

using namespace helix;
using namespace helix::engine;

namespace
{
    // The same tree walked recursively every frame, for comparison with the compiled list.
    void Walk(const PlayerLoopSystem& s)
    {
        do
        {
            if (s.loopConditionFunction && !s.loopConditionFunction(s.loopUser))
                return;
            if (s.updateFunction)
                s.updateFunction(s.updateUser);
            for (const PlayerLoopSystem& sub : s.subSystemList)
                Walk(sub);
        } while (s.loopConditionFunction);
    }
}

// Items are frames of the engine's loop with nothing to do (the engine's own systems still run).
HX_BENCH(PlayerLoopEmptyFrame)
{
    PlayerLoop::SetPlayerLoop(PlayerLoop::GetDefaultPlayerLoop());
    state.SetItemsPerIteration(1000);
    while (state.KeepRunning())
        for (int i = 0; i < 1000; i++)
            PlayerLoop::RunFrame();
}

// Items are frames: the same loop walked as a tree.
HX_BENCH(PlayerLoopEmptyFrameTreeWalk)
{
    PlayerLoopSystem loop = PlayerLoop::GetDefaultPlayerLoop();
    state.SetItemsPerIteration(1000);
    while (state.KeepRunning())
        for (int i = 0; i < 1000; i++)
            Walk(loop);
}
