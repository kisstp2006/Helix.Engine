#pragma once

#include <Helix/Core/Strings/String.h>
#include <Helix/Core/Basics/Tags.h>
#include <Helix/Engine/Event/Event.h>

// Application: the running player as scripts see it (product, platform, paths, frame rate, focus, quitting). The host
// (the player executable) feeds what only it knows (focus, low memory, deep links, the data folder) and runs frames
// with RunFrame until it returns false.
namespace helix::engine
{
    HX_ENUM(Namespace = "HelixEngine")
    enum class RuntimePlatform : i32
    {
        OSXEditor = 0,
        OSXPlayer = 1,
        WindowsPlayer = 2,
        WindowsEditor = 7,
        IPhonePlayer = 8,
        Android = 11,
        LinuxPlayer = 13,
        LinuxEditor = 16,
        WebGLPlayer = 17,
    };

    HX_ENUM(Namespace = "HelixEngine")
    enum class NetworkReachability : i32
    {
        NotReachable = 0,
        ReachableViaCarrierDataNetwork = 1,
        ReachableViaLocalAreaNetwork = 2,
    };

    HX_ENUM(Namespace = "HelixEngine")
    enum class ThreadPriority : i32
    {
        Low = 0,
        BelowNormal = 1,
        Normal = 2,
        High = 4,
    };

    HX_ENUM(Namespace = "HelixEngine")
    enum class LogType : i32
    {
        Error = 0,
        Assert = 1,
        Warning = 2,
        Log = 3,
        Exception = 4,
    };

    HX_CLASS(Namespace = "HelixEngine")
    class Application
    {
    public:
        // ---------------------------------------------------------------------------------------------- state
        HX_PROPERTY(ReadOnly) static bool isPlaying();
        HX_PROPERTY(ReadOnly) static bool isEditor();
        HX_PROPERTY(ReadOnly) static bool isBatchMode();
        HX_PROPERTY(ReadOnly) static bool isFocused();
        HX_PROPERTY(ReadOnly) static bool isMobilePlatform();
        HX_PROPERTY(ReadOnly) static bool isConsolePlatform();
        HX_PROPERTY(ReadOnly) static RuntimePlatform platform();
        HX_PROPERTY() static bool runInBackground();
        static void SetRunInBackground(bool value);
        // Frames per second to aim for; -1 is the platform's default (as fast as the display allows).
        HX_PROPERTY() static i32 targetFrameRate();
        static void SetTargetFrameRate(i32 framesPerSecond);
        HX_PROPERTY(ReadOnly) static NetworkReachability internetReachability();
        // The main thread time a frame gives to making the objects of background loads: Low 2 ms, BelowNormal 4 ms,
        // Normal 10 ms, High 50 ms.
        HX_PROPERTY() static ThreadPriority backgroundLoadingPriority();
        static void SetBackgroundLoadingPriority(ThreadPriority value);

        // ---------------------------------------------------------------------------------------------- product
        HX_PROPERTY(ReadOnly) static StringView companyName();
        HX_PROPERTY(ReadOnly) static StringView productName();
        HX_PROPERTY(ReadOnly) static StringView version();
        HX_PROPERTY(ReadOnly) static StringView identifier();
        // The engine's version.
        HX_PROPERTY(ReadOnly) static StringView helixVersion();

        // ---------------------------------------------------------------------------------------------- paths
        // The player's data folder (set by the host; next to the executable by default).
        HX_PROPERTY(ReadOnly) static StringView dataPath();
        HX_PROPERTY(ReadOnly) static StringView streamingAssetsPath();
        // Per user and product: <LocalLow on Windows, ~/.config elsewhere>/<company>/<product>.
        HX_PROPERTY(ReadOnly) static StringView persistentDataPath();
        HX_PROPERTY(ReadOnly) static StringView temporaryCachePath();
        HX_PROPERTY(ReadOnly) static StringView consoleLogPath();
        HX_PROPERTY(ReadOnly) static StringView absoluteURL();

        // ---------------------------------------------------------------------------------------------- quitting
        // Quits at the end of the frame: wantsToQuit is asked (any false cancels), then quitting, then every scene
        // unloads (disable and destroy hooks), then unloading.
        HX_FUNCTION() static void Quit(i32 exitCode = 0);

        // ---------------------------------------------------------------------------------------------- events
        HX_EVENT() static Query<>& wantsToQuit();
        HX_EVENT() static Event<>& quitting();
        HX_EVENT() static Event<>& unloading();
        HX_EVENT() static Event<bool>& focusChanged();
        // The application was paused (true) or resumed (false) by the system (mobile players; scripts' OnApplicationPause).
        static Event<bool>& pauseChanged();
        HX_EVENT(Delegate = "global::HelixEngine.Application.LowMemoryCallback") static Event<>& lowMemory();
        HX_EVENT() static Event<StringView>& deepLinkActivated();
        // Every frame, just before rendering.
        static Event<>& onBeforeRender();
        // Log messages of the main thread; the threaded one gets every thread's (its callbacks must be thread safe).
        HX_EVENT(Delegate = "global::HelixEngine.Application.LogCallback") static Event<StringView, StringView, LogType>& logMessageReceived();
        HX_EVENT(Delegate = "global::HelixEngine.Application.LogCallback") static Event<StringView, StringView, LogType>& logMessageReceivedThreaded();

        // ---------------------------------------------------------------------------------------------- host
        // One frame of the player loop and the quit flow; false once the application has quit.
        static bool RunFrame();
        // Whether frames should run now (focused, or running in the background).
        static bool ShouldRun();
        static i32 ExitCode();
        // Waits for the target frame rate (the player loop's FramePacing system).
        static void PaceFrame();
        static void SetFocused(bool focused);
        static void SetPaused(bool paused);
        static void SetBatchMode(bool batchMode);
        static void SetInternetReachability(NetworkReachability reachability);
        static void SetDataPath(StringView path);
        static void SetAbsoluteURL(StringView url);
        static void SetProductInfo(StringView companyName, StringView productName, StringView version, StringView identifier);
        static void ReportLowMemory();
        static void ReportDeepLink(StringView url);
        // Back to the state at startup (tests).
        static void Reset();
    };
}
