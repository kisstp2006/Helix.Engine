#include <Helix/Engine/Application/Application.h>
#include <Helix/Engine/Asset/AssetLoader.h>
#include <Helix/Engine/Asset/Implementation/AssetAccess.h>
#include <Helix/Engine/Async/Implementation/AsyncAccess.h>
#include <Helix/Engine/GameObject/GameObject.h>
#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Engine/Prefab/Prefab.h>
#include <Helix/Engine/Scene/Implementation/SceneAccess.h>

#include <Helix/Core/Hashing/Md5.h>
#include <Helix/Core/Logging/Log.h>
#include <Helix/Core/Strings/Format.h>
#include <Helix/Jobs/Jobs.h>
#include <Helix/Platform/IO/FileSystem.h>
#include <Helix/Platform/Threading/Thread.h>
#include <Helix/Platform/Time/Time.h>
#include <Helix/Serialization/SerializedFile/SerializedFile.h>

#include <atomic>
#include <cstring>

namespace helix::engine
{
    namespace
    {
        LogChannel AssetLog{"Engine"};

        // ------------------------------------------------------------------------------------------------ paths

        char Lower(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }

        String LowerPath(StringView path)
        {
            String s;
            for (char c : path)
                s.Append(c == '\\' ? '/' : Lower(c));
            return s;
        }

        StringView ExtensionOf(StringView path)
        {
            usize slash = path.FindLast('/');
            usize dot = path.FindLast('.');
            if (dot == StringView::npos || (slash != StringView::npos && dot < slash))
                return {};
            return path.Substr(dot + 1);
        }

        // The part below the last folder named Resources, lower case and without the extension.
        String ResourcePathOf(StringView path)
        {
            String lower = LowerPath(path);
            StringView l = lower;
            usize at = StringView::npos;
            for (usize i = 0; i + 10 <= l.Size(); i++)
                if (l.Substr(i, 10) == "resources/" && (i == 0 || l[i - 1] == '/'))
                    at = i + 10;
            if (at == StringView::npos)
                return String();
            StringView rest = l.Substr(at);
            usize slash = rest.FindLast('/');
            usize dot = rest.FindLast('.');
            if (dot != StringView::npos && (slash == StringView::npos || dot > slash))
                rest = rest.Substr(0, dot);
            return String(rest);
        }

        // ------------------------------------------------------------------------------------------------ state

        enum Stage : u8
        {
            Queued,
            Reading,
            Decoding,
            Decoded,
        };

        struct Waiter
        {
            u32 op;
            u32 klass;
        };

        struct Request
        {
            Guid guid;
            String path;
            AssetSource* source = nullptr;
            AssetLoader loader;
            i32 priority = 0;
            u64 order = 0;
            std::atomic<u8> stage{Queued};
            // Set once the decode job is scheduled (its handle is then valid).
            std::atomic<bool> scheduled{false};
            Array<u8> bytes;
            Array<u8> meta;
            void* decoded = nullptr;
            String error;
            jobs::JobHandle job;
            Array<Waiter> waiters;
        };

        struct ObjectInfo
        {
            struct Record* record;
            i64 fileID;
            // Its data was unloaded (UnloadAssetData); a load of the asset fills it again.
            bool unloaded = false;
        };

        struct Record
        {
            Guid guid;
            i32 type = kAssetTypeImported;
            i64 mainFileID = 0;
            HashMap<i64, EntityId> objects;
            // In the order integrate made them.
            Array<i64> order;
        };

        struct State
        {
            // Catalog.
            Array<AssetSource*> sources;
            Array<AssetEntry> entries;
            HashMap<Guid, u32> byGuid;
            HashMap<String, u32> byPath;
            HashMap<String, u32> byResource;
            // Loaders.
            Array<AssetLoader> loaders;
            bool builtins = false;
            // Loaded assets.
            HashMap<Guid, Record*> records;
            HashMap<EntityId, ObjectInfo> objects;
            Array<Guid> integrating;
            // Loads in flight (main thread), and the queue of the IO thread.
            Array<Request*> requests;
            HashMap<Guid, Request*> inFlight;
            u64 nextOrder = 0;
            Array<u32> unloadOps;
            platform::Mutex mutex;
            platform::ConditionVariable wake;
            Array<Request*> ioQueue;
            platform::Thread io;
            bool ioStarted = false;
            bool stopping = false;

            ~State()
            {
                if (ioStarted)
                {
                    {
                        platform::ScopedLock lock(mutex);
                        stopping = true;
                    }
                    wake.NotifyAll();
                    io.Join();
                }
            }
        };

        State& S()
        {
            static State s;
            return s;
        }

        // ------------------------------------------------------------------------------------------------ loaders

