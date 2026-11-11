#include <Support/TestClasses.h>

#include <Helix/Engine/Application/Application.h>
#include <Helix/Engine/PlayerLoop/PlayerLoop.h>
#include <Helix/Engine/Settings/Settings.h>
#include <Helix/Engine/Time/Time.h>
#include <Helix/Core/Logging/Log.h>
#include <Helix/Testing/Test.h>

#include <cmath>

using namespace helix;
using namespace helix::engine;
using namespace helix::engine::testing;

namespace
{
    // Time by hand.
    struct ManualClock final : TimeClock
    {
        double now = 100;
        double Now() override { return now; }
    };

    bool Near(double a, double b) { return std::fabs(a - b) < 1e-5; }

    struct Fresh
    {
        ManualClock clock;
        Fresh()
        {
            Time::SetClock(&clock);
            Time::Reset();
            Application::Reset();
            PlayerLoop::SetPlayerLoop(PlayerLoop::GetDefaultPlayerLoop());
        }
        ~Fresh()
        {
            PlayerLoop::SetPlayerLoop(PlayerLoop::GetDefaultPlayerLoop());
            Time::SetClock(nullptr);
            Time::Reset();
            Application::Reset();
            Cleanup();
        }
        void Frame(double seconds)
        {
            clock.now += seconds;
            Time::BeginFrame();
        }
    };

    Array<String>& Calls()
    {
        static Array<String> c;
        return c;
    }

    bool HasStrings(const Array<String>& got, std::initializer_list<const char*> expected)
    {
        bool same = got.Size() == expected.size();
        usize i = 0;
        for (const char* e : expected)
            same = same && got[i++] == StringView(e);
        if (!same)
        {
            std::printf("    calls:");
            for (const String& s : got)
                std::printf(" %.*s", int(s.Size()), s.Data());
            std::printf("\n");
        }
        return same;
    }

    PlayerLoopSystem Recorder(const char* name)
    {
        PlayerLoopSystem s;
        s.type = name;
        s.updateFunction = [](void* user) { Calls().Push(String(static_cast<const char*>(user))); };
        s.updateUser = const_cast<char*>(name);
        return s;
    }
}

HX_TEST(Time, FramesAreCappedAndScaled)
{
    Fresh f;
    f.Frame(0);
    HX_CHECK(Time::frameCount() == 1 && Time::deltaTime() == 0.0f);
    f.Frame(0.016);
    HX_CHECK(Near(Time::deltaTime(), 0.016) && Near(Time::timeAsDouble(), 0.016) && Near(Time::unscaledDeltaTime(), 0.016));
    // A stall of a second: game time grows by maximumDeltaTime, unscaled time by the whole second.
    f.Frame(1.0);
    HX_CHECK(Near(Time::deltaTime(), 0.3333333) && Near(Time::unscaledDeltaTime(), 1.0) && Near(Time::unscaledTimeAsDouble(), 1.016));
    double before = Time::timeAsDouble();
    // Time scale 0: frames go on, game time stands.
    Time::SetTimeScale(0);
    f.Frame(0.016);
    HX_CHECK(Time::deltaTime() == 0.0f && Time::timeAsDouble() == before && Time::frameCount() == 4);
    Time::SetTimeScale(2);
    f.Frame(0.01);
    HX_CHECK(Near(Time::deltaTime(), 0.02) && Near(Time::unscaledDeltaTime(), 0.01));
    // Captured frames take the same time each, whatever real time did.
    Time::SetTimeScale(1);
    Time::SetCaptureFramerate(50);
    f.Frame(0.5);
    HX_CHECK(Near(Time::deltaTime(), 0.02) && Time::captureFramerate() == 50 && Near(Time::captureDeltaTime(), 0.02));
    Time::SetCaptureDeltaTime(0);
    HX_CHECK(Time::smoothDeltaTime() > 0.0f && Near(Time::realtimeSinceStartupAsDouble(), 1.542));
}

