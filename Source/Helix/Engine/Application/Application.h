#pragma once

#include <Helix/Core/Strings/String.h>
#include <Helix/Engine/Event/Event.h>

// Application: the running player as scripts see it (product, platform, paths, frame rate, focus, quitting). The host
// (the player executable) feeds what only it knows (focus, low memory, deep links, the data folder) and runs frames
// with RunFrame until it returns false.
namespace helix::engine
{
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

    enum class NetworkReachability : i32
    {
        NotReachable = 0,
        ReachableViaCarrierDataNetwork = 1,
        ReachableViaLocalAreaNetwork = 2,
    };

    enum class LogType : i32
    {
        Error = 0,
        Assert = 1,
        Warning = 2,
        Log = 3,
        Exception = 4,
    };

    class Application
    {
    public:
        // ---------------------------------------------------------------------------------------------- state
        static bool isPlaying();
        static bool isEditor();
        static bool isBatchMode();
        static bool isFocused();
        static bool isMobilePlatform();
        static bool isConsolePlatform();
        static RuntimePlatform platform();
        static bool runInBackground();
        static void SetRunInBackground(bool value);
        // Frames per second to aim for; -1 is the platform's default (as fast as the display allows).
        static i32 targetFrameRate();
        static void SetTargetFrameRate(i32 framesPerSecond);
        static NetworkReachability internetReachability();

        // ---------------------------------------------------------------------------------------------- product
        static StringView companyName();
        static StringView productName();
        static StringView version();
        static StringView identifier();
        // The engine's version.
        static StringView helixVersion();

        // ---------------------------------------------------------------------------------------------- paths
        // The player's data folder (set by the host; next to the executable by default).
        static StringView dataPath();
        static StringView streamingAssetsPath();
        // Per user and product: <LocalLow on Windows, ~/.config elsewhere>/<company>/<product>.
        static StringView persistentDataPath();
        static StringView temporaryCachePath();
        static StringView consoleLogPath();
        static StringView absoluteURL();

        // ---------------------------------------------------------------------------------------------- quitting
        // Quits at the end of the frame: wantsToQuit is asked (any false cancels), then quitting, then every scene
        // unloads (disable and destroy hooks), then unloading.
        static void Quit(i32 exitCode = 0);

        // ---------------------------------------------------------------------------------------------- events
        static Query<>& wantsToQuit();
        static Event<>& quitting();
        static Event<>& unloading();
        static Event<bool>& focusChanged();
        static Event<>& lowMemory();
        static Event<StringView>& deepLinkActivated();
        // Every frame, just before rendering.
        static Event<>& onBeforeRender();
        // Log messages of the main thread; the threaded one gets every thread's (its callbacks must be thread safe).
        static Event<StringView, StringView, LogType>& logMessageReceived();
        static Event<StringView, StringView, LogType>& logMessageReceivedThreaded();

        // ---------------------------------------------------------------------------------------------- host
        // One frame of the player loop and the quit flow; false once the application has quit.
        static bool RunFrame();
        // Whether frames should run now (focused, or running in the background).
        static bool ShouldRun();
        static i32 ExitCode();
        // Waits for the target frame rate (the player loop's FramePacing system).
        static void PaceFrame();
        static void SetFocused(bool focused);
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
