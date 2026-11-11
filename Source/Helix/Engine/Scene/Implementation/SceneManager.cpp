#include <Helix/Engine/Asset/Asset.h>
#include <Helix/Engine/Async/Implementation/AsyncAccess.h>
#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Engine/Scene/Implementation/SceneAccess.h>
#include <Helix/Engine/Scene/SceneFile.h>
#include <Helix/Engine/Time/Time.h>
#include <Helix/Engine/Transform/Implementation/TransformAccess.h>

#include <Helix/Core/Logging/Log.h>
#include <Helix/Jobs/Jobs.h>
#include <Helix/Platform/IO/FileSystem.h>

namespace helix::engine
{
    namespace
    {
        LogChannel SceneLog{"Engine"};

        constexpr u32 kSlotBits = 10;
        constexpr u32 kMaxSlots = 1u << kSlotBits;
        constexpr u32 kDontDestroyOnLoadSlot = 0;

        enum class SceneState : u8
        {
            Loading,
            Loaded,
            Unloading,
        };

        struct SceneSlot
        {
            u32 generation = 0;
            bool used = false;
            SceneState state = SceneState::Loaded;
            bool dirty = false;
            bool subScene = false;
            i32 buildIndex = -1;
            String name;
            String path;
            HashMap<EntityId, i64> fileIds;
            u64 fileIdCounter = 0;
        };

        struct Request
        {
            u32 op = 0;
            u32 slot = 0;
            bool unload = false;
            bool async = false;
            LoadSceneMode mode = LoadSceneMode::Single;
            Array<u8> bytes;
            const SceneFormat* format = nullptr;
            ParsedScene* parsed = nullptr;
            String error;
            jobs::JobHandle job;
            bool parsing = false;
            // The assets the file refers to, loaded before it builds (async loads).
            Array<Guid> dependencies;
            Array<u32> assetLoads;
            bool requested = false;
        };

        struct State
        {
            Array<SceneSlot> slots;
            // The scenes in the order they were added (the DontDestroyOnLoad scene is not among them).
            Array<u32> order;
            u32 active = 1;
            Array<String> buildScenes;
            // The fileIDs of prefab assets' objects in their files.
            HashMap<EntityId, i64> assetFileIds;
            FileSceneSource files;
            SceneSource* source = nullptr;
            Array<Request*> queue;
            SceneEvent<Scene, Scene> activeSceneChanged;
            SceneEvent<Scene, LoadSceneMode> sceneLoaded;
            SceneEvent<Scene> sceneUnloaded;

            State()
            {
                // Slot 0: the DontDestroyOnLoad scene; slot 1: the scene the engine starts with.
                SceneSlot& ddol = slots.Emplace();
                ddol.used = true;
                ddol.generation = 1;
                ddol.name = "DontDestroyOnLoad";
                SceneSlot& first = slots.Emplace();
                first.used = true;
                first.generation = 1;
                first.name = "Untitled";
                order.Push(1);
            }

            ~State()
            {
                for (Request* r : queue)
                {
                    delete r->parsed;
                    delete r;
                }
            }
        };

        State gState;

        SceneSlot* SlotOf(SceneHandle h)
        {
            u32 slot = h.value & (kMaxSlots - 1);
            u32 generation = h.value >> kSlotBits;
            if (generation == 0 || slot >= gState.slots.Size())
                return nullptr;
            SceneSlot& s = gState.slots[slot];
            return s.used && s.generation == generation ? &s : nullptr;
        }

        Scene MakeScene(u32 slot) { return Scene{SceneHandle{(gState.slots[slot].generation << kSlotBits) | slot}}; }

        bool SameText(StringView a, StringView b)
        {
            if (a.Size() != b.Size())
                return false;
            for (usize i = 0; i < a.Size(); i++)
            {
                char x = a[i], y = b[i];
                if (x >= 'A' && x <= 'Z')
                    x = char(x - 'A' + 'a');
                if (y >= 'A' && y <= 'Z')
                    y = char(y - 'A' + 'a');
                if (x != y)
                    return false;
            }
            return true;
        }