        void EnsureLoaders()
        {
            State& s = S();
            if (s.builtins)
                return;
            s.builtins = true;
            for (AssetLoader l : {TextureAssetLoader(), ModelAssetLoader(), SerializedAssetLoader(), ShaderAssetLoader()})
            {
                bool replaced = false;
                for (const AssetLoader& e : s.loaders)
                    replaced |= e.name == l.name;
                if (!replaced)
                    s.loaders.Push(Move(l));
            }
        }

        u32 LoaderOf(StringView path)
        {
            EnsureLoaders();
            String ext = LowerPath(ExtensionOf(path));
            State& s = S();
            // The last registered loader of an extension wins.
            for (usize i = s.loaders.Size(); i-- > 0;)
                for (const String& e : s.loaders[i].extensions)
                    if (e == ext)
                        return u32(i);
            return kNoAssetLoader;
        }

        void Rebuild()
        {
            State& s = S();
            EnsureLoaders();
            s.entries.Clear();
            s.byGuid = HashMap<Guid, u32>();
            s.byPath = HashMap<String, u32>();
            s.byResource = HashMap<String, u32>();
            Array<AssetEntry> listed;
            for (AssetSource* source : s.sources)
            {
                listed.Clear();
                source->List(listed);
                for (AssetEntry& e : listed)
                {
                    if (!e.guid.IsValid() || s.byGuid.Find(e.guid))
                        continue;
                    e.source = source;
                    e.loader = LoaderOf(e.path);
                    if (e.loader == kNoAssetLoader)
                        continue;
                    e.resourcePath = ResourcePathOf(e.path);
                    u32 index = u32(s.entries.Size());
                    s.byGuid.Set(e.guid, index);
                    String lower = LowerPath(e.path);
                    if (!s.byPath.Find(lower))
                        s.byPath.Set(lower, index);
                    if (!e.resourcePath.Empty() && !s.byResource.Find(e.resourcePath))
                        s.byResource.Set(e.resourcePath, index);
                    s.entries.Push(Move(e));
                }
            }
        }

        // ------------------------------------------------------------------------------------------------ records

        Object* ObjectOf(const Record& r, i64 fileID)
        {
            if (fileID == 0 || fileID == kPrefabAssetFileId)
                fileID = r.mainFileID;
            const EntityId* id = r.objects.Find(fileID);
            if (!id && fileID == kPrefabAssetFileId)
                id = r.objects.Find(r.mainFileID);
            return id ? EntityIdToObject(*id) : nullptr;
        }

        // Loaded: the main object lives and no object waits for its data.
        bool Complete(const Record& r)
        {
            if (!ObjectOf(r, 0))
                return false;
            for (auto e : r.objects)
                if (const ObjectInfo* info = S().objects.Find(e.value); info && info->unloaded && EntityIdToObject(e.value))
                    return false;
            return true;
        }

        void ForgetRecord(Record* r)
        {
            State& s = S();
            for (auto e : r->objects)
                if (const ObjectInfo* info = s.objects.Find(e.value); info && info->record == r)
                    s.objects.Remove(e.value);
            s.records.Remove(r->guid);
            delete r;
        }

        // Copies the data of `from` into `to` (same class) through the binary form; references to the asset's new
        // objects turn into references to the kept ones.
        bool CopyInto(Object& to, Object& from, const HashMap<EntityId, EntityId>& kept)
        {
            const ClassInfo& info = from.Class();
            if (to.ClassIndex() != from.ClassIndex() || !info.writeBinary || !info.readBinary)
                return false;
            serialization::SerializedFileWriter w(false);
            info.writeBinary(from, w, 1, nullptr);
            Array<u8> bytes;
            String error;
            serialization::SerializedFile f;
            if (!w.Finish(bytes, error) || !f.Open(Span<const u8>(bytes.Data(), bytes.Size()), error) || !info.readBinary(to, f, 0, error, nullptr))
            {
                HX_LOG_ERROR(AssetLog, "The data of '{}' could not be loaded again: {}", to.Name(), error);
                return false;
            }
            ReferenceRemap remap;
            remap.map = kept;
            if (info.remap)
                info.remap(to, remap);
            return true;
        }

        // ------------------------------------------------------------------------------------------------ requests

        void Read(Request& r)
        {
            r.stage.store(Reading, std::memory_order_release);
            if (!r.source->Read(r.path, r.bytes, r.error) && r.error.Empty())
                r.error = "the file could not be read";
            // The .meta of a source file is optional.
            String ignored;
            String metaPath(r.path);
            metaPath.Append(".meta");
            if (r.error.Empty() && !r.source->Read(metaPath, r.meta, ignored))
                r.meta.Clear();
        }

