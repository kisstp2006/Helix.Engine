#include <Helix/Engine/Time/Time.h>

#include <chrono>

namespace helix::engine
{
    namespace
    {
        class SteadyClock final : public TimeClock
        {
        public:
            double Now() override { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
        };

        // The defaults of the TimeManager settings.
        struct TimeState
        {
            TimeClock* clock = nullptr;
            bool started = false;
            double start = 0, lastReal = 0;
            double time = 0, unscaledTime = 0, fixedTime = 0, fixedUnscaledTime = 0, levelLoad = 0;
            f32 deltaTime = 0, unscaledDeltaTime = 0, smoothDeltaTime = 0;
            f32 fixedDeltaTime = 0.02f, maximumDeltaTime = 0.3333333f, maximumParticleDeltaTime = 0.03f, timeScale = 1;
            f32 captureDeltaTime = 0;
            i32 frameCount = 0;
            bool inFixed = false;
        };

        SteadyClock gSteady;
        TimeState gTime;

        double Now()
        {
            TimeClock* c = gTime.clock ? gTime.clock : static_cast<TimeClock*>(&gSteady);
            return c->Now();
        }
    }

    double Time::timeAsDouble() { return gTime.inFixed ? gTime.fixedTime : gTime.time; }
    f32 Time::deltaTime() { return gTime.inFixed ? gTime.fixedDeltaTime : gTime.deltaTime; }
    f32 Time::unscaledDeltaTime() { return gTime.inFixed ? fixedUnscaledDeltaTime() : gTime.unscaledDeltaTime; }
    double Time::unscaledTimeAsDouble() { return gTime.inFixed ? gTime.fixedUnscaledTime : gTime.unscaledTime; }
    f32 Time::smoothDeltaTime() { return gTime.smoothDeltaTime; }
    f32 Time::fixedDeltaTime() { return gTime.fixedDeltaTime; }

    void Time::SetFixedDeltaTime(f32 seconds)
    {
        // The reference keeps it at least 0.0001 s.
        gTime.fixedDeltaTime = seconds < 0.0001f ? 0.0001f : seconds;
    }

    f32 Time::fixedUnscaledDeltaTime() { return gTime.timeScale > 0 ? gTime.fixedDeltaTime / gTime.timeScale : gTime.fixedDeltaTime; }
    double Time::fixedTimeAsDouble() { return gTime.fixedTime; }
    double Time::fixedUnscaledTimeAsDouble() { return gTime.fixedUnscaledTime; }
    f32 Time::maximumDeltaTime() { return gTime.maximumDeltaTime; }
    void Time::SetMaximumDeltaTime(f32 seconds) { gTime.maximumDeltaTime = seconds > gTime.fixedDeltaTime ? seconds : gTime.fixedDeltaTime; }
    f32 Time::maximumParticleDeltaTime() { return gTime.maximumParticleDeltaTime; }
    void Time::SetMaximumParticleDeltaTime(f32 seconds) { gTime.maximumParticleDeltaTime = seconds > 0 ? seconds : 0; }
    f32 Time::timeScale() { return gTime.timeScale; }
    void Time::SetTimeScale(f32 scale) { gTime.timeScale = scale > 0 ? scale : 0; }

    double Time::realtimeSinceStartupAsDouble()
    {
        if (!gTime.started)
            return 0;
        return Now() - gTime.start;
    }

    double Time::timeSinceLevelLoadAsDouble() { return timeAsDouble() - gTime.levelLoad; }
    i32 Time::frameCount() { return gTime.frameCount; }
    bool Time::inFixedTimeStep() { return gTime.inFixed; }
    f32 Time::captureDeltaTime() { return gTime.captureDeltaTime; }
    void Time::SetCaptureDeltaTime(f32 seconds) { gTime.captureDeltaTime = seconds > 0 ? seconds : 0; }
    i32 Time::captureFramerate() { return gTime.captureDeltaTime > 0 ? i32(1.0f / gTime.captureDeltaTime + 0.5f) : 0; }
    void Time::SetCaptureFramerate(i32 fps) { gTime.captureDeltaTime = fps > 0 ? 1.0f / f32(fps) : 0; }

    void Time::BeginFrame()
    {
        TimeState& t = gTime;
        double now = Now();
        if (!t.started)
        {
            t.started = true;
            t.start = now;
            t.lastReal = now;
        }
        double real = now - t.lastReal;
        t.lastReal = now;
        t.inFixed = false;
        t.unscaledDeltaTime = f32(real);
        t.unscaledTime += real;
        // Captured frames take the same time each; otherwise real time, capped.
        double step = t.captureDeltaTime > 0 ? double(t.captureDeltaTime) : (real < t.maximumDeltaTime ? real : double(t.maximumDeltaTime));
        double scaled = step * t.timeScale;
        t.deltaTime = f32(scaled);
        t.time += scaled;
        t.smoothDeltaTime = t.frameCount == 0 ? t.deltaTime : t.smoothDeltaTime * 0.8f + t.deltaTime * 0.2f;
        t.frameCount++;
    }

    bool Time::NextFixedStep()
    {
        TimeState& t = gTime;
        // A tiny tolerance so steps that end exactly on the frame's time run in that frame.
        if (t.fixedTime + t.fixedDeltaTime <= t.time + 1e-9)
        {
            t.fixedTime += t.fixedDeltaTime;
            t.fixedUnscaledTime += fixedUnscaledDeltaTime();
            t.inFixed = true;
            return true;
        }
        t.inFixed = false;
        return false;
    }

    void Time::LevelLoaded() { gTime.levelLoad = gTime.time; }

    void Time::SetClock(TimeClock* clock)
    {
        gTime.clock = clock;
        gTime.started = false;
    }

    void Time::Reset()
    {
        TimeClock* clock = gTime.clock;
        gTime = TimeState();
        gTime.clock = clock;
    }
}
