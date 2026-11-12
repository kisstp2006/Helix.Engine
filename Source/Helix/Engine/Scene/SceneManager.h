#pragma once

#include <Helix/Core/Basics/Span.h>
#include <Helix/Core/Strings/String.h>
#include <Helix/Engine/Async/AsyncOperation.h>
#include <Helix/Engine/Event/Event.h>
#include <Helix/Engine/Object/EntityId.h>
#include <Helix/Engine/Scene/Scene.h>

// SceneManager: the loaded scenes, the active one, loading and unloading. Loads run when the player loop calls
// ProcessSceneLoads (after the update loop); LoadSceneAsync parses on Helix.Jobs and creates the objects on the main
// thread. Scene bytes come from a SceneSource (files by default), names resolve through the build scene list.
namespace helix::engine
{
    // The scene events are engine events.
    template <class... Args>
    using SceneEvent = Event<Args...>;

    // Where the bytes of a scene come from (extensible: packed player data, the editor's asset database).
    class SceneSource
    {
    public:
        virtual ~SceneSource() = default;
        virtual bool Read(StringView path, Array<u8>& out, String& error) = 0;
    };

    // Files on disk, paths relative to `root` (the working directory when empty).
    class FileSceneSource final : public SceneSource
    {
    public:
        explicit FileSceneSource(StringView root = StringView()) : root_(root) {}
        bool Read(StringView path, Array<u8>& out, String& error) override;

    private:
        String root_;
    };

    // The extension of scene files (lower case, without the dot).
    inline constexpr StringView kSceneFileExtension = "helix";

    // Scenes as assets: the bytes from the asset source that lists the path (a player data folder, source files). Makes
    // the asset catalog list scene files.
    class AssetSceneSource final : public SceneSource
    {
    public:
        AssetSceneSource();
        bool Read(StringView path, Array<u8>& out, String& error) override;
    };

    // Scenes held in memory under their paths.
    class MemorySceneSource final : public SceneSource
    {
    public:
        void Set(StringView path, Span<const u8> bytes);
        void Set(StringView path, StringView text) { Set(path, Span<const u8>(reinterpret_cast<const u8*>(text.Data()), text.Size())); }
        bool Read(StringView path, Array<u8>& out, String& error) override;

    private:
        Array<String> paths_;
        Array<Array<u8>> bytes_;
    };

    HX_CLASS(Namespace = "HelixEngine.SceneManagement")
    class SceneManager
    {
    public:
        // Scenes loaded or loading (not the DontDestroyOnLoad scene).
        static u32 sceneCount();
        static u32 loadedSceneCount();
        static u32 sceneCountInBuildSettings();

        static Scene GetActiveScene();
        // False for scenes that are not loaded.
        static bool SetActiveScene(Scene scene);
        static Scene GetSceneAt(u32 index);
        static Scene GetSceneByName(StringView name);
        static Scene GetSceneByPath(StringView path);
        static Scene GetSceneByBuildIndex(i32 buildIndex);
        // A loaded scene that came from the id (the first when it was loaded more than once); invalid when none.
        static Scene GetSceneByLoadableSceneId(LoadableSceneId loadableSceneId);
        // An empty loaded scene; invalid when a loaded scene has the name.
        static Scene CreateScene(StringView name, const CreateSceneParameters& parameters = {});

        // In the next ProcessSceneLoads; pending async loads finish first. The scene is returned at once (not loaded).
        static Scene LoadScene(StringView nameOrPath, const LoadSceneParameters& parameters = {});
        static Scene LoadScene(i32 buildIndex, const LoadSceneParameters& parameters = {});
        static void LoadScene(StringView nameOrPath, LoadSceneMode mode) { LoadScene(nameOrPath, LoadSceneParameters{mode}); }
        static void LoadScene(i32 buildIndex, LoadSceneMode mode) { LoadScene(buildIndex, LoadSceneParameters{mode}); }
        static AsyncOperation LoadSceneAsync(StringView nameOrPath, const LoadSceneParameters& parameters = {});
        static AsyncOperation LoadSceneAsync(i32 buildIndex, const LoadSceneParameters& parameters = {});
        // The scene asset the id names in the asset catalog; each load makes a scene of its own.
        static AsyncOperation LoadSceneAsync(LoadableSceneId loadableSceneId, const LoadSceneParameters& parameters = {});
        static AsyncOperation LoadSceneAsync(StringView nameOrPath, LoadSceneMode mode) { return LoadSceneAsync(nameOrPath, LoadSceneParameters{mode}); }
        static AsyncOperation LoadSceneAsync(i32 buildIndex, LoadSceneMode mode) { return LoadSceneAsync(buildIndex, LoadSceneParameters{mode}); }
        // Destroys the scene's objects and removes it; refused for the last loaded scene (an invalid operation).
        static AsyncOperation UnloadSceneAsync(Scene scene, UnloadSceneOptions options = UnloadSceneOptions::None);
        static AsyncOperation UnloadSceneAsync(StringView nameOrPath, UnloadSceneOptions options = UnloadSceneOptions::None);
        static AsyncOperation UnloadSceneAsync(i32 buildIndex, UnloadSceneOptions options = UnloadSceneOptions::None);