        void Decode(Request& r)
        {
            if (r.error.Empty())
            {
                AssetDecodeInput in;
                in.guid = r.guid;
                in.path = r.path;
                in.source = r.source;
                in.bytes = Span<const u8>(r.bytes.Data(), r.bytes.Size());
                in.owner = &r.bytes;
                in.meta = Span<const u8>(r.meta.Data(), r.meta.Size());
                r.decoded = r.loader.decode(in, r.error);
                if (!r.decoded && r.error.Empty())
                    r.error = "the loader made nothing";
            }
            r.bytes = Array<u8>();
            r.meta = Array<u8>();
            r.stage.store(Decoded, std::memory_order_release);
        }

        bool UseJobs() { return jobs::IsInitialized() && jobs::WorkerCount() > 0; }

        void Dispatch(Request& r)
        {
            if (UseJobs())
            {
                r.stage.store(Decoding, std::memory_order_release);
                Request* p = &r;
                r.job = jobs::Schedule([p] { Decode(*p); });
                r.scheduled.store(true, std::memory_order_release);
            }
            else
                Decode(r);
        }

        void IoMain(void*)
        {
            State& s = S();
            for (;;)
            {
                Request* r = nullptr;
                {
                    platform::ScopedLock lock(s.mutex);
                    while (s.ioQueue.Empty() && !s.stopping)
                        s.wake.Wait(s.mutex);
                    if (s.stopping)
                        return;
                    // The highest priority first, then the oldest.
                    usize best = 0;
                    for (usize i = 1; i < s.ioQueue.Size(); i++)
                    {
                        Request* a = s.ioQueue[i];
                        Request* b = s.ioQueue[best];
                        if (a->priority > b->priority || (a->priority == b->priority && a->order < b->order))
                            best = i;
                    }
                    r = s.ioQueue[best];
                    s.ioQueue.RemoveAt(best);
                    // Taken under the lock: a synchronous load sees it is no longer queued.
                    r->stage.store(Reading, std::memory_order_release);
                }
                Read(*r);
                Dispatch(*r);
            }
        }

        // Removes the request from the IO queue when the IO thread has not taken it; true when it was still there.
        bool TakeFromQueue(Request& r)
        {
            State& s = S();
            platform::ScopedLock lock(s.mutex);
            if (r.stage.load(std::memory_order_acquire) != Queued)
                return false;
            for (usize i = 0; i < s.ioQueue.Size(); i++)
                if (s.ioQueue[i] == &r)
                {
                    s.ioQueue.RemoveAt(i);
                    break;
                }
            r.stage.store(Reading, std::memory_order_release);
            return true;
        }

        // Reads and decodes now on this thread, or waits for the IO thread and the job.
        void Await(Request& r)
        {
            if (r.stage.load(std::memory_order_acquire) == Decoded)
                return;
            if (TakeFromQueue(r))
            {
                Read(r);
                Decode(r);
                return;
            }
            for (;;)
            {
                if (r.stage.load(std::memory_order_acquire) == Decoded)
                    return;
                if (r.scheduled.load(std::memory_order_acquire))
                {
                    jobs::Complete(r.job);
                    return;
                }
                platform::YieldThread();
            }
        }

        void Queue(Request& r)
        {
            State& s = S();
            if (!platform::ThreadsSupported())
                return;
            {
                platform::ScopedLock lock(s.mutex);
                s.ioQueue.Push(&r);
                if (!s.ioStarted)
                {
                    platform::ThreadDesc d;
                    d.function = &IoMain;
                    d.name = "helix-asset-io";
                    s.ioStarted = s.io.Start(d);
                }
            }
            s.wake.NotifyOne();
            if (!s.ioStarted)
                TakeFromQueue(r);
        }

        Request* NewRequest(const AssetEntry& e, i32 priority)
        {
            State& s = S();
            Request* r = new Request();
            r->guid = e.guid;
            r->path = e.path;
            r->source = e.source;
            r->loader = s.loaders[e.loader];
            r->priority = priority;
            r->order = s.nextOrder++;
            s.requests.Push(r);
            s.inFlight.Set(e.guid, r);
            return r;
        }

        void Drop(Request* r)
        {
            State& s = S();
            for (usize i = 0; i < s.requests.Size(); i++)
                if (s.requests[i] == r)
                {
                    s.requests.RemoveAt(i);
                    break;
                }
            s.inFlight.Remove(r->guid);
            if (r->decoded && r->loader.release)
                r->loader.release(r->decoded);
            delete r;
        }

        Object* FirstOfClass(const Record& r, u32 klass)
        {
            Object* main = ObjectOf(r, 0);
            if (klass == kNoClass || (main && IsDerivedFrom(main->ClassIndex(), klass)))
                return main;
            for (i64 f : r.order)
                if (Object* o = ObjectOf(r, f); o && IsDerivedFrom(o->ClassIndex(), klass))
                    return o;
            return nullptr;
        }