        // The file name without directories and extension.
        StringView Stem(StringView path)
        {
            usize slash = path.FindLast('/');
            if (slash != StringView::npos)
                path = path.Substr(slash + 1);
            usize dot = path.FindLast('.');
            return dot == StringView::npos ? path : path.Substr(0, dot);
        }

        u32 NewSlot(StringView name, StringView path, i32 buildIndex, SceneState state)
        {
            u32 index = kMaxSlots;
            for (u32 i = 1; i < gState.slots.Size(); i++)
                if (!gState.slots[i].used)
                {
                    index = i;
                    break;
                }
            if (index == kMaxSlots)
            {
                // The last slot is the assets'.
                if (gState.slots.Size() == kAssetSlot)
                    return kMaxSlots;
                index = u32(gState.slots.Size());
                gState.slots.Emplace();
            }
            SceneSlot& s = gState.slots[index];
            s.used = true;
            s.generation = (s.generation + 1) & ((1u << (32 - kSlotBits)) - 1);
            if (s.generation == 0)
                s.generation = 1;
            s.state = state;
            s.dirty = false;
            s.subScene = false;
            s.buildIndex = buildIndex;
            s.name = name;
            s.path = path;
            s.fileIds.Clear();
            s.fileIdCounter = 0;
            gState.order.Push(index);
            return index;
        }

        void FreeSlot(u32 slot)
        {
            SceneSlot& s = gState.slots[slot];
            s.used = false;
            s.fileIds.Clear();
            for (usize i = 0; i < gState.order.Size(); i++)
                if (gState.order[i] == slot)
                {
                    gState.order.RemoveAt(i);
                    break;
                }
        }

        u32 LoadedCount()
        {
            u32 n = 0;
            for (u32 slot : gState.order)
                n += gState.slots[slot].state == SceneState::Loaded ? 1 : 0;
            return n;
        }

        void SetActive(u32 slot)
        {
            Scene previous = MakeScene(gState.active);
            bool wasValid = gState.slots[gState.active].used;
            gState.active = slot;
            TransformAccess::NewRootScene() = slot;
            gState.activeSceneChanged.Invoke(wasValid ? previous : Scene(), MakeScene(slot));
        }

        // ------------------------------------------------------------------------------------------------ unloading

        // Destroys the scene's objects, raises sceneUnloaded and frees the slot.
        void UnloadNow(u32 slot, bool pickActive)
        {
            Array<EntityId> roots;
            for (TransformHierarchy* h = FirstRootHierarchy(slot); h; h = h->nextRoot)
                if (Transform* t = h->transforms[0])
                    if (GameObject* g = t->gameObject())
                        roots.Push(g->GetEntityId());
            for (EntityId id : roots)
                if (GameObject* g = EntityIdToObject<GameObject>(id))
                    DestroyImmediate(g);
            Scene scene = MakeScene(slot);
            gState.sceneUnloaded.Invoke(scene);
            FreeSlot(slot);
            if (pickActive && gState.active == slot)
                for (u32 s : gState.order)
                    if (gState.slots[s].state == SceneState::Loaded)
                    {
                        SetActive(s);
                        break;
                    }
        }

        // ------------------------------------------------------------------------------------------------ loading

        bool Resolve(StringView nameOrPath, String& path, i32& buildIndex)
        {
            for (u32 i = 0; i < gState.buildScenes.Size(); i++)
            {
                StringView p = gState.buildScenes[i];
                if (SameText(p, nameOrPath) || SameText(Stem(p), nameOrPath))
                {
                    path = p;
                    buildIndex = i32(i);
                    return true;
                }
            }
            path = nameOrPath;
            buildIndex = -1;
            return !nameOrPath.Empty();
        }

        SceneSource& Source() { return gState.source ? *gState.source : static_cast<SceneSource&>(gState.files); }

        void Parse(Request& r)
        {
            r.parsed = r.format->Parse(Span<const u8>(r.bytes.Data(), r.bytes.Size()), r.error);
            if (r.parsed && r.async)
                r.format->Dependencies(*r.parsed, r.dependencies);
        }

        // Starts the loads of the assets the parsed file needs; true once they are all done.
        bool AssetsReady(Request& r)
        {
            if (!r.requested)
            {
                r.requested = true;
                for (const Guid& g : r.dependencies)
                    if (!IsAssetLoaded(g) && FindAsset(g))
                        r.assetLoads.Push(LoadAssetAsync(g).id);
            }
            for (u32 op : r.assetLoads)
                if (!AsyncOperation{op}.isDone())
                    return false;
            return true;
        }