        // Moves the source's roots to the end of the destination's and removes the source.
        static void MergeScenes(Scene source, Scene destination);
        // Root GameObjects only, into a loaded scene.
        static bool MoveGameObjectToScene(GameObject* gameObject, Scene scene);
        static void MoveGameObjectsToScene(Span<const EntityId> ids, Scene scene);

        // The scenes of the build, in order: their paths (names are the file names without extension).
        static void SetBuildScenes(Span<const StringView> paths);
        // Null for files on disk.
        static void SetSceneSource(SceneSource* source);

        // Runs the loads and unloads that are due; the player loop calls it after the update loop.
        static void ProcessSceneLoads();
        // Back to the state at startup: loads dropped, every scene unloaded (the DontDestroyOnLoad scene emptied), one
        // new empty active scene, no build scenes and files as the scene source (a host that runs again, tests).
        static void Reset();

        HX_EVENT(Delegate = "global::HelixEngine.Events.HelixAction<global::HelixEngine.SceneManagement.Scene, global::HelixEngine.SceneManagement.Scene>")
        static Event<Scene, Scene>& activeSceneChanged();
        HX_EVENT(Delegate = "global::HelixEngine.Events.HelixAction<global::HelixEngine.SceneManagement.Scene, global::HelixEngine.SceneManagement.LoadSceneMode>")
        static Event<Scene, LoadSceneMode>& sceneLoaded();
        HX_EVENT(Delegate = "global::HelixEngine.Events.HelixAction<global::HelixEngine.SceneManagement.Scene>")
        static Event<Scene>& sceneUnloaded();

        // The script API's members (the C# signatures over the members above; the operations through SceneNative).
        HX_PROPERTY(Name = "sceneCount", ReadOnly) static i32 SceneCountApi() { return i32(sceneCount()); }
        HX_PROPERTY(Name = "loadedSceneCount", ReadOnly) static i32 LoadedSceneCountApi() { return i32(loadedSceneCount()); }
        HX_PROPERTY(Name = "sceneCountInBuildSettings", ReadOnly) static i32 SceneCountInBuildSettingsApi() { return i32(sceneCountInBuildSettings()); }
        HX_FUNCTION(Name = "GetActiveScene") static Scene GetActiveSceneApi() { return GetActiveScene(); }
        HX_FUNCTION(Name = "SetActiveScene") static bool SetActiveSceneApi(Scene scene) { return SetActiveScene(scene); }
        HX_FUNCTION(Name = "GetSceneAt") static Scene GetSceneAtApi(i32 index) { return index >= 0 ? GetSceneAt(u32(index)) : Scene(); }
        HX_FUNCTION(Name = "GetSceneByName") static Scene GetSceneByNameApi(StringView name) { return GetSceneByName(name); }
        HX_FUNCTION(Name = "GetSceneByPath") static Scene GetSceneByPathApi(StringView scenePath) { return GetSceneByPath(scenePath); }
        HX_FUNCTION(Name = "GetSceneByBuildIndex") static Scene GetSceneByBuildIndexApi(i32 buildIndex) { return GetSceneByBuildIndex(buildIndex); }
        HX_FUNCTION(Name = "CreateScene") static Scene CreateSceneApi(StringView sceneName) { return CreateScene(sceneName); }
        HX_FUNCTION(Name = "CreateScene") static Scene CreateSceneApi(StringView sceneName, CreateSceneParameters parameters) { return CreateScene(sceneName, parameters); }
        HX_FUNCTION(Name = "LoadScene") static void LoadSceneApi(StringView sceneName) { LoadScene(sceneName, LoadSceneMode::Single); }
        HX_FUNCTION(Name = "LoadScene") static void LoadSceneApi(StringView sceneName, LoadSceneMode mode) { LoadScene(sceneName, mode); }
        HX_FUNCTION(Name = "LoadScene") static Scene LoadSceneApi(StringView sceneName, LoadSceneParameters parameters) { return LoadScene(sceneName, parameters); }
        HX_FUNCTION(Name = "LoadScene") static void LoadSceneApi(i32 sceneBuildIndex) { LoadScene(sceneBuildIndex, LoadSceneMode::Single); }
        HX_FUNCTION(Name = "LoadScene") static void LoadSceneApi(i32 sceneBuildIndex, LoadSceneMode mode) { LoadScene(sceneBuildIndex, mode); }
        HX_FUNCTION(Name = "LoadScene") static Scene LoadSceneApi(i32 sceneBuildIndex, LoadSceneParameters parameters) { return LoadScene(sceneBuildIndex, parameters); }
        HX_FUNCTION(Name = "MergeScenes") static void MergeScenesApi(Scene sourceScene, Scene destinationScene) { MergeScenes(sourceScene, destinationScene); }
        HX_FUNCTION(Name = "MoveGameObjectToScene") static void MoveGameObjectToSceneApi(GameObject* go, Scene scene) { MoveGameObjectToScene(go, scene); }
    };

    // The scene DontDestroyOnLoad objects live in.
    Scene DontDestroyOnLoadScene();
    // The scene of a slot (for the engine's own code).
    Scene SceneOfSlot(u32 slot);
}
