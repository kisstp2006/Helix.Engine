#include <Helix/Engine/Async/Implementation/AsyncAccess.h>
#include <Helix/Engine/Screen/Screen.h>

namespace helix::engine
{
    namespace
    {
        struct ScreenState
        {
            ScreenReport report;
            ScreenRequests requests;
            bool pending = false;
            i32 sleepTimeout = SleepTimeout::SystemSetting;
            bool autorotate[4] = {true, true, true, true};
        };

        ScreenState& S()
        {
            static ScreenState s;
            return s;
        }

        bool IsFull(FullScreenMode mode) { return mode == FullScreenMode::ExclusiveFullScreen || mode == FullScreenMode::FullScreenWindow; }

        // The mode and size a resolution request starts from: the last request of this frame, else what is reported.
        ScreenRequests& ResolutionRequest()
        {
            ScreenState& s = S();
            if (!s.requests.resolution)
            {
                s.requests.resolution = true;
                s.requests.width = s.report.width;
                s.requests.height = s.report.height;
                s.requests.fullScreenMode = s.report.fullScreenMode;
                s.requests.refreshRate = {};
            }
            s.pending = true;
            return s.requests;
        }
    }

    i32 Screen::width() { return S().report.width; }
    i32 Screen::height() { return S().report.height; }
    f32 Screen::dpi() { return S().report.dpi; }

    bool Screen::fullScreen() { return IsFull(S().report.fullScreenMode); }

    void Screen::SetFullScreen(bool value)
    {
        ScreenRequests& r = ResolutionRequest();
        if (value != IsFull(r.fullScreenMode))
            r.fullScreenMode = value ? FullScreenMode::FullScreenWindow : FullScreenMode::Windowed;
    }

    FullScreenMode Screen::fullScreenMode() { return S().report.fullScreenMode; }
    void Screen::SetFullScreenMode(FullScreenMode mode) { ResolutionRequest().fullScreenMode = mode; }

    Resolution Screen::currentResolution() { return S().report.currentResolution; }
    Span<const Resolution> Screen::resolutions() { return Span<const Resolution>(S().report.resolutions.Data(), S().report.resolutions.Size()); }

    void Screen::SetResolution(i32 width, i32 height, FullScreenMode fullscreenMode, RefreshRate preferredRefreshRate)
    {
        if (width <= 0 || height <= 0)
            return;
        ScreenRequests& r = ResolutionRequest();
        r.width = width;
        r.height = height;
        r.fullScreenMode = fullscreenMode;
        r.refreshRate = preferredRefreshRate;
    }

    void Screen::SetResolution(i32 width, i32 height, FullScreenMode fullscreenMode) { SetResolution(width, height, fullscreenMode, RefreshRate()); }

    void Screen::SetResolution(i32 width, i32 height, bool fullscreen)
    {
        SetResolution(width, height, fullscreen ? FullScreenMode::FullScreenWindow : FullScreenMode::Windowed, RefreshRate());
    }

    i32 Screen::msaaSamples() { return S().report.msaaSamples; }

    void Screen::SetMSAASamples(i32 numSamples)
    {
        ScreenState& s = S();
        s.requests.msaaSamples = true;
        s.requests.msaaSamplesValue = numSamples < 1 ? 1 : numSamples;
        s.pending = true;
    }

    ScreenOrientation Screen::orientation() { return S().report.orientation; }

    void Screen::SetOrientation(ScreenOrientation value)
    {
        ScreenState& s = S();
        s.requests.orientation = true;
        s.requests.orientationValue = value;
        s.pending = true;
    }

    bool Screen::autorotateToPortrait() { return S().autorotate[0]; }
    void Screen::SetAutorotateToPortrait(bool value) { S().autorotate[0] = value; }
    bool Screen::autorotateToPortraitUpsideDown() { return S().autorotate[1]; }
    void Screen::SetAutorotateToPortraitUpsideDown(bool value) { S().autorotate[1] = value; }
    bool Screen::autorotateToLandscapeLeft() { return S().autorotate[2]; }
    void Screen::SetAutorotateToLandscapeLeft(bool value) { S().autorotate[2] = value; }
    bool Screen::autorotateToLandscapeRight() { return S().autorotate[3]; }
    void Screen::SetAutorotateToLandscapeRight(bool value) { S().autorotate[3] = value; }