        Request* Queue(StringView nameOrPath, i32 buildIndex, const LoadSceneParameters& p, bool async)
        {
            String path;
            i32 index = buildIndex;
            if (buildIndex >= 0)
            {
                if (u32(buildIndex) >= gState.buildScenes.Size())
                {
                    HX_LOG_ERROR(SceneLog, "Scene with build index {} couldn't be loaded: the build has {} scenes.", buildIndex, gState.buildScenes.Size());
                    return nullptr;
                }
                path = gState.buildScenes[buildIndex];
            }
            else if (!Resolve(nameOrPath, path, index))
            {
                HX_LOG_ERROR(SceneLog, "A scene to load needs a name or path.");
                return nullptr;
            }
            Request* r = new Request();
            String error;
            if (!Source().Read(path, r->bytes, error))
            {
                HX_LOG_ERROR(SceneLog, "Scene '{}' couldn't be loaded: {}", path, error);
                delete r;
                return nullptr;
            }
            r->format = FindSceneFormat(Span<const u8>(r->bytes.Data(), r->bytes.Size()));
            if (!r->format)
            {
                HX_LOG_ERROR(SceneLog, "Scene '{}' couldn't be loaded: no scene format reads it.", path);
                delete r;
                return nullptr;
            }
            r->slot = NewSlot(Stem(path), path, index, SceneState::Loading);
            if (r->slot == kMaxSlots)
            {
                HX_LOG_ERROR(SceneLog, "Scene '{}' couldn't be loaded: {} scenes are open.", path, kMaxSlots);
                delete r;
                return nullptr;
            }
            r->mode = p.loadSceneMode;
            r->async = async;
            if (async)
            {
                r->op = AsyncAccess::New(true);
                // Parsing off the main thread when the job system has workers (without them jobs run only inside
                // Complete, so the parse happens here).
                if (jobs::IsInitialized() && jobs::WorkerCount() > 0)
                {
                    r->parsing = true;
                    r->job = jobs::Schedule([r] { Parse(*r); });
                }
                else
                    Parse(*r);
            }
            gState.queue.Push(r);
            return r;
        }

        void Integrate(Request& r)
        {
            if (r.parsing)
            {
                jobs::Complete(r.job);
                r.parsing = false;
            }
            if (!r.parsed && r.error.Empty())
                Parse(r);
            SceneSlot& s = gState.slots[r.slot];
            if (!r.parsed)
            {
                HX_LOG_ERROR(SceneLog, "Scene '{}' couldn't be loaded: {}", s.path, r.error);
                FreeSlot(r.slot);
                AsyncAccess::Finish(r.op);
                return;
            }
            if (r.mode == LoadSceneMode::Single)
            {
                Array<u32> others;
                for (u32 slot : gState.order)
                    if (slot != r.slot && gState.slots[slot].state == SceneState::Loaded)
                        others.Push(slot);
                for (u32 slot : others)
                    UnloadNow(slot, false);
                SetActive(r.slot);
                Time::LevelLoaded();
            }
            Array<String> errors;
            if (!SceneAccess::Build(*r.format, *r.parsed, r.slot, errors))
                for (const String& e : errors)
                    HX_LOG_ERROR(SceneLog, "Scene '{}': {}", s.path, e);
            gState.slots[r.slot].state = SceneState::Loaded;
            gState.sceneLoaded.Invoke(MakeScene(r.slot), r.mode);
            AsyncAccess::Finish(r.op);
        }

        void Unload(Request& r)
        {
            UnloadNow(r.slot, true);
            AsyncAccess::Finish(r.op);
        }

        void Delete(Request* r)
        {
            if (r->parsing)
                jobs::Complete(r->job);
            delete r->parsed;
            delete r;
        }

