#include <Helix/Engine/Screen/Screen.h>
#include <Helix/Testing/Test.h>

using namespace helix;
using namespace helix::engine;

namespace
{
    ScreenReport Desktop()
    {
        ScreenReport r;
        r.width = 1280;
        r.height = 720;
        r.dpi = 144;
        r.fullScreenMode = FullScreenMode::Windowed;
        r.windowPosition = {100, 80};
        r.display = 1;
        DisplayInfo a;
        a.name = String("Left");
        a.width = 1920;
        a.height = 1080;
        a.refreshRate = {60, 1};
        a.workArea = {0, 0, 1920, 1040};
        DisplayInfo b = a;
        b.name = String("Right");
        b.workArea = {1920, 0, 1920, 1040};
        r.displays.Push(a);
        r.displays.Push(b);
        r.resolutions.Push({1920, 1080, {60, 1}});
        r.resolutions.Push({1280, 720, {60, 1}});
        r.currentResolution = {1280, 720, {60, 1}};
        return r;
    }
}

HX_TEST(Screen, ReportsWhatTheHostTells)
{
    Screen::Reset();
    HX_CHECK(Screen::width() == 0 && Screen::dpi() == 0 && Screen::msaaSamples() == 1);
    Screen::Report(Desktop());
    HX_CHECK(Screen::width() == 1280 && Screen::height() == 720 && Screen::dpi() == 144);
    HX_CHECK(!Screen::fullScreen() && Screen::fullScreenMode() == FullScreenMode::Windowed);
    HX_CHECK(Screen::resolutions().Size() == 2 && Screen::currentResolution().width == 1280);
    HX_CHECK(Screen::currentResolution().refreshRateRatio.value() == 60.0);
    HX_CHECK((Screen::mainWindowPosition() == Vector2Int{100, 80}));
    HX_CHECK(Screen::mainWindowDisplayInfo().name == StringView("Right"));
    Array<DisplayInfo> layout;
    Screen::GetDisplayLayout(layout);
    HX_CHECK(layout.Size() == 2 && layout[0].name == StringView("Left"));
    // The safe area is the whole screen when the host tells none.
    HX_CHECK((Screen::safeArea() == Rect{0, 0, 1280, 720}));
    HX_CHECK(Screen::cutouts().Empty());
    ScreenRequests none;
    HX_CHECK(!Screen::TakeRequests(none));
    Screen::Reset();
}

HX_TEST(Screen, RequestsWaitForTheHost)
{
    Screen::Reset();
    Screen::Report(Desktop());
    // Full screen keeps the size; the last request of the frame wins and reads change only once the host reports.
    Screen::SetFullScreen(true);
    HX_CHECK(!Screen::fullScreen());
    ScreenRequests r;
    HX_REQUIRE(Screen::TakeRequests(r));
    HX_CHECK(r.resolution && r.width == 1280 && r.height == 720 && r.fullScreenMode == FullScreenMode::FullScreenWindow);
    HX_CHECK(!Screen::TakeRequests(r));
    Screen::SetResolution(800, 600, FullScreenMode::ExclusiveFullScreen, RefreshRate{120, 1});
    Screen::SetResolution(1024, 768, true);
    Screen::SetSleepTimeout(SleepTimeout::NeverSleep);
    Screen::SetBrightness(2);
    Screen::SetMSAASamples(4);
    Screen::SetOrientation(ScreenOrientation::LandscapeRight);
    HX_REQUIRE(Screen::TakeRequests(r));
    HX_CHECK(r.width == 1024 && r.height == 768 && r.fullScreenMode == FullScreenMode::FullScreenWindow && r.refreshRate.numerator == 0);
    HX_CHECK(r.sleepTimeout && r.sleepTimeoutValue == -1 && Screen::sleepTimeout() == -1);
    HX_CHECK(r.brightness && r.brightnessValue == 1);
    HX_CHECK(r.msaaSamples && r.msaaSamplesValue == 4);
    HX_CHECK(r.orientation && r.orientationValue == ScreenOrientation::LandscapeRight);
    // Off again: windowed at the reported size.
    ScreenReport full = Desktop();
    full.fullScreenMode = FullScreenMode::FullScreenWindow;
    Screen::Report(full);
    HX_CHECK(Screen::fullScreen());
    Screen::SetFullScreen(true);
    HX_REQUIRE(Screen::TakeRequests(r));
    HX_CHECK(r.fullScreenMode == FullScreenMode::FullScreenWindow);
    Screen::SetFullScreen(false);
    HX_REQUIRE(Screen::TakeRequests(r));
    HX_CHECK(r.fullScreenMode == FullScreenMode::Windowed);
    Screen::Reset();
}

HX_TEST(Screen, WindowMovesFinishWhenTheHostSaysSo)
{
    Screen::Reset();
    Screen::Report(Desktop());
    AsyncOperation move = Screen::MoveMainWindowTo(Screen::mainWindowDisplayInfo(), Vector2Int{10, 20});
    HX_CHECK(move.IsValid() && !move.isDone());
    ScreenRequests r;
    HX_REQUIRE(Screen::TakeRequests(r));
    HX_CHECK(r.move && r.moveDisplay == 1 && (r.movePosition == Vector2Int{10, 20}) && r.moveOperation == move.id);
    Screen::FinishWindowMove(r.moveOperation);
    HX_CHECK(move.isDone());
    // A display of no system is done at once.
    DisplayInfo other;
    other.name = String("Elsewhere");
    HX_CHECK(Screen::MoveMainWindowTo(other, Vector2Int{0, 0}).isDone());
    // A second move in one frame replaces the first.
    AsyncOperation a = Screen::MoveMainWindowTo(Screen::mainWindowDisplayInfo(), Vector2Int{1, 1});
    AsyncOperation b = Screen::MoveMainWindowTo(Screen::mainWindowDisplayInfo(), Vector2Int{2, 2});
    HX_CHECK(a.isDone() && !b.isDone());
    HX_REQUIRE(Screen::TakeRequests(r));
    HX_CHECK((r.movePosition == Vector2Int{2, 2}));
    Screen::FinishWindowMove(r.moveOperation);
    Screen::Reset();
}