        void FinishWaiters(Request& r, const Record* record)
        {
            Array<Waiter> waiters = Move(r.waiters);
            for (const Waiter& w : waiters)
            {
                if (OperationState* op = AsyncAccess::Find(w.op))
                    if (Object* o = record ? FirstOfClass(*record, w.klass) : nullptr)
                        op->result = o->GetEntityId();
                AsyncAccess::Finish(w.op);
            }
        }

        // Makes the objects of a decoded request (main thread).
        void Integrate(Request& r)
        {
            State& s = S();
            if (!r.error.Empty() || !r.decoded)
            {
                HX_LOG_ERROR(AssetLog, "Asset '{}' couldn't be loaded: {}", r.path, r.error);
                FinishWaiters(r, nullptr);
                return;
            }
            AssetObjects out;
            AssetRuntimeAccess::Init(out, r.guid, r.path);
            Array<String> errors;
            s.integrating.Push(r.guid);
            bool ok = r.loader.integrate(r.decoded, out, errors);
            s.integrating.RemoveAt(s.integrating.Size() - 1);
            if (r.loader.release)
                r.loader.release(r.decoded);
            r.decoded = nullptr;
            for (const String& e : errors)
                HX_LOG_ERROR(AssetLog, "Asset '{}': {}", r.path, e);
            if (!ok && out.objects.Empty())
            {
                FinishWaiters(r, nullptr);
                return;
            }
            Record** found = s.records.Find(r.guid);
            Record* record = found ? *found : nullptr;
            if (!record)
            {
                record = new Record();
                record->guid = r.guid;
                s.records.Set(r.guid, record);
            }
            record->type = r.loader.referenceType;
            if (out.mainFileID != 0 || record->mainFileID == 0)
                record->mainFileID = out.mainFileID ? out.mainFileID : out.objects.Empty() ? 0 : out.objects[0].fileID;
            // Objects the record still has keep their identity: unloaded ones take the new data, live ones stay as they
            // are; the new copies go.
            HashMap<EntityId, EntityId> kept;
            Array<EntityId> drop;
            for (const AssetObjects::Entry& e : out.objects)
                if (const EntityId* old = record->objects.Find(e.fileID); old && EntityIdToObject(*old))
                    kept.Set(e.id, *old);
            for (const AssetObjects::Entry& e : out.objects)
            {
                Object* fresh = EntityIdToObject(e.id);
                if (!fresh)
                    continue;
                if (const EntityId* old = kept.Find(e.id))
                {
                    ObjectInfo* info = s.objects.Find(*old);
                    if (info && info->unloaded)
                    {
                        CopyInto(*EntityIdToObject(*old), *fresh, kept);
                        info->unloaded = false;
                    }
                    drop.Push(e.id);
                    continue;
                }
                if (const EntityId* old = record->objects.Find(e.fileID))
                    s.objects.Remove(*old);
                else
                    record->order.Push(e.fileID);
                record->objects.Set(e.fileID, e.id);
                s.objects.Set(e.id, ObjectInfo{record, e.fileID, false});
            }
            for (EntityId id : drop)
                if (Object* o = EntityIdToObject(id))
                    DestroyImmediate(o, true);
            // Model and prefab files: their root is the prefab of the guid.
            if (GameObject* g = EntityIdToObject<GameObject>(record->objects.Find(record->mainFileID) ? *record->objects.Find(record->mainFileID) : EntityId()))
                if (FindPrefab(r.guid) != g)
                    RegisterPrefab(r.guid, g);
            FinishWaiters(r, record);
        }

        Request* StartLoad(const Guid& guid, i32 priority, bool async)
        {
            State& s = S();
            if (Request** r = s.inFlight.Find(guid))
            {
                // The IO thread reads priorities under the lock.
                platform::ScopedLock lock(s.mutex);
                if (priority > (*r)->priority)
                    (*r)->priority = priority;
                return *r;
            }
            const AssetEntry* e = FindAsset(guid);
            if (!e)
                return nullptr;
            Request* r = NewRequest(*e, priority);
            if (async)
                Queue(*r);
            return r;
        }

        u32 BudgetMilliseconds()
        {
            switch (Application::backgroundLoadingPriority())
            {
            case ThreadPriority::Low:
                return 2;
            case ThreadPriority::BelowNormal:
                return 4;
            case ThreadPriority::Normal:
                return 10;
            case ThreadPriority::High:
            default:
                return 50;
            }
        }

        void PriorityChanged(u32 op, i32 priority)
        {
            State& s = S();
            for (Request* r : s.requests)
                for (const Waiter& w : r->waiters)
                    if (w.op == op && priority > r->priority)
                    {
                        platform::ScopedLock lock(s.mutex);
                        r->priority = priority;
                    }
        }

