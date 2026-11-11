#include <Helix/Engine/Application/Application.h>
#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Engine/PlayerLoop/PlayerLoop.h>
#include <Helix/Engine/Scene/SceneManager.h>
#include <Helix/Engine/Transform/Hierarchy.h>

#include <Helix/Core/Logging/Log.h>
#include <Helix/Core/Strings/Format.h>
#include <Helix/Platform/IO/FileSystem.h>

#include <chrono>
#include <cstdlib>
#include <thread>

namespace helix::engine
{
    namespace
    {
        constexpr StringView kEngineVersion = "0.5.0";

        struct AppState
        {
            bool focused = true;
            bool batchMode = false;
            bool runInBackground = false;
            i32 targetFrameRate = -1;
            NetworkReachability reachability = NetworkReachability::ReachableViaLocalAreaNetwork;
            String company = String("DefaultCompany");
            String product = String("Product");
            String version = String("0.1");
            String identifier = String("com.DefaultCompany.Product");
            String dataPath, streamingAssetsPath, persistentDataPath, temporaryCachePath, consoleLogPath, absoluteURL;
            bool pathsReady = false;
            bool dataPathSet = false;
            bool quitRequested = false;
            bool quit = false;
            i32 exitCode = 0;
            std::chrono::steady_clock::time_point lastFrame;
            bool paced = false;
            std::thread::id mainThread = std::this_thread::get_id();
            bool sink = false;
            Query<> wantsToQuit;
            Event<> quitting, unloading, lowMemory, onBeforeRender;
            Event<bool> focusChanged;
            Event<StringView> deepLinkActivated;
            Event<StringView, StringView, LogType> logMessageReceived, logMessageReceivedThreaded;
        };

        AppState& App()
        {
            static AppState s;
            return s;
        }

        [[maybe_unused]] String Env(const char* name)
        {
            const char* v = std::getenv(name);
            return v ? String(v) : String();
        }

        String Slashes(String s)
        {
            for (usize i = 0; i < s.Size(); i++)
                if (s[i] == '\\')
                    s[i] = '/';
            return s;
        }

        void ComputePaths()
        {
            AppState& a = App();
            if (!a.dataPathSet)
            {
                // Next to the executable: <executable without extension>_Data.
                String exe = Slashes(platform::ExecutablePath());
                StringView e = exe;
                usize dot = e.FindLast('.');
                usize slash = e.FindLast('/');
                if (dot != StringView::npos && (slash == StringView::npos || dot > slash))
                    e = e.Substr(0, dot);
                a.dataPath = Format("{}_Data", e);
            }
            a.streamingAssetsPath = Format("{}/StreamingAssets", a.dataPath);
#if defined(_WIN32)
            String profile = Slashes(Env("USERPROFILE"));
            String temp = Slashes(Env("TEMP"));
            a.persistentDataPath = Format("{}/AppData/LocalLow/{}/{}", profile, a.company, a.product);
            a.temporaryCachePath = Format("{}/{}/{}", temp, a.company, a.product);
#elif defined(__EMSCRIPTEN__)
            a.persistentDataPath = Format("/idbfs/{}/{}", a.company, a.product);
            a.temporaryCachePath = Format("/tmp/{}/{}", a.company, a.product);
#else
            String config = Env("XDG_CONFIG_HOME");
            if (config.Empty())
                config = Format("{}/.config", Env("HOME"));
            a.persistentDataPath = Format("{}/{}/{}", config, a.company, a.product);
            a.temporaryCachePath = Format("/tmp/{}/{}", a.company, a.product);
#endif
            a.consoleLogPath = Format("{}/Player.log", a.persistentDataPath);
            a.pathsReady = true;
        }

        const AppState& Paths()
        {
            if (!App().pathsReady)
                ComputePaths();
            return App();
        }

        void Sink(const LogMessage& m, void*)
        {
            AppState& a = App();
            LogType type = m.level >= LogLevel::Error ? LogType::Error : m.level == LogLevel::Warning ? LogType::Warning : LogType::Log;
            a.logMessageReceivedThreaded.Invoke(m.text, StringView(), type);
            if (std::this_thread::get_id() == a.mainThread)
                a.logMessageReceived.Invoke(m.text, StringView(), type);
        }

        void EnsureSink()
        {
            AppState& a = App();
            if (!a.sink)
                a.sink = AddLogSink(&Sink, nullptr);
        }

        // Every scene and the DontDestroyOnLoad scene unload: their objects' disable and destroy hooks run.
        void UnloadEverything()
        {
            Array<u32> slots;
            for (u32 i = 0; i < SceneManager::sceneCount(); i++)
                slots.Push(SceneManager::GetSceneAt(i).Slot());
            slots.Push(DontDestroyOnLoadScene().Slot());
            for (u32 slot : slots)
            {
                Array<EntityId> roots;
                for (TransformHierarchy* h = FirstRootHierarchy(slot); h; h = h->nextRoot)
                    if (Transform* t = h->transforms[0])
                        if (GameObject* g = t->gameObject())
                            roots.Push(g->GetEntityId());
                for (EntityId id : roots)
                    if (GameObject* g = EntityIdToObject<GameObject>(id))
                        DestroyImmediate(g);
            }
        }
    }

    bool Application::isPlaying() { return true; }
    bool Application::isEditor() { return false; }
    bool Application::isBatchMode() { return App().batchMode; }
    bool Application::isFocused() { return App().focused; }

