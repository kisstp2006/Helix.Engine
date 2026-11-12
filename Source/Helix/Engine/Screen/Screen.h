#pragma once

#include <Helix/Core/Basics/Span.h>
#include <Helix/Core/Basics/Tags.h>
#include <Helix/Core/Containers/Array.h>
#include <Helix/Core/Strings/String.h>
#include <Helix/Engine/Async/AsyncOperation.h>
#include <Helix/Math/Geometry.h>

// Screen: the display and the player's window as scripts see them. The host reports what the system tells (size, dpi,
// mode, displays, the window's place) and applies at the end of the frame what scripts asked (resolution, full screen
// mode, window moves, orientation, sleep timeout, brightness).
namespace helix::engine
{
    HX_ENUM(Namespace = "HelixEngine")
    enum class FullScreenMode : i32
    {
        // The display switched to the resolution (Windows; elsewhere FullScreenWindow).
        ExclusiveFullScreen = 0,
        // A window without a frame covering the display (borderless full screen).
        FullScreenWindow = 1,
        MaximizedWindow = 2,
        Windowed = 3,
    };

    HX_ENUM(Namespace = "HelixEngine")
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

    HX_STRUCT(Namespace = "HelixEngine")
    struct RefreshRate
    {
        HX_FIELD() u32 numerator = 0;
        HX_FIELD() u32 denominator = 1;

        // Hertz.
        f64 value() const { return denominator != 0 ? f64(numerator) / f64(denominator) : 0.0; }
        bool operator==(const RefreshRate&) const = default;
    };

    HX_STRUCT(Namespace = "HelixEngine")
    struct Resolution
    {
        HX_FIELD() i32 width = 0;
        HX_FIELD() i32 height = 0;
        HX_FIELD() RefreshRate refreshRateRatio;

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

    HX_CLASS(Namespace = "HelixEngine")
    class Screen
    {
    public:
        HX_PROPERTY(ReadOnly) static i32 width();
        HX_PROPERTY(ReadOnly) static i32 height();
        // 0 when the system does not tell.
        HX_PROPERTY(ReadOnly) static f32 dpi();

        // ---------------------------------------------------------------------------------------------- modes
        HX_PROPERTY() static bool fullScreen();
        // True goes to FullScreenWindow (or keeps the full screen mode in use), false to Windowed.
        static void SetFullScreen(bool value);
        HX_PROPERTY() static FullScreenMode fullScreenMode();
        static void SetFullScreenMode(FullScreenMode mode);
        // The display's current mode (the window's size when windowed).
        HX_PROPERTY(ReadOnly) static Resolution currentResolution();
        // The modes of the window's display.
        static Span<const Resolution> resolutions();
        // Applied at the end of the frame.
        HX_FUNCTION() static void SetResolution(i32 width, i32 height, FullScreenMode fullscreenMode, RefreshRate preferredRefreshRate);
        HX_FUNCTION() static void SetResolution(i32 width, i32 height, FullScreenMode fullscreenMode);
        HX_FUNCTION() static void SetResolution(i32 width, i32 height, bool fullscreen);
        HX_PROPERTY(ReadOnly) static i32 msaaSamples();
        HX_FUNCTION() static void SetMSAASamples(i32 numSamples);

        // ---------------------------------------------------------------------------------------------- mobile
        HX_PROPERTY() static ScreenOrientation orientation();
        static void SetOrientation(ScreenOrientation value);
        HX_PROPERTY() static bool autorotateToPortrait();
        static void SetAutorotateToPortrait(bool value);
        HX_PROPERTY() static bool autorotateToPortraitUpsideDown();
        static void SetAutorotateToPortraitUpsideDown(bool value);
        HX_PROPERTY() static bool autorotateToLandscapeLeft();
        static void SetAutorotateToLandscapeLeft(bool value);
        HX_PROPERTY() static bool autorotateToLandscapeRight();
        static void SetAutorotateToLandscapeRight(bool value);
        HX_PROPERTY(ReadOnly) static Rect safeArea();
        static Span<const Rect> cutouts();
        // Seconds of no input before the screen dims, or a SleepTimeout value.
        HX_PROPERTY() static i32 sleepTimeout();
        static void SetSleepTimeout(i32 value);
        HX_PROPERTY() static f32 brightness();
        static void SetBrightness(f32 value);

        // ---------------------------------------------------------------------------------------------- window
        HX_PROPERTY(ReadOnly) static Vector2Int mainWindowPosition();
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