    Rect Screen::safeArea()
    {
        const ScreenReport& r = S().report;
        if (r.safeArea.width > 0 && r.safeArea.height > 0)
            return r.safeArea;
        return Rect{0, 0, f32(r.width), f32(r.height)};
    }

    Span<const Rect> Screen::cutouts() { return Span<const Rect>(S().report.cutouts.Data(), S().report.cutouts.Size()); }

    i32 Screen::sleepTimeout() { return S().sleepTimeout; }

    void Screen::SetSleepTimeout(i32 value)
    {
        ScreenState& s = S();
        s.sleepTimeout = value;
        s.requests.sleepTimeout = true;
        s.requests.sleepTimeoutValue = value;
        s.pending = true;
    }

    f32 Screen::brightness() { return S().report.brightness; }

    void Screen::SetBrightness(f32 value)
    {
        ScreenState& s = S();
        s.requests.brightness = true;
        s.requests.brightnessValue = value < 0 ? 0 : value > 1 ? 1 : value;
        s.pending = true;
    }

    Vector2Int Screen::mainWindowPosition() { return S().report.windowPosition; }

    DisplayInfo Screen::mainWindowDisplayInfo()
    {
        const ScreenReport& r = S().report;
        return r.display < r.displays.Size() ? r.displays[r.display] : DisplayInfo();
    }

    AsyncOperation Screen::MoveMainWindowTo(const DisplayInfo& display, Vector2Int position)
    {
        ScreenState& s = S();
        AsyncOperation op{AsyncAccess::New()};
        for (u32 i = 0; i < s.report.displays.Size(); i++)
        {
            const DisplayInfo& d = s.report.displays[i];
            if (d.name == display.name && d.width == display.width && d.height == display.height && d.workArea == display.workArea)
            {
                // A move asked earlier in the frame is replaced: it is done as far as its caller is concerned.
                if (s.requests.move)
                    AsyncAccess::Finish(s.requests.moveOperation);
                s.requests.move = true;
                s.requests.moveDisplay = i;
                s.requests.movePosition = position;
                s.requests.moveOperation = op.id;
                s.pending = true;
                return op;
            }
        }
        // Not a display of this system: nothing to wait for.
        AsyncAccess::Finish(op.id);
        return op;
    }

    void Screen::GetDisplayLayout(Array<DisplayInfo>& out)
    {
        out.Clear();
        for (const DisplayInfo& d : S().report.displays)
            out.Push(d);
    }

    void Screen::Report(const ScreenReport& report)
    {
        ScreenReport& r = S().report;
        r.width = report.width;
        r.height = report.height;
        r.dpi = report.dpi;
        r.fullScreenMode = report.fullScreenMode;
        r.windowPosition = report.windowPosition;
        r.display = report.display;
        r.orientation = report.orientation;
        r.safeArea = report.safeArea;
        r.cutouts.Clear();
        for (const Rect& c : report.cutouts)
            r.cutouts.Push(c);
        r.displays.Clear();
        for (const DisplayInfo& d : report.displays)
            r.displays.Push(d);
        r.resolutions.Clear();
        for (const Resolution& m : report.resolutions)
            r.resolutions.Push(m);
        r.currentResolution = report.currentResolution;
        r.brightness = report.brightness;
        r.msaaSamples = report.msaaSamples;
    }

    const ScreenReport& Screen::Reported() { return S().report; }

    bool Screen::TakeRequests(ScreenRequests& out)
    {
        ScreenState& s = S();
        if (!s.pending)
            return false;
        out = s.requests;
        s.requests = ScreenRequests();
        s.pending = false;
        return true;
    }

    void Screen::FinishWindowMove(u32 operation)
    {
        if (operation != 0)
            AsyncAccess::Finish(operation);
    }

    void Screen::Reset()
    {
        ScreenState& s = S();
        if (s.requests.move)
            AsyncAccess::Finish(s.requests.moveOperation);
        s = ScreenState();
    }
}