        void RunUnloadUnused();
    }

    void AssetRuntimeAccess::Init(AssetObjects& out, const Guid& guid, StringView path)
    {
        out.guid_ = guid;
        out.path_ = path;
    }

    void AssetObjects::Add(i64 fileID, Object* object, bool main)
    {
        if (!object)
            return;
        object->SetPersistent(true);
        objects.Push({fileID, object->GetEntityId()});
        if (main && mainFileID == 0)
            mainFileID = fileID;
    }

    // ---------------------------------------------------------------------------------------------- ids

    i64 AssetObjectFileId(i32 classId, StringView name)
    {
        u64 h = CombineHash(MixHash(u64(classId)), Hash64(name.Data(), name.Size()));
        i64 id = i64(h);
        // Never 0, never a main object's.
        if (id == 0 || (id > 0 && id % 100000 == 0))
            id ^= 0x5bd1e995;
        return id;
    }

    Guid PathGuid(StringView path)
    {
        String lower = LowerPath(path);
        Md5Digest d = ComputeMd5(lower.Data(), lower.Size());
        Guid g;
        std::memcpy(g.bytes, &d, sizeof(g.bytes));
        if (!g.IsValid())
            g.bytes[0] = 1;
        return g;
    }

    // ---------------------------------------------------------------------------------------------- loaders

    void RegisterAssetLoader(const AssetLoader& loader)
    {
        EnsureLoaders();
        State& s = S();
        bool replaced = false;
        for (AssetLoader& l : s.loaders)
            if (l.name == loader.name)
                l = loader, replaced = true;
        if (!replaced)
            s.loaders.Push(loader);
        if (!s.sources.Empty())
            Rebuild();
    }

    bool FindAssetLoader(StringView extension, AssetLoader& out)
    {
        String e(extension);
        u32 i = LoaderOf(String(".").Append(e));
        if (i == kNoAssetLoader)
            return false;
        out = S().loaders[i];
        return true;
    }

    // ---------------------------------------------------------------------------------------------- catalog

    void AddAssetSource(AssetSource* source)
    {
        if (!source)
            return;
        State& s = S();
        for (AssetSource* e : s.sources)
            if (e == source)
                return;
        s.sources.Push(source);
        Rebuild();
    }

    void RemoveAssetSource(AssetSource* source)
    {
        State& s = S();
        for (usize i = 0; i < s.sources.Size(); i++)
            if (s.sources[i] == source)
            {
                // Loads from it finish first (they read through it).
                for (Request* r : Array<Request*>(s.requests))
                    if (r->source == source)
                    {
                        Await(*r);
                        Integrate(*r);
                        Drop(r);
                    }
                s.sources.RemoveAt(i);
                Rebuild();
                return;
            }
    }

    void RefreshAssets() { Rebuild(); }

    const AssetEntry* FindAsset(const Guid& guid)
    {
        State& s = S();
        const u32* i = s.byGuid.Find(guid);
        return i ? &s.entries[*i] : nullptr;
    }

    const AssetEntry* FindAssetAtPath(StringView path)
    {
        State& s = S();
        const u32* i = s.byPath.Find(LowerPath(path));
        return i ? &s.entries[*i] : nullptr;
    }

    const AssetEntry* FindResource(StringView resourcePath)
    {
        State& s = S();
        const u32* i = s.byResource.Find(LowerPath(resourcePath));
        return i ? &s.entries[*i] : nullptr;
    }

    void FindResources(StringView folder, Array<const AssetEntry*>& out)
    {
        out.Clear();
        String f = LowerPath(folder);
        while (!f.Empty() && f[f.Size() - 1] == '/')
            f = String(StringView(f).Substr(0, f.Size() - 1));
        for (const AssetEntry& e : S().entries)
        {
            StringView r = e.resourcePath;
            if (r.Empty())
                continue;
            if (f.Empty() || r == StringView(f) || (r.StartsWith(f) && r[f.Size()] == '/'))
                out.Push(&e);
        }
    }

    // ---------------------------------------------------------------------------------------------- loading

    Object* FindLoadedAsset(const Guid& guid, i64 fileID)
    {
        Record** r = S().records.Find(guid);
        return r ? ObjectOf(**r, fileID) : nullptr;
    }

    bool IsAssetLoaded(const Guid& guid)
    {
        Record** r = S().records.Find(guid);
        return r && Complete(**r);
    }

    Object* LoadAsset(const Guid& guid) { return LoadAsset(guid, 0); }