HX_TEST(Time, FixedStepsCatchUpWithTheFrames)
{
    Fresh f;
    Time::SetFixedDeltaTime(0.02f);
    f.Frame(0);
    u32 steps = 0, frames = 0;
    bool insideRight = true;
    // 50 ms frames over a second: 50 steps of 20 ms, two or three a frame.
    for (int i = 0; i < 20; i++)
    {
        f.Frame(0.05);
        u32 here = 0;
        while (Time::NextFixedStep())
        {
            here++;
            insideRight = insideRight && Time::inFixedTimeStep() && Time::deltaTime() == 0.02f && Time::timeAsDouble() == Time::fixedTimeAsDouble();
        }
        HX_CHECK(here == 2 || here == 3);
        HX_CHECK(!Time::inFixedTimeStep() && Near(Time::deltaTime(), 0.05));
        steps += here;
        frames++;
    }
    HX_CHECK(steps == 50u && insideRight && frames == 20u);
    // No steps while the game time stands.
    Time::SetTimeScale(0);
    f.Frame(0.5);
    HX_CHECK(!Time::NextFixedStep());
}

HX_TEST(PlayerLoop, PhasesInOrderAndSystemsByName)
{
    Fresh f;
    PlayerLoopSystem loop = PlayerLoop::GetDefaultPlayerLoop();
    const char* phases[] = {"TimeUpdate", "Initialization", "EarlyUpdate", "FixedUpdate", "PreUpdate", "Update", "PreLateUpdate", "PostLateUpdate"};
    HX_REQUIRE(loop.subSystemList.Size() == 8u);
    bool ordered = true;
    for (u32 i = 0; i < 8; i++)
        ordered = ordered && loop.subSystemList[i].type == StringView(phases[i]);
    HX_CHECK(ordered && loop.subSystemList[3].loopConditionFunction != nullptr);
    // Systems of our own between the engine's.
    Calls().Clear();
    HX_CHECK(PlayerLoop::InsertSystem("Update", Recorder("update"), "ScriptRunBehaviourUpdate"));
    HX_CHECK(PlayerLoop::InsertSystem("FixedUpdate", Recorder("fixed")));
    HX_CHECK(PlayerLoop::InsertSystem("PreLateUpdate", Recorder("late")));
    HX_CHECK(PlayerLoop::InsertSystem("EarlyUpdate", Recorder("early"), "UpdatePreloading"));
    HX_CHECK(!PlayerLoop::InsertSystem("NoPhase", Recorder("x")));
    Time::SetFixedDeltaTime(0.02f);
    f.clock.now += 0.045;
    PlayerLoop::RunFrame();
    // First frame: no game time yet, no fixed steps.
    HX_CHECK(HasStrings(Calls(), {"early", "update", "late"}));
    Calls().Clear();
    f.clock.now += 0.045;
    PlayerLoop::RunFrame();
    HX_CHECK(HasStrings(Calls(), {"early", "fixed", "fixed", "update", "late"}));
    // A destroy waits for the frame's end; a scene load for the next frame.
    GameObject* g = GameObject::Create("gone");
    EntityId id = g->GetEntityId();
    Destroy(g);
    HX_CHECK(EntityIdIsValid(id));
    PlayerLoop::RunFrame();
    HX_CHECK(!EntityIdIsValid(id));
    // Changing the loop inside a frame takes effect after it.
    PlayerLoopSystem changer;
    changer.type = "changer";
    changer.updateFunction = [](void*) { PlayerLoop::RemoveSystem("late"); };
    PlayerLoop::InsertSystem("Update", changer);
    Calls().Clear();
    PlayerLoop::RunFrame();
    HX_CHECK(Calls().Size() >= 2u && Calls().Back() == StringView("late"));
    Calls().Clear();
    PlayerLoop::RunFrame();
    bool lateGone = true;
    for (const String& c : Calls())
        lateGone = lateGone && c != StringView("late");
    HX_CHECK(lateGone && PlayerLoop::RemoveSystem("changer") && !PlayerLoop::RemoveSystem("changer"));
}