        AsyncOperation QueueUnload(u32 slot)
        {
            SceneSlot& s = gState.slots[slot];
            if (slot == kDontDestroyOnLoadSlot || s.state != SceneState::Loaded)
            {
                HX_LOG_ERROR(SceneLog, "UnloadSceneAsync: '{}' is not a loaded scene.", s.name);
                return {};
            }
            if (LoadedCount() <= 1)
            {
                HX_LOG_ERROR(SceneLog, "Unloading the last loaded scene ('{}') is not supported.", s.name);
                return {};
            }
            s.state = SceneState::Unloading;
            Request* r = new Request();
            r->unload = true;
            r->slot = slot;
            r->op = AsyncAccess::New(true);
            gState.queue.Push(r);
            return AsyncOperation{r->op};
        }
    }

    // ---------------------------------------------------------------------------------------------- SceneAccess

    HashMap<EntityId, i64>* SceneAccess::FileIds(u32 slot)
    {
        if (slot == kAssetSlot)
            return &gState.assetFileIds;
        return slot < gState.slots.Size() && gState.slots[slot].used ? &gState.slots[slot].fileIds : nullptr;
    }

    i64 SceneAccess::NewFileId(u32 slot, const HashMap<i64, EntityId>& used)
    {
        u64& counter = gState.slots[slot].fileIdCounter;
        for (;;)
        {
            // splitmix64 of the scene's counter: spread over the 63 bit range, the same every run.
            u64 z = ++counter * 0x9E3779B97F4A7C15ull + (u64(slot) << 40);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            z ^= z >> 31;
            i64 id = i64(z >> 1);
            if (id != 0 && id != kSceneRootsFileId && !used.Find(id))
                return id;
        }
    }

    // ---------------------------------------------------------------------------------------------- Scene

    bool Scene::IsValid() const { return SlotOf(handle) != nullptr; }

    StringView Scene::name() const
    {
        const SceneSlot* s = SlotOf(handle);
        return s ? StringView(s->name) : StringView();
    }

    StringView Scene::path() const
    {
        const SceneSlot* s = SlotOf(handle);
        return s ? StringView(s->path) : StringView();
    }

    i32 Scene::buildIndex() const
    {
        const SceneSlot* s = SlotOf(handle);
        return s ? s->buildIndex : -1;
    }

    bool Scene::isLoaded() const
    {
        const SceneSlot* s = SlotOf(handle);
        return s && s->state == SceneState::Loaded;
    }

    bool Scene::isDirty() const
    {
        const SceneSlot* s = SlotOf(handle);
        return s && s->dirty;
    }

    bool Scene::isSubScene() const
    {
        const SceneSlot* s = SlotOf(handle);
        return s && s->subScene;
    }

    void Scene::SetIsSubScene(bool value) const
    {
        if (SceneSlot* s = SlotOf(handle))
            s->subScene = value;
    }

    u32 Scene::rootCount() const { return IsValid() ? RootHierarchyCount(Slot()) : 0; }

    void Scene::GetRootGameObjects(Array<GameObject*>& out) const
    {
        out.Clear();
        if (!IsValid())
            return;
        for (TransformHierarchy* h = FirstRootHierarchy(Slot()); h; h = h->nextRoot)
            if (Transform* t = h->transforms[0])
                if (GameObject* g = t->gameObject())
                    out.Push(g);
    }

    Scene DontDestroyOnLoadScene() { return MakeScene(kDontDestroyOnLoadSlot); }

    Scene SceneOfSlot(u32 slot) { return slot < gState.slots.Size() && gState.slots[slot].used ? MakeScene(slot) : Scene(); }

    Scene GameObject::scene() const
    {
        Transform* t = transform();
        return t ? SceneOfSlot(t->Hierarchy().scene) : Scene();
    }

    Scene GameObject::GetScene(EntityId id)
    {
        GameObject* g = EntityIdToObject<GameObject>(id);
        return g ? g->scene() : Scene();
    }

    // ---------------------------------------------------------------------------------------------- sources

    bool FileSceneSource::Read(StringView path, Array<u8>& out, String& error)
    {
        String full = root_.Empty() ? String(path) : Format("{}/{}", root_, path);
        if (platform::ReadFile(full, out))
            return true;
        error = Format("the file {} could not be read", full);
        return false;
    }