    Object* LoadAsset(const Guid& guid, i64 fileID)
    {
        State& s = S();
        if (Record** r = s.records.Find(guid); r && Complete(**r))
            return ObjectOf(**r, fileID);
        for (const Guid& g : s.integrating)
            if (g == guid)
            {
                // A file that refers back to an asset whose objects are being made.
                HX_LOG_WARNING(AssetLog, "Asset '{}' refers to itself through other assets; the reference reads null.", FindAsset(guid) ? StringView(FindAsset(guid)->path) : StringView());
                return nullptr;
            }
        Request* r = StartLoad(guid, 0, false);
        if (!r)
            return nullptr;
        Await(*r);
        Integrate(*r);
        Drop(r);
        Record** rec = s.records.Find(guid);
        return rec ? ObjectOf(**rec, fileID) : nullptr;
    }

    Object* LoadAssetOfClass(const Guid& guid, u32 klass)
    {
        if (!LoadAsset(guid))
            return nullptr;
        Record** r = S().records.Find(guid);
        return r ? FirstOfClass(**r, klass) : nullptr;
    }

    void LoadAllAssets(const Guid& guid, Array<Object*>& out)
    {
        out.Clear();
        if (!LoadAsset(guid))
            return;
        Record* r = *S().records.Find(guid);
        if (Object* main = ObjectOf(*r, 0))
            out.Push(main);
        for (i64 f : r->order)
            if (f != r->mainFileID)
                if (Object* o = ObjectOf(*r, f))
                    out.Push(o);
    }

    ResourceRequest LoadAssetAsync(const Guid& guid, u32 klass, i32 priority)
    {
        State& s = S();
        ResourceRequest request;
        request.id = AsyncAccess::New(true, priority);
        OperationState* op = AsyncAccess::Find(request.id);
        op->priorityChanged = &PriorityChanged;
        if (Record** r = s.records.Find(guid); r && Complete(**r))
        {
            if (Object* o = FirstOfClass(**r, klass))
                op->result = o->GetEntityId();
            AsyncAccess::Finish(request.id);
            return request;
        }
        Request* r = StartLoad(guid, priority, true);
        if (!r)
        {
            AsyncAccess::Finish(request.id);
            return request;
        }
        r->waiters.Push({request.id, klass});
        return request;
    }

    bool GetAssetReference(const Object* object, Guid& guid, i64& fileID, i32& type)
    {
        if (!object)
            return false;
        const ObjectInfo* info = S().objects.Find(object->GetEntityId());
        if (!info)
            return false;
        guid = info->record->guid;
        fileID = info->fileID;
        type = info->record->type;
        return true;
    }

    void RegisterAssetObject(const Guid& guid, i64 fileID, Object* object, bool main, i32 type)
    {
        if (!object || !guid.IsValid())
            return;
        State& s = S();
        Record** found = s.records.Find(guid);
        Record* r = found ? *found : nullptr;
        if (!r)
        {
            r = new Record();
            r->guid = guid;
            r->type = type;
            s.records.Set(guid, r);
        }
        if (!r->objects.Find(fileID))
            r->order.Push(fileID);
        r->objects.Set(fileID, object->GetEntityId());
        if (main || r->mainFileID == 0)
            r->mainFileID = fileID;
        object->SetPersistent(true);
        s.objects.Set(object->GetEntityId(), ObjectInfo{r, fileID, false});
    }

    bool UnloadAssetData(Object* object)
    {
        if (!object)
            return false;
        ObjectInfo* info = S().objects.Find(object->GetEntityId());
        const ClassInfo& c = object->Class();
        if (!info || !c.unloadData)
        {
            HX_LOG_ERROR(AssetLog, "UnloadAsset may only be used on individual assets loaded from files, not on '{}' ({}).", object->Name(), c.name);
            return false;
        }
        c.unloadData(*object);
        info->unloaded = true;
        return true;
    }

    // ---------------------------------------------------------------------------------------------- unused assets

    AsyncOperation UnloadUnusedAssets()
    {
        AsyncOperation op;
        op.id = AsyncAccess::New();
        S().unloadOps.Push(op.id);
        return op;
    }