HX_TEST(Application, SettingsFilesApplyToTimeTagsAndProduct)
{
    Fresh f;
    const char* file = R"(%YAML 1.1
%TAG !h! tag:helixengine,2026:
--- !h!5 &1
TimeManager:
  m_ObjectHideFlags: 0
  Fixed Timestep: 0.01
  Maximum Allowed Timestep: 0.1
  m_TimeScale: 0.5
  Maximum Particle Timestep: 0.03
--- !h!78 &2
TagManager:
  tags:
  - Enemy
  layers:
  - Default
  - TransparentFX
  - Ignore Raycast
  -
  - Water
  - UI
  -
  -
  - Ground
--- !h!129 &3
PlayerSettings:
  companyName: Acme
  productName: Rocket
  bundleVersion: 1.2.3
  applicationIdentifier: com.acme.rocket
)";
    Array<String> errors;
    HX_CHECK(LoadSettingsFile(StringView(file), errors) && errors.Empty());
    HX_CHECK(Time::fixedDeltaTime() == 0.01f && Time::maximumDeltaTime() == 0.1f && Time::timeScale() == 0.5f);
    TagHandle enemy;
    HX_CHECK(GetExistingTag("Enemy", enemy) && NameToLayer("Ground") == 8 && LayerName(4) == StringView("Water"));
    HX_CHECK(Application::companyName() == StringView("Acme") && Application::productName() == StringView("Rocket") && Application::version() == StringView("1.2.3"));
    HX_CHECK(Application::persistentDataPath().Contains("Acme/Rocket") && Application::identifier() == StringView("com.acme.rocket"));
    HX_CHECK(GetSettings<TimeManager>()->IsPersistent());
    // Back to the defaults for the other tests.
    TimeManager* t = GetSettings<TimeManager>();
    t->fixedTimestep = 0.02f, t->maximumAllowedTimestep = 0.3333333f, t->timeScale = 1;
    ApplySettings(ClassOf<TimeManager>());
    Application::SetProductInfo("DefaultCompany", "Product", "0.1", "com.DefaultCompany.Product");
}

HX_TEST(Application, QuitAsksThenUnloadsEverything)
{
    Fresh f;
    GameObject* g = GameObject::Create("alive");
    g->AddComponent<Logger>();
    GameObject* keep = GameObject::Create("kept");
    keep->AddComponent<Logger>();
    DontDestroyOnLoad(keep);
    EntityId gid = g->GetEntityId(), kid = keep->GetEntityId();
    static bool allow = false;
    u32 ask = Application::wantsToQuit().Add([](void*) { return allow; });
    Array<String> order;
    u32 q = Application::quitting().Add([](void* u) { static_cast<Array<String>*>(u)->Push("quitting"); }, &order);
    u32 un = Application::unloading().Add([](void* u) { static_cast<Array<String>*>(u)->Push("unloading"); }, &order);
    // A handler that says no keeps the application running.
    Application::Quit(3);
    HX_CHECK(Application::RunFrame() && order.Empty() && EntityIdIsValid(gid));
    allow = true;
    Events().Clear();
    Application::Quit(3);
    HX_CHECK(!Application::RunFrame() && Application::ExitCode() == 3 && !Application::RunFrame());
    HX_CHECK(order.Size() == 2u && order[0] == StringView("quitting") && order[1] == StringView("unloading"));
    // Every scene's objects went through their disable and destroy hooks, the DontDestroyOnLoad scene's too.
    HX_CHECK(!EntityIdIsValid(gid) && !EntityIdIsValid(kid));
    bool destroyedBoth = false;
    u32 n = 0;
    for (const String& e : Events())
        n += e.View().Contains(":destroy") ? 1 : 0;
    destroyedBoth = n == 2;
    HX_CHECK(destroyedBoth);
    Application::wantsToQuit().Remove(ask);
    Application::quitting().Remove(q);
    Application::unloading().Remove(un);
}

HX_TEST(Application, StateAndLogMessages)
{
    Fresh f;
    HX_CHECK(Application::isPlaying() && !Application::isEditor() && Application::targetFrameRate() == -1);
    Application::SetTargetFrameRate(0);
    HX_CHECK(Application::targetFrameRate() == -1);
    Array<bool> focus;
    u32 fc = Application::focusChanged().Add([](void* u, bool v) { static_cast<Array<bool>*>(u)->Push(v); }, &focus);
    Application::SetFocused(false);
    HX_CHECK(!Application::ShouldRun());
    Application::SetRunInBackground(true);
    HX_CHECK(Application::ShouldRun());
    Application::SetFocused(true);
    HX_CHECK(focus.Size() == 2u && !focus[0] && focus[1]);
    Application::focusChanged().Remove(fc);
    struct Seen
    {
        String text;
        LogType type = LogType::Log;
    } seen;
    u32 lm = Application::logMessageReceived().Add(
        [](void* u, StringView text, StringView, LogType type) {
            Seen& s = *static_cast<Seen*>(u);
            s.text = text;
            s.type = type;
        },
        &seen);
    LogChannel channel{"Test"};
    HX_LOG_WARNING(channel, "careful {}", 7);
    HX_CHECK(seen.text.View().Contains("careful 7") && seen.type == LogType::Warning);
    Application::logMessageReceived().Remove(lm);
    HX_CHECK(!Application::dataPath().Empty() && Application::streamingAssetsPath().EndsWith("/StreamingAssets"));
}
