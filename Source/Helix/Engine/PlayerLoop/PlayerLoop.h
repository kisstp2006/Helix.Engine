#pragma once

#include <Helix/Core/Containers/Array.h>
#include <Helix/Core/Strings/String.h>

// The player loop: what runs each frame, as a tree of named systems in the reference's phases (TimeUpdate,
// Initialization, EarlyUpdate, FixedUpdate, PreUpdate, Update, PreLateUpdate, PostLateUpdate). Engine systems are
// registered into it by name; SetPlayerLoop compiles the tree into a flat list that RunFrame walks.
namespace helix::engine
{
    struct PlayerLoopSystem
    {
        // The system's name (its phase's systems are below it).
        String type;
        void (*updateFunction)(void* user) = nullptr;
        void* updateUser = nullptr;
        // Runs the system (and its subsystems) again while this returns true; null runs it once.
        bool (*loopConditionFunction)(void* user) = nullptr;
        void* loopUser = nullptr;
        Array<PlayerLoopSystem> subSystemList = Array<PlayerLoopSystem>();
    };

    class PlayerLoop
    {
    public:
        // The engine's loop: the reference's phases and subsystem names, the engine's own systems registered into it
        // (systems without a function keep their place for ordering).
        static PlayerLoopSystem GetDefaultPlayerLoop();
        static PlayerLoopSystem GetCurrentPlayerLoop();
        // Replaces the whole loop.
        static void SetPlayerLoop(const PlayerLoopSystem& loop);

        // Adds a system to the current loop under the system named `parent` (a phase or any system), before the one
        // named `before` (empty: last). False when the parent is not there.
        static bool InsertSystem(StringView parent, const PlayerLoopSystem& system, StringView before = StringView());
        // Removes the first system of that name; false when there is none.
        static bool RemoveSystem(StringView type);

        // One frame of the current loop.
        static void RunFrame();
    };
}