    namespace
    {
        void RunUnloadUnused()
        {
            State& s = S();
            Array<Object*> all;
            FindObjectsOfTypeAll(ClassOf<Object>(), all);
            u32 gameObject = ClassOf<GameObject>();
            // Objects made by code outside the scenes, by id (destroying assets below frees objects of `all`).
            Array<EntityId> loose;
            for (Object* o : all)
                if (!o->IsPersistent() && o->ClassIndex() != gameObject && !(o->Class().flags & ClassComponent) &&
                    !Has(o->hideFlags, HideFlags::DontUnloadUnusedAsset) && !s.objects.Find(o->GetEntityId()))
                    loose.Push(o->GetEntityId());
            HashMap<EntityId, u8> reached;
            reached.Reserve(all.Size());
            Array<EntityId> stack;
            auto reach = [&](EntityId id) {
                if (!reached.Find(id))
                {
                    reached.Set(id, u8(1));
                    stack.Push(id);
                }
            };
            for (Object* o : all)
            {
                bool sceneObject = !o->IsPersistent() && (o->ClassIndex() == gameObject || (o->Class().flags & ClassComponent));
                if (sceneObject || Has(o->hideFlags, HideFlags::DontUnloadUnusedAsset))
                    reach(o->GetEntityId());
            }
            HashMap<Record*, u8> liveRecords;
            Array<EntityId> seen;
            while (!stack.Empty())
            {
                EntityId id = stack[stack.Size() - 1];
                stack.RemoveAt(stack.Size() - 1);
                Object* o = EntityIdToObject(id);
                if (!o)
                    continue;
                // Reaching one object of an asset keeps the whole asset.
                if (const ObjectInfo* info = s.objects.Find(id); info && !liveRecords.Find(info->record))
                {
                    liveRecords.Set(info->record, u8(1));
                    for (auto e : info->record->objects)
                        reach(e.value);
                }
                if (!o->Class().remap)
                    continue;
                seen.Clear();
                ReferenceRemap visitor;
                visitor.seen = &seen;
                o->Class().remap(*o, visitor);
                for (EntityId r : seen)
                    reach(r);
            }
            // Assets nothing reaches go whole.
            Array<Record*> dead;
            for (auto e : s.records)
                if (!liveRecords.Find(e.value))
                    dead.Push(e.value);
            for (Record* r : dead)
            {
                Array<EntityId> ids;
                for (auto e : r->objects)
                    ids.Push(e.value);
                if (GameObject* root = EntityIdToObject<GameObject>(r->objects.Find(r->mainFileID) ? *r->objects.Find(r->mainFileID) : EntityId()))
                    UnloadPrefab(root);
                for (EntityId id : ids)
                    if (Object* o = EntityIdToObject(id))
                        DestroyImmediate(o, true);
                ForgetRecord(r);
            }
            for (EntityId id : loose)
                if (!reached.Find(id))
                    if (Object* o = EntityIdToObject(id))
                        DestroyImmediate(o, true);
        }
    }

    // ---------------------------------------------------------------------------------------------- frames

    void ProcessAssetLoads()
    {
        State& s = S();
        if (!s.unloadOps.Empty())
        {
            Array<u32> ops = Move(s.unloadOps);
            RunUnloadUnused();
            for (u32 op : ops)
                AsyncAccess::Finish(op);
        }
        if (s.requests.Empty())
            return;
        f64 budget = f64(BudgetMilliseconds()) / 1000.0;
        f64 start = platform::Now();
        bool first = true;
        for (;;)
        {
            // The best decoded request, or without an IO thread the best queued one.
            Request* best = nullptr;
            for (Request* r : s.requests)
            {
                u8 stage = r->stage.load(std::memory_order_acquire);
                bool ready = stage == Decoded || (!s.ioStarted && stage == Queued);
                if (!ready || r->waiters.Empty())
                    continue;
                if (!best || r->priority > best->priority || (r->priority == best->priority && r->order < best->order))
                    best = r;
            }
            if (!best || (!first && platform::Now() - start > budget))
                return;
            first = false;
            Await(*best);
            Integrate(*best);
            Drop(best);
        }
    }

    u32 PendingAssetLoads() { return u32(S().requests.Size()); }

    void ResetAssets()
    {
        State& s = S();
        for (Request* r : Array<Request*>(s.requests))
        {
            Await(*r);
            FinishWaiters(*r, nullptr);
            Drop(r);
        }
        for (u32 op : s.unloadOps)
            AsyncAccess::Finish(op);
        s.unloadOps.Clear();
        Array<Record*> records;
        for (auto e : s.records)
            records.Push(e.value);
        for (Record* r : records)
            delete r;
        s.records = HashMap<Guid, Record*>();
        s.objects = HashMap<EntityId, ObjectInfo>();
        s.integrating.Clear();
        s.sources.Clear();
        Rebuild();
    }

    // ---------------------------------------------------------------------------------------------- files

    bool WriteAssetFile(Span<Object* const> objects, Array<u8>& out, String& error, bool binary)
    {
        if (objects.Empty())
            return error = "no objects", false;
        Array<i64> fileIds;
        HashMap<i64, u8> used;
        for (usize i = 0; i < objects.Size(); i++)
        {
            Object* o = objects[i];
            if (!o)
                return error = "a null object", false;
            Guid g;
            i64 f = 0;
            i32 t = 0;
            if (!GetAssetReference(o, g, f, t))
                f = i == 0 ? MainAssetFileId(o->Class().classId) : AssetObjectFileId(o->Class().classId, o->Name());
            while (used.Find(f))
                f = AssetObjectFileId(o->Class().classId, String(o->Name()).Append(Format("#{}", i)));
            used.Set(f, u8(1));
            fileIds.Push(f);
        }
        return SceneAccess::WriteObjects(objects, Span<const i64>(fileIds.Data(), fileIds.Size()), binary, out, error);
    }

