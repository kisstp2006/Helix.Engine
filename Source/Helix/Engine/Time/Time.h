#pragma once

#include <Helix/Core/Basics/Tags.h>

#include <Helix/Core/Basics/Types.h>

// Time: the frame's clock as scripts see it. The player loop's TimeUpdate system starts each frame (BeginFrame) and the
// FixedUpdate phase asks for fixed steps (NextFixedStep); everything else reads. Real time comes from a TimeClock the
// host can replace (tests and capture runs drive it by hand).
namespace helix::engine
{
    class TimeClock
    {
    public:
        virtual ~TimeClock() = default;
        // Seconds from an arbitrary start, never going back.
        virtual double Now() = 0;
    };

    HX_CLASS(Namespace = "HelixEngine")
    class Time
    {
    public:
        // The game time at the start of the frame (the fixed time inside a fixed step).
        HX_PROPERTY(ReadOnly) static f32 time() { return f32(timeAsDouble()); }
        HX_PROPERTY(ReadOnly) static double timeAsDouble();
        // The scaled frame time, capped by maximumDeltaTime (fixedDeltaTime inside a fixed step; 0 with timeScale 0).
        HX_PROPERTY(ReadOnly) static f32 deltaTime();
        HX_PROPERTY(ReadOnly) static f32 unscaledDeltaTime();
        HX_PROPERTY(ReadOnly) static f32 unscaledTime() { return f32(unscaledTimeAsDouble()); }
        HX_PROPERTY(ReadOnly) static double unscaledTimeAsDouble();
        HX_PROPERTY(ReadOnly) static f32 smoothDeltaTime();
        HX_PROPERTY() static f32 fixedDeltaTime();
        static void SetFixedDeltaTime(f32 seconds);
        HX_PROPERTY(ReadOnly) static f32 fixedUnscaledDeltaTime();
        HX_PROPERTY(ReadOnly) static f32 fixedTime() { return f32(fixedTimeAsDouble()); }
        HX_PROPERTY(ReadOnly) static double fixedTimeAsDouble();
        HX_PROPERTY(ReadOnly) static f32 fixedUnscaledTime() { return f32(fixedUnscaledTimeAsDouble()); }
        HX_PROPERTY(ReadOnly) static double fixedUnscaledTimeAsDouble();
        HX_PROPERTY() static f32 maximumDeltaTime();
        static void SetMaximumDeltaTime(f32 seconds);
        HX_PROPERTY() static f32 maximumParticleDeltaTime();
        static void SetMaximumParticleDeltaTime(f32 seconds);
        HX_PROPERTY() static f32 timeScale();
        static void SetTimeScale(f32 scale);
        // Real time since the start, read now (not at the start of the frame).
        HX_PROPERTY(ReadOnly) static f32 realtimeSinceStartup() { return f32(realtimeSinceStartupAsDouble()); }
        HX_PROPERTY(ReadOnly) static double realtimeSinceStartupAsDouble();
        HX_PROPERTY(ReadOnly) static f32 timeSinceLevelLoad() { return f32(timeSinceLevelLoadAsDouble()); }
        HX_PROPERTY(ReadOnly) static double timeSinceLevelLoadAsDouble();
        HX_PROPERTY(ReadOnly) static i32 frameCount();
        HX_PROPERTY(ReadOnly) static bool inFixedTimeStep();
        // A fixed frame time regardless of real time (0: off); captureFramerate is its reciprocal.
        HX_PROPERTY() static f32 captureDeltaTime();
        static void SetCaptureDeltaTime(f32 seconds);
        HX_PROPERTY() static i32 captureFramerate();
        static void SetCaptureFramerate(i32 framesPerSecond);

        // ------------------------------------------------------------------ for the player loop and the host
        // Starts a frame: real time read, the capped and scaled delta added.
        static void BeginFrame();
        // True (and the fixed time moved on by a step) while the fixed time is behind the frame's time.
        static bool NextFixedStep();
        // A Single scene load: timeSinceLevelLoad counts from now.
        static void LevelLoaded();
        // Null for the steady clock of the system.
        static void SetClock(TimeClock* clock);
        // Back to the state at startup (tests).
        static void Reset();
    };
}
