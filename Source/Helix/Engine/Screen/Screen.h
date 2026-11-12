#pragma once

#include <Helix/Core/Basics/Span.h>
#include <Helix/Core/Containers/Array.h>
#include <Helix/Core/Strings/String.h>
#include <Helix/Engine/Async/AsyncOperation.h>
#include <Helix/Math/Geometry.h>

// Screen: the display and the player's window as scripts see them. The host reports what the system tells (size, dpi,
// mode, displays, the window's place) and applies at the end of the frame what scripts asked (resolution, full screen
// mode, window moves, orientation, sleep timeout, brightness).
namespace helix::engine
{
    enum class FullScreenMode : i32
    {
        // The display switched to the resolution (Windows; elsewhere FullScreenWindow).
        ExclusiveFullScreen = 0,
        // A window without a frame covering the display (borderless full screen).
        FullScreenWindow = 1,
        MaximizedWindow = 2,
        Windowed = 3,
    };

    enum class ScreenOrientation : i32
    {
        Portrait = 1,
        PortraitUpsideDown = 2,
        LandscapeLeft = 3,
        LandscapeRight = 4,
        AutoRotation = 5,
    };

    struct SleepTimeout
    {
        static constexpr i32 NeverSleep = -1;
        static constexpr i32 SystemSetting = -2;
    };

    struct RefreshRate
    {
        u32 numerator = 0;
        u32 denominator = 1;

        // Hertz.
        f64 value() const { return denominator != 0 ? f64(numerator) / f64(denominator) : 0.0; }
        bool operator==(const RefreshRate&) const = default;
    };

    struct Resolution
    {
        i32 width = 0;
        i32 height = 0;
        RefreshRate refreshRateRatio;

        bool operator==(const Resolution&) const = default;
    };

    struct DisplayInfo
    {
        String name;
        i32 width = 0;
        i32 height = 0;
        RefreshRate refreshRate;
        // The part of the desktop not taken by task bars, in desktop pixels.
        RectInt workArea;
    };

    // What the host knows of the screen; reported once a frame (or when it changes).
    struct ScreenReport
    {
        // The render target of the screen in pixels.
        i32 width = 0;
        i32 height = 0;
        f32 dpi = 0;
        FullScreenMode fullScreenMode = FullScreenMode::Windowed;
        // The window's top left in desktop pixels and the display that holds it (an index into displays).
        Vector2Int windowPosition;
        u32 display = 0;
        ScreenOrientation orientation = ScreenOrientation::LandscapeLeft;
        // Pixels from the bottom left; the whole screen when empty.
        Rect safeArea;
        Array<Rect> cutouts = Array<Rect>();
        // The displays, the primary one first.
        Array<DisplayInfo> displays = Array<DisplayInfo>();
        // The modes of the window's display and the current one.
        Array<Resolution> resolutions = Array<Resolution>();
        Resolution currentResolution;
        f32 brightness = 1;
        i32 msaaSamples = 1;
    };

    // What scripts asked since the host's last look; each part only when its flag is set.
    struct ScreenRequests
    {
        bool resolution = false;
        i32 width = 0;
        i32 height = 0;
        FullScreenMode fullScreenMode = FullScreenMode::Windowed;
        RefreshRate refreshRate;

        bool move = false;
        u32 moveDisplay = 0;
        Vector2Int movePosition;
        // Finished with Screen::FinishWindowMove once the window is there.
        u32 moveOperation = 0;

        bool orientation = false;
        ScreenOrientation orientationValue = ScreenOrientation::AutoRotation;
        bool sleepTimeout = false;
        i32 sleepTimeoutValue = SleepTimeout::SystemSetting;
        bool brightness = false;
        f32 brightnessValue = 1;
        bool msaaSamples = false;
        i32 msaaSamplesValue = 1;
    };

    class Screen
    {
    public:
        static i32 width();
        static i32 height();
        // 0 when the system does not tell.
        static f32 dpi();

        // ---------------------------------------------------------------------------------------------- modes
        static bool fullScreen();
        // True goes to FullScreenWindow (or keeps the full screen mode in use), false to Windowed.
        static void SetFullScreen(bool value);
        static FullScreenMode fullScreenMode();
        static void SetFullScreenMode(FullScreenMode mode);
        // The display's current mode (the window's size when windowed).
        static Resolution currentResolution();
        // The modes of the window's display.
        static Span<const Resolution> resolutions();
        // Applied at the end of the frame.
        static void SetResolution(i32 width, i32 height, FullScreenMode fullscreenMode, RefreshRate preferredRefreshRate);
        static void SetResolution(i32 width, i32 height, FullScreenMode fullscreenMode);
        static void SetResolution(i32 width, i32 height, bool fullscreen);
        static i32 msaaSamples();
        static void SetMSAASamples(i32 numSamples);

        // ---------------------------------------------------------------------------------------------- mobile
        static ScreenOrientation orientation();
        static void SetOrientation(ScreenOrientation value);
        static bool autorotateToPortrait();
        static void SetAutorotateToPortrait(bool value);
        static bool autorotateToPortraitUpsideDown();
        static void SetAutorotateToPortraitUpsideDown(bool value);
        static bool autorotateToLandscapeLeft();
        static void SetAutorotateToLandscapeLeft(bool value);
        static bool autorotateToLandscapeRight();
        static void SetAutorotateToLandscapeRight(bool value);
        static Rect safeArea();
        static Span<const Rect> cutouts();
        // Seconds of no input before the screen dims, or a SleepTimeout value.
        static i32 sleepTimeout();
        static void SetSleepTimeout(i32 value);
        static f32 brightness();
        static void SetBrightness(f32 value);

        // ---------------------------------------------------------------------------------------------- window
        static Vector2Int mainWindowPosition();
        static DisplayInfo mainWindowDisplayInfo();
        // Moves the window to a position relative to the display's top left; done when the window is there.
        static AsyncOperation MoveMainWindowTo(const DisplayInfo& display, Vector2Int position);
        static void GetDisplayLayout(Array<DisplayInfo>& out);

        // ---------------------------------------------------------------------------------------------- host
        static void Report(const ScreenReport& report);
        static const ScreenReport& Reported();
        // What scripts asked since the last call; false when nothing.
        static bool TakeRequests(ScreenRequests& out);
        static void FinishWindowMove(u32 operation);
        // Back to the state at startup (tests).
        static void Reset();
    };
}