    // ---------------------------------------------------------------------------------------------- sources

    namespace
    {
        struct ListContext
        {
            StringView root;
            Array<AssetEntry>* out;
        };

        // The guid line of a .meta file.
        bool MetaGuid(StringView text, Guid& out)
        {
            usize at = text.Find("guid:");
            while (at != StringView::npos)
            {
                if (at == 0 || text[at - 1] == '\n' || text[at - 1] == ' ')
                {
                    StringView rest = text.Substr(at + 5).Trimmed();
                    if (rest.Size() >= 32 && Guid::TryParse(rest.Substr(0, 32), out))
                        return true;
                }
                at = text.Find("guid:", at + 5);
            }
            return false;
        }

        void ListFolder(ListContext& c, StringView relative)
        {
            struct Visit
            {
                ListContext* c;
                String relative;
                Array<String> files;
                Array<String> dirs;
            } v{&c, String(relative), Array<String>(), Array<String>()};
            String full = relative.Empty() ? String(c.root) : platform::JoinPath(c.root, relative);
            platform::ListDirectory(
                full,
                [](const platform::DirectoryEntry& e, void* user) {
                    Visit& v = *static_cast<Visit*>(user);
                    if (e.name.Empty() || e.name[0] == '.')
                        return;
                    String path = v.relative.Empty() ? String(e.name) : String(v.relative).Append('/').Append(e.name);
                    (e.isDirectory ? v.dirs : v.files).Push(Move(path));
                },
                &v);
            for (const String& f : v.files)
            {
                if (StringView(f).EndsWith(".meta"))
                    continue;
                AssetEntry e;
                e.path = f;
                Array<u8> meta;
                Guid g;
                if (platform::ReadFile(platform::JoinPath(c.root, String(f).Append(".meta")), meta) &&
                    MetaGuid(StringView(reinterpret_cast<const char*>(meta.Data()), meta.Size()), g))
                    e.guid = g;
                else
                    e.guid = PathGuid(f);
                c.out->Push(Move(e));
            }
            for (const String& d : v.dirs)
                ListFolder(c, d);
        }
    }

    void FolderAssetSource::List(Array<AssetEntry>& out)
    {
        ListContext c{root_, &out};
        ListFolder(c, {});
    }

    bool FolderAssetSource::Read(StringView path, Array<u8>& out, String& error)
    {
        if (platform::ReadFile(platform::JoinPath(root_, path), out))
            return true;
        error = Format("'{}' could not be read", path);
        return false;
    }

    void PlayerDataSource::List(Array<AssetEntry>& out)
    {
        Array<u8> bytes;
        if (!platform::ReadFile(platform::JoinPath(root_, kAssetIndexFile), bytes))
        {
            HX_LOG_ERROR(AssetLog, "The player data folder '{}' has no {}.", root_, kAssetIndexFile);
            return;
        }
        // One asset per line: the guid, a space, the path.
        StringView text(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());
        usize at = 0;
        while (at < text.Size())
        {
            usize end = text.Find('\n', at);
            if (end == StringView::npos)
                end = text.Size();
            StringView line = text.Substr(at, end - at).Trimmed();
            at = end + 1;
            if (line.Empty() || line[0] == '#')
                continue;
            AssetEntry e;
            if (line.Size() < 34 || !Guid::TryParse(line.Substr(0, 32), e.guid) || line[32] != ' ')
            {
                HX_LOG_ERROR(AssetLog, "{} of '{}': the line '{}' is not a guid and a path.", kAssetIndexFile, root_, line);
                continue;
            }
            e.path = String(line.Substr(33));
            out.Push(Move(e));
        }
    }

    bool PlayerDataSource::Read(StringView path, Array<u8>& out, String& error)
    {
        if (platform::ReadFile(platform::JoinPath(root_, path), out))
            return true;
        error = Format("'{}' could not be read", path);
        return false;
    }

    bool WriteAssetIndex(StringView folder, Span<const AssetEntry> entries, String& error)
    {
        String text("# guid path\n");
        for (const AssetEntry& e : entries)
        {
            char g[33];
            e.guid.ToChars(g);
            text.Append(StringView(g, 32)).Append(' ').Append(e.path).Append('\n');
        }
        if (!platform::WriteFile(platform::JoinPath(folder, kAssetIndexFile), Span<const u8>(reinterpret_cast<const u8*>(text.Data()), text.Size())))
            return error = Format("'{}' could not be written", platform::JoinPath(folder, kAssetIndexFile)), false;
        return true;
    }
}