    void MemorySceneSource::Set(StringView path, Span<const u8> bytes)
    {
        for (usize i = 0; i < paths_.Size(); i++)
            if (paths_[i] == path)
            {
                bytes_[i].Clear();
                bytes_[i].Append(bytes.Data(), bytes.Size());
                return;
            }
        paths_.Push(String(path));
        Array<u8>& b = bytes_.Emplace();
        b.Append(bytes.Data(), bytes.Size());
    }

    bool MemorySceneSource::Read(StringView path, Array<u8>& out, String& error)
    {
        for (usize i = 0; i < paths_.Size(); i++)
            if (paths_[i] == path)
            {
                out = bytes_[i];
                return true;
            }
        error = Format("no scene {} in memory", path);
        return false;
    }

    // ---------------------------------------------------------------------------------------------- SceneManager

    u32 SceneManager::sceneCount() { return u32(gState.order.Size()); }
    u32 SceneManager::loadedSceneCount() { return LoadedCount(); }
    u32 SceneManager::sceneCountInBuildSettings() { return u32(gState.buildScenes.Size()); }

    Scene SceneManager::GetActiveScene() { return MakeScene(gState.active); }

    bool SceneManager::SetActiveScene(Scene scene)
    {
        SceneSlot* s = SlotOf(scene.handle);
        if (!s || s->state != SceneState::Loaded || scene.Slot() == kDontDestroyOnLoadSlot)
            return false;
        if (scene.Slot() != gState.active)
            SetActive(scene.Slot());
        return true;
    }

    Scene SceneManager::GetSceneAt(u32 index) { return index < gState.order.Size() ? MakeScene(gState.order[index]) : Scene(); }

    Scene SceneManager::GetSceneByName(StringView name)
    {
        for (u32 slot : gState.order)
            if (SameText(gState.slots[slot].name, name))
                return MakeScene(slot);
        return Scene();
    }

    Scene SceneManager::GetSceneByPath(StringView path)
    {
        for (u32 slot : gState.order)
            if (SameText(gState.slots[slot].path, path))
                return MakeScene(slot);
        return Scene();
    }

    Scene SceneManager::GetSceneByBuildIndex(i32 buildIndex)
    {
        for (u32 slot : gState.order)
            if (buildIndex >= 0 && gState.slots[slot].buildIndex == buildIndex)
                return MakeScene(slot);
        return Scene();
    }

    Scene SceneManager::CreateScene(StringView name, const CreateSceneParameters&)
    {
        if (name.Empty() || GetSceneByName(name).IsValid())
        {
            HX_LOG_ERROR(SceneLog, "CreateScene: a scene named '{}' already exists (or the name is empty).", name);
            return Scene();
        }
        u32 slot = NewSlot(name, StringView(), -1, SceneState::Loaded);
        return slot == kMaxSlots ? Scene() : MakeScene(slot);
    }

    Scene SceneManager::LoadScene(StringView nameOrPath, const LoadSceneParameters& parameters)
    {
        Request* r = Queue(nameOrPath, -1, parameters, false);
        return r ? MakeScene(r->slot) : Scene();
    }

    Scene SceneManager::LoadScene(i32 buildIndex, const LoadSceneParameters& parameters)
    {
        Request* r = Queue(StringView(), buildIndex < 0 ? i32(gState.buildScenes.Size()) : buildIndex, parameters, false);
        return r ? MakeScene(r->slot) : Scene();
    }

    AsyncOperation SceneManager::LoadSceneAsync(StringView nameOrPath, const LoadSceneParameters& parameters)
    {
        Request* r = Queue(nameOrPath, -1, parameters, true);
        return r ? AsyncOperation{r->op} : AsyncOperation();
    }

    AsyncOperation SceneManager::LoadSceneAsync(i32 buildIndex, const LoadSceneParameters& parameters)
    {
        Request* r = Queue(StringView(), buildIndex < 0 ? i32(gState.buildScenes.Size()) : buildIndex, parameters, true);
        return r ? AsyncOperation{r->op} : AsyncOperation();
    }

    AsyncOperation SceneManager::UnloadSceneAsync(Scene scene, UnloadSceneOptions)
    {
        if (!SlotOf(scene.handle))
        {
            HX_LOG_ERROR(SceneLog, "UnloadSceneAsync: the scene is not valid.");
            return {};
        }
        return QueueUnload(scene.Slot());
    }

