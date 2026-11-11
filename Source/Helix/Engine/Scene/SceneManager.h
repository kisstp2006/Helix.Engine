#pragma once

#include <Helix/Core/Basics/Span.h>
#include <Helix/Core/Strings/String.h>
#include <Helix/Engine/Object/EntityId.h>
#include <Helix/Engine/Scene/Scene.h>

// SceneManager: the loaded scenes, the active one, loading and unloading. Loads run when the player loop calls
// ProcessSceneLoads (after the update loop); LoadSceneAsync parses on Helix.Jobs and creates the objects on the main
// thread. Scene bytes come from a SceneSource (files by default), names resolve through the build scene list.
namespace helix::engine
{
    // Callbacks of an event: a function and its user pointer.
    template <class... Args>
    class SceneEvent
    {
    public:
        using Callback = void (*)(void* user, Args... args);
        // An id for Remove.
        u32 Add(Callback callback, void* user = nullptr)
        {
            entries_.Push({callback, user, ++next_});
            return next_;
        }
        void Remove(u32 id)
        {
            for (usize i = 0; i < entries_.Size(); i++)
                if (entries_[i].id == id)
                {
                    entries_.RemoveAt(i);
                    return;
                }
        }
        void Invoke(Args... args) const
        {
            // A copy: callbacks may add or remove callbacks.
            Array<Entry> now = entries_;
            for (const Entry& e : now)
                e.callback(e.user, args...);
        }

    private:
        struct Entry
        {
            Callback callback;
            void* user;
            u32 id;
        };
        Array<Entry> entries_;
        u32 next_ = 0;
    };

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

    // A load or unload in progress. A handle: an operation that finished long ago reads as done.
    struct AsyncOperation
    {
        u32 id = 0;

        bool IsValid() const { return id != 0; }
        bool isDone() const;
        // 0 to 1; a load waiting for allowSceneActivation stays at 0.9.
        f32 progress() const;
        i32 priority() const;
        void SetPriority(i32 value) const;
        bool allowSceneActivation() const;
        void SetAllowSceneActivation(bool value) const;
        // Called when the operation is done; at once when it already is.
        void AddCompleted(void (*callback)(void* user, AsyncOperation op), void* user = nullptr) const;
    };

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
        // An empty loaded scene; invalid when a loaded scene has the name.
        static Scene CreateScene(StringView name, const CreateSceneParameters& parameters = {});

        // In the next ProcessSceneLoads; pending async loads finish first. The scene is returned at once (not loaded).
        static Scene LoadScene(StringView nameOrPath, const LoadSceneParameters& parameters = {});
        static Scene LoadScene(i32 buildIndex, const LoadSceneParameters& parameters = {});
        static void LoadScene(StringView nameOrPath, LoadSceneMode mode) { LoadScene(nameOrPath, LoadSceneParameters{mode}); }
        static void LoadScene(i32 buildIndex, LoadSceneMode mode) { LoadScene(buildIndex, LoadSceneParameters{mode}); }
        static AsyncOperation LoadSceneAsync(StringView nameOrPath, const LoadSceneParameters& parameters = {});
        static AsyncOperation LoadSceneAsync(i32 buildIndex, const LoadSceneParameters& parameters = {});
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

        static SceneEvent<Scene, Scene>& activeSceneChanged();
        static SceneEvent<Scene, LoadSceneMode>& sceneLoaded();
        static SceneEvent<Scene>& sceneUnloaded();
    };

    // The scene DontDestroyOnLoad objects live in.
    Scene DontDestroyOnLoadScene();
    // The scene of a slot (for the engine's own code).
    Scene SceneOfSlot(u32 slot);
}