    RuntimePlatform Application::platform()
    {
#if defined(_WIN32)
        return RuntimePlatform::WindowsPlayer;
#elif defined(__ANDROID__)
        return RuntimePlatform::Android;
#elif defined(__EMSCRIPTEN__)
        return RuntimePlatform::WebGLPlayer;
#elif defined(__APPLE__)
        return RuntimePlatform::OSXPlayer;
#else
        return RuntimePlatform::LinuxPlayer;
#endif
    }

    bool Application::isMobilePlatform() { return platform() == RuntimePlatform::Android || platform() == RuntimePlatform::IPhonePlayer; }
    bool Application::isConsolePlatform() { return false; }
    bool Application::runInBackground() { return App().runInBackground; }
    void Application::SetRunInBackground(bool value) { App().runInBackground = value; }
    i32 Application::targetFrameRate() { return App().targetFrameRate; }
    void Application::SetTargetFrameRate(i32 fps) { App().targetFrameRate = fps > 0 ? fps : -1; }
    NetworkReachability Application::internetReachability() { return App().reachability; }

    StringView Application::companyName() { return App().company; }
    StringView Application::productName() { return App().product; }
    StringView Application::version() { return App().version; }
    StringView Application::identifier() { return App().identifier; }
    StringView Application::engineVersion() { return kEngineVersion; }

    StringView Application::dataPath() { return Paths().dataPath; }
    StringView Application::streamingAssetsPath() { return Paths().streamingAssetsPath; }
    StringView Application::persistentDataPath() { return Paths().persistentDataPath; }
    StringView Application::temporaryCachePath() { return Paths().temporaryCachePath; }
    StringView Application::consoleLogPath() { return Paths().consoleLogPath; }
    StringView Application::absoluteURL() { return App().absoluteURL; }

    void Application::Quit(i32 exitCode)
    {
        AppState& a = App();
        a.quitRequested = true;
        a.exitCode = exitCode;
    }

    Query<>& Application::wantsToQuit() { return App().wantsToQuit; }
    Event<>& Application::quitting() { return App().quitting; }
    Event<>& Application::unloading() { return App().unloading; }
    Event<bool>& Application::focusChanged() { return App().focusChanged; }
    Event<>& Application::lowMemory() { return App().lowMemory; }
    Event<StringView>& Application::deepLinkActivated() { return App().deepLinkActivated; }
    Event<>& Application::onBeforeRender() { return App().onBeforeRender; }

    Event<StringView, StringView, LogType>& Application::logMessageReceived()
    {
        EnsureSink();
        return App().logMessageReceived;
    }

    Event<StringView, StringView, LogType>& Application::logMessageReceivedThreaded()
    {
        EnsureSink();
        return App().logMessageReceivedThreaded;
    }

    bool Application::RunFrame()
    {
        AppState& a = App();
        if (a.quit)
            return false;
        PlayerLoop::RunFrame();
        if (!a.quitRequested)
            return true;
        a.quitRequested = false;
        if (!a.wantsToQuit.Ask())
            return true;
        a.quitting.Invoke();
        UnloadEverything();
        a.unloading.Invoke();
        a.quit = true;
        return false;
    }

    bool Application::ShouldRun() { return App().focused || App().runInBackground || App().batchMode; }
    i32 Application::ExitCode() { return App().exitCode; }

    void Application::PaceFrame()
    {
        AppState& a = App();
        auto now = std::chrono::steady_clock::now();
        if (a.targetFrameRate > 0 && a.paced)
        {
            auto frame = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / a.targetFrameRate));
            auto until = a.lastFrame + frame;
            // Sleep most of the way, then yield to the deadline (sleeps overshoot by a scheduler tick).
            if (until - now > std::chrono::milliseconds(2))
                std::this_thread::sleep_for(until - now - std::chrono::milliseconds(2));
            while (std::chrono::steady_clock::now() < until)
                std::this_thread::yield();
            // A frame that ran late does not make the next ones short.
            now = std::chrono::steady_clock::now();
            a.lastFrame = now - until > frame ? now : until;
        }
        else
            a.lastFrame = now;
        a.paced = true;
    }

    void Application::SetFocused(bool focused)
    {
        AppState& a = App();
        if (a.focused == focused)
            return;
        a.focused = focused;
        a.focusChanged.Invoke(focused);
    }

    void Application::SetBatchMode(bool batchMode) { App().batchMode = batchMode; }
    void Application::SetInternetReachability(NetworkReachability r) { App().reachability = r; }

    void Application::SetDataPath(StringView path)
    {
        AppState& a = App();
        a.dataPath = path;
        a.dataPathSet = true;
        a.pathsReady = false;
    }

    void Application::SetAbsoluteURL(StringView url) { App().absoluteURL = url; }

    void Application::SetProductInfo(StringView company, StringView product, StringView version, StringView identifier)
    {
        AppState& a = App();
        if (!company.Empty())
            a.company = company;
        if (!product.Empty())
            a.product = product;
        if (!version.Empty())
            a.version = version;
        if (!identifier.Empty())
            a.identifier = identifier;
        a.pathsReady = false;
    }

    void Application::ReportLowMemory() { App().lowMemory.Invoke(); }
    void Application::ReportDeepLink(StringView url) { App().deepLinkActivated.Invoke(url); }

    void Application::Reset()
    {
        AppState& a = App();
        a.focused = true;
        a.batchMode = false;
        a.runInBackground = false;
        a.targetFrameRate = -1;
        a.quitRequested = false;
        a.quit = false;
        a.exitCode = 0;
        a.paced = false;
    }
}