    AsyncOperation SceneManager::UnloadSceneAsync(StringView nameOrPath, UnloadSceneOptions options)
    {
        Scene s = GetSceneByName(nameOrPath);
        if (!s.IsValid())
            s = GetSceneByPath(nameOrPath);
        return UnloadSceneAsync(s, options);
    }

    AsyncOperation SceneManager::UnloadSceneAsync(i32 buildIndex, UnloadSceneOptions options)
    {
        return UnloadSceneAsync(GetSceneByBuildIndex(buildIndex), options);
    }

    void SceneManager::MergeScenes(Scene source, Scene destination)
    {
        SceneSlot* from = SlotOf(source.handle);
        SceneSlot* to = SlotOf(destination.handle);
        if (!from || !to || source == destination || from->state != SceneState::Loaded || to->state != SceneState::Loaded ||
            source.Slot() == kDontDestroyOnLoadSlot)
        {
            HX_LOG_ERROR(SceneLog, "MergeScenes: both scenes must be loaded and different.");
            return;
        }
        while (TransformHierarchy* h = FirstRootHierarchy(source.Slot()))
            TransformAccess::MoveToScene(*h, destination.Slot());
        UnloadNow(source.Slot(), false);
        if (gState.active == source.Slot())
            SetActive(destination.Slot());
    }

    bool SceneManager::MoveGameObjectToScene(GameObject* gameObject, Scene scene)
    {
        SceneSlot* s = SlotOf(scene.handle);
        if (!gameObject || !s || s->state != SceneState::Loaded)
        {
            HX_LOG_ERROR(SceneLog, "MoveGameObjectToScene: the scene must be loaded.");
            return false;
        }
        Transform* t = gameObject->transform();
        if (t->parent())
        {
            HX_LOG_ERROR(SceneLog, "MoveGameObjectToScene: only root GameObjects can be moved ({}).", gameObject->Name());
            return false;
        }
        if (t->Hierarchy().scene != scene.Slot())
            TransformAccess::MoveToScene(t->Hierarchy(), scene.Slot());
        return true;
    }

    void SceneManager::MoveGameObjectsToScene(Span<const EntityId> ids, Scene scene)
    {
        for (EntityId id : ids)
            MoveGameObjectToScene(EntityIdToObject<GameObject>(id), scene);
    }

    void SceneManager::SetBuildScenes(Span<const StringView> paths)
    {
        gState.buildScenes.Clear();
        for (StringView p : paths)
            gState.buildScenes.Push(String(p));
    }

    void SceneManager::SetSceneSource(SceneSource* source) { gState.source = source; }

    void SceneManager::ProcessSceneLoads()
    {
        // A load from LoadScene makes every load before it finish, allowed or not.
        usize force = 0;
        for (usize i = 0; i < gState.queue.Size(); i++)
            if (!gState.queue[i]->async && !gState.queue[i]->unload)
                force = i + 1;
        usize done = 0;
        for (; done < gState.queue.Size(); done++)
        {
            Request& r = *gState.queue[done];
            if (r.unload)
            {
                Unload(r);
                continue;
            }
            if (r.async && done >= force)
            {
                // Loads activate in the order they were asked for: a waiting one holds the ones after it.
                if (r.parsing && !jobs::IsCompleted(r.job))
                    break;
                if (r.parsing)
                {
                    jobs::Complete(r.job);
                    r.parsing = false;
                }
                if (!AssetsReady(r))
                    break;
                OperationState* op = AsyncAccess::Find(r.op);
                if (op)
                    op->progress = 0.9f;
                if (op && !op->allowActivation)
                    break;
            }
            Integrate(r);
        }
        for (usize i = 0; i < done; i++)
            Delete(gState.queue[i]);
        Array<Request*> rest;
        for (usize i = done; i < gState.queue.Size(); i++)
            rest.Push(gState.queue[i]);
        gState.queue = Move(rest);
    }

    SceneEvent<Scene, Scene>& SceneManager::activeSceneChanged() { return gState.activeSceneChanged; }
    SceneEvent<Scene, LoadSceneMode>& SceneManager::sceneLoaded() { return gState.sceneLoaded; }
    SceneEvent<Scene>& SceneManager::sceneUnloaded() { return gState.sceneUnloaded; }
}
