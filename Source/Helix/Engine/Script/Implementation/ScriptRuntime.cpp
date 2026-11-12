#include <Helix/Engine/Script/ScriptRuntime.h>

#include <Helix/Core/Logging/Log.h>
#include <Helix/Engine/Application/Application.h>
#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Engine/Object/Implementation/ObjectAccess.h>
#include <Helix/Engine/Rendering/Renderer.h>
#include <Helix/Platform/IO/FileSystem.h>
#include <Helix/Platform/System/DynamicLibrary.h>
#include <Helix/Scripting/Bindings/Interop.h>
#include <Helix/Scripting/Scripts/NativeScript.h>
#include <Helix/Scripting/Types/TypeBuilder.h>

#include <bit>

namespace helix::engine
{
    ScriptClass* MutableScriptClass(u32 index);

    namespace
    {
        LogChannel gScriptLog{"Scripts"};

        // The functions the C# side of the engine hands over.
        struct ManagedScripts
        {
            i32 (*create)(u64 objectId, u32 scriptIndex, u64* handle) = nullptr;
            void (*runBatch)(u32 message, const u64* handles, const u32* scripts, const u8* live, i32 count) = nullptr;
            void (*runOne)(u64 handle, u32 message, u8 argument) = nullptr;
        };
        ManagedScripts gManaged;

        // The phases with lists: FixedUpdate, Update, LateUpdate.
        constexpr ScriptMessage kPhases[] = {ScriptMessage::FixedUpdate, ScriptMessage::Update, ScriptMessage::LateUpdate};

        struct Phase
        {
            // Sorted: execution order, C++ before C#, creation.
            Array<GameBehaviour*> list;
            // Added while the phase runs (placed after it).
            Array<GameBehaviour*> added;
            bool running = false;
            bool holes = false;
        };

        struct Runtime
        {
            Phase phases[3];
            Array<GameBehaviour*> pendingStart;
            u64 sequence = 0;
            // The C# batch in progress: scripts disabled or destroyed during it are not called.
            const u64* batchHandles = nullptr;
            u8* batchLive = nullptr;
            i32 batchCount = 0;
            Array<u64> handles;
            Array<u32> scripts;
            Array<u8> live;
            bool hooked = false;
            bool warnedNoManaged = false;
        };

        Runtime& RT()
        {
            static Runtime r;
            return r;
        }

        i32 PhaseIndex(ScriptMessage m)
        {
            for (i32 i = 0; i < 3; i++)
                if (kPhases[i] == m)
                    return i;
            return -1;
        }

        u64& Handle(GameBehaviour* b) { return scripting::ObjectHandles<Object>::WrapperOf(b); }
    }

    struct ScriptAccess
    {
        static bool Before(const GameBehaviour* a, const GameBehaviour* b)
        {
            if (a->order_ != b->order_)
                return a->order_ < b->order_;
            ScriptLanguage la = a->script_->language, lb = b->script_->language;
            if (la != lb)
                return la == ScriptLanguage::Cpp;
            return a->sequence_ < b->sequence_;
        }

        static void Insert(Phase& p, GameBehaviour* b)
        {
            if (p.running)
            {
                p.added.Push(b);
                return;
            }
            usize at = p.list.Size();
            while (at > 0 && Before(b, p.list[at - 1]))
                at--;
            p.list.Push(nullptr);
            for (usize i = p.list.Size() - 1; i > at; i--)
                p.list[i] = p.list[i - 1];
            p.list[at] = b;
        }

        static void Remove(Phase& p, GameBehaviour* b)
        {
            for (usize i = 0; i < p.list.Size(); i++)
                if (p.list[i] == b)
                {
                    if (p.running)
                    {
                        p.list[i] = nullptr;
                        p.holes = true;
                    }
                    else
                        p.list.RemoveAt(i);
                    break;
                }
            for (usize i = 0; i < p.added.Size(); i++)
                if (p.added[i] == b)
                {
                    p.added.RemoveAt(i);
                    break;
                }
        }

        static void Finish(Phase& p)
        {
            p.running = false;
            if (p.holes)
            {
                usize w = 0;
                for (usize r = 0; r < p.list.Size(); r++)
                    if (p.list[r])
                        p.list[w++] = p.list[r];
                p.list.Resize(w);
                p.holes = false;
            }
            Array<GameBehaviour*> added = Move(p.added);
            p.added = Array<GameBehaviour*>();
            for (GameBehaviour* b : added)
                Insert(p, b);
        }

        // Out of the lists (disabled or destroyed); a C# batch running does not call it any more.
        static void Unlist(GameBehaviour* b)
        {
            Runtime& rt = RT();
            if (b->listed_)
            {
                b->listed_ = false;
                for (Phase& p : rt.phases)
                    Remove(p, b);
            }
            if (rt.batchLive && Handle(b))
                for (i32 i = 0; i < rt.batchCount; i++)
                    if (rt.batchHandles[i] == Handle(b))
                        rt.batchLive[i] = 0;
        }

        static void List(GameBehaviour* b)
        {
            if (b->listed_ || !b->script_)
                return;
            b->listed_ = true;
            for (i32 i = 0; i < 3; i++)
                if (b->script_->Has(kPhases[i]))
                    Insert(RT().phases[i], b);
        }

        static void ResolveScript(GameBehaviour* b)
        {
            if (!b->script_)
                if (const ScriptClass* s = FindScriptClassOfEngineClass(b->ClassIndex()))
                    SetScript(b, s);
        }

        static void SetScript(GameBehaviour* b, const ScriptClass* s)
        {
            b->script_ = s;
            MonoScript* asset = b->scriptAsset_.Get();
            b->order_ = asset && asset->executionOrder != 0 ? asset->executionOrder : s ? s->defaultOrder : 0;
            EnsureManaged(b);
        }

        // The C# object of a C# script, made once (the native object must be in the table).
        static void EnsureManaged(GameBehaviour* b)
        {
            const ScriptClass* s = b->script_;
            if (!s || s->language != ScriptLanguage::CSharp || Handle(b) || !b->GetEntityId().IsValid())
                return;
            Runtime& rt = RT();
            if (!gManaged.create)
            {
                if (!rt.warnedNoManaged)
                    HX_LOG_ERROR(gScriptLog, "{}: C# scripts need the C# side of the engine (the .NET host is not running)", s->name);
                rt.warnedNoManaged = true;
                return;
            }
            u64 handle = 0;
            if (gManaged.create(b->GetEntityId().value, s->index, &handle) != 0 || !handle)
            {
                HX_LOG_ERROR(gScriptLog, "{}: its C# object could not be made", s->name);
                return;
            }
            // The C# object is the wrapper, held while the native object lives.
            u64& slot = Handle(b);
            if (slot && slot != handle)
                scripting::ReleaseWrapper(slot);
            slot = handle;
        }

        static void Activate(GameBehaviour* b)
        {
            ResolveScript(b);
            EnsureManaged(b);
            if (b->awoken_)
                return;
            b->awoken_ = true;
            CallScriptMessage(b, ScriptMessage::Awake);
        }

        static void Enable(GameBehaviour* b)
        {
            ResolveScript(b);
            if (!b->script_)
                return;
            CallScriptMessage(b, ScriptMessage::OnEnable);
            // OnEnable may have disabled it.
            if (!b->isActiveAndEnabled())
                return;
            if (!b->started_ && !b->pendingStart_)
            {
                b->pendingStart_ = true;
                RT().pendingStart.Push(b);
            }
            List(b);
        }

        static void Disable(GameBehaviour* b)
        {
            Unlist(b);
            CallScriptMessage(b, ScriptMessage::OnDisable);
        }

        // Out of the lists and the pending Starts, no message (also from the destructor).
        static void Forget(GameBehaviour* b)
        {
            Unlist(b);
            Array<GameBehaviour*>& pending = RT().pendingStart;
            for (usize i = 0; i < pending.Size(); i++)
                if (pending[i] == b)
                    pending[i] = nullptr;
            b->pendingStart_ = false;
        }

        static void Destroy(GameBehaviour* b)
        {
            Forget(b);
            if (b->awoken_)
                CallScriptMessage(b, ScriptMessage::OnDestroy);
        }

        static void RunPendingStarts()
        {
            Runtime& rt = RT();
            // Starts may enable more scripts; their Starts run here too.
            while (!rt.pendingStart.Empty())
            {
                Array<GameBehaviour*> now = Move(rt.pendingStart);
                rt.pendingStart = Array<GameBehaviour*>();
                // In execution order.
                for (usize i = 1; i < now.Size(); i++)
                    for (usize k = i; k > 0 && now[k] && (!now[k - 1] || Before(now[k], now[k - 1])); k--)
                    {
                        GameBehaviour* t = now[k];
                        now[k] = now[k - 1];
                        now[k - 1] = t;
                    }
                for (GameBehaviour* b : now)
                {
                    if (!b || !b->pendingStart_)
                        continue;
                    b->pendingStart_ = false;
                    if (!b->isActiveAndEnabled())
                    {
                        // Disabled before its Start: it waits for the next enable.
                        continue;
                    }
                    b->started_ = true;
                    CallScriptMessage(b, ScriptMessage::Start);
                }
            }
        }

        static void Run(ScriptMessage m)
        {
            i32 index = PhaseIndex(m);
            if (index < 0)
                return;
            if (m != ScriptMessage::LateUpdate)
                RunPendingStarts();
            Runtime& rt = RT();
            Phase& p = rt.phases[index];
            p.running = true;
            usize i = 0;
            while (i < p.list.Size())
            {
                GameBehaviour* first = p.list[i];
                if (!first)
                {
                    i++;
                    continue;
                }
                // A run of one order and language.
                usize end = i + 1;
                while (end < p.list.Size() && (!p.list[end] || (p.list[end]->order_ == first->order_ && p.list[end]->script_->language == first->script_->language)))
                    end++;
                if (first->script_->language == ScriptLanguage::Cpp)
                {
                    for (usize k = i; k < end; k++)
                    {
                        GameBehaviour* b = p.list[k];
                        if (b && b->started_)
                            b->script_->call[u32(m)](static_cast<Object*>(b));
                    }
                }
                else if (gManaged.runBatch)
                {
                    rt.handles.Clear();
                    rt.scripts.Clear();
                    rt.live.Clear();
                    for (usize k = i; k < end; k++)
                    {
                        GameBehaviour* b = p.list[k];
                        if (b && b->started_ && Handle(b))
                        {
                            rt.handles.Push(Handle(b));
                            rt.scripts.Push(b->script_->index);
                            rt.live.Push(1);
                        }
                    }
                    if (!rt.handles.Empty())
                    {
                        rt.batchHandles = rt.handles.Data();
                        rt.batchLive = rt.live.Data();
                        rt.batchCount = i32(rt.handles.Size());
                        gManaged.runBatch(u32(m), rt.handles.Data(), rt.scripts.Data(), rt.live.Data(), i32(rt.handles.Size()));
                        rt.batchHandles = nullptr;
                        rt.batchLive = nullptr;
                        rt.batchCount = 0;
                    }
                }
                i = end;
            }
            Finish(p);
        }
    };

    // ---------------------------------------------------------------------------------------------- GameBehaviour

    namespace
    {
        // Messages of the hierarchy, the renderers and the application.
        void ScriptsOf(GameObject* g, ScriptMessage m, bool children)
        {
            if (!g || !g->activeInHierarchy())
                return;
            for (const GameObject::ComponentEntry& e : g->Components())
                if (IsDerivedFrom(e.klass, ClassOf<GameBehaviour>()))
                    if (auto* b = static_cast<GameBehaviour*>(EntityIdToObject(e.component.Id())))
                        if (b->isActiveAndEnabled())
                            CallScriptMessage(b, m);
            if (children)
                if (Transform* t = g->transform())
                    for (u32 i = 0; i < t->childCount(); i++)
                        ScriptsOf(t->GetChild(i)->gameObject(), m, true);
        }

        void HierarchyChanged(Transform* moved, Transform* oldParent, Transform* newParent)
        {
            ScriptsOf(moved->gameObject(), ScriptMessage::OnTransformParentChanged, true);
            if (oldParent)
                ScriptsOf(oldParent->gameObject(), ScriptMessage::OnTransformChildrenChanged, false);
            if (newParent)
                ScriptsOf(newParent->gameObject(), ScriptMessage::OnTransformChildrenChanged, false);
        }

        void VisibilityChanged(Renderer* r, bool visible)
        {
            GameObject* g = r->gameObject();
            if (!g)
                return;
            for (const GameObject::ComponentEntry& e : g->Components())
                if (IsDerivedFrom(e.klass, ClassOf<GameBehaviour>()))
                    if (auto* b = static_cast<GameBehaviour*>(EntityIdToObject(e.component.Id())))
                        CallScriptMessage(b, visible ? ScriptMessage::OnBecameVisible : ScriptMessage::OnBecameInvisible);
        }

        void Hook()
        {
            Runtime& rt = RT();
            if (rt.hooked)
                return;
            rt.hooked = true;
            detail::gHierarchyChanged = &HierarchyChanged;
            detail::gVisibilityChanged = &VisibilityChanged;
            Application::focusChanged().Add([](void*, bool focus) { BroadcastScriptMessage(ScriptMessage::OnApplicationFocus, focus); });
            Application::pauseChanged().Add([](void*, bool paused) { BroadcastScriptMessage(ScriptMessage::OnApplicationPause, paused); });
            Application::quitting().Add([](void*) { BroadcastScriptMessage(ScriptMessage::OnApplicationQuit); });
        }
    }

    namespace
    {
        // The one field ResolveYaml reads of a GameBehaviour's document.
        struct ScriptOnly
        {
            static constexpr StringView kTypeName = "GameBehaviour";
            PPtr<MonoScript> m_Script;
            template <class V>
            void Transfer(V& v)
            {
                v.Field("m_Script", m_Script);
            }
        };
    }

    GameBehaviour::GameBehaviour()
    {
        Hook();
        sequence_ = ++RT().sequence;
    }

    GameBehaviour::GameBehaviour(const GameBehaviour& other)
        : Behaviour(other), scriptAsset_(other.scriptAsset_), script_(other.script_), order_(other.order_)
    {
        sequence_ = ++RT().sequence;
    }

    GameBehaviour::~GameBehaviour()
    {
        if (listed_ || pendingStart_)
            ScriptAccess::Forget(this);
    }

    void GameBehaviour::OnActivateNative() { ScriptAccess::Activate(this); }
    void GameBehaviour::OnEnableNative() { ScriptAccess::Enable(this); }
    void GameBehaviour::OnDisableNative() { ScriptAccess::Disable(this); }
    void GameBehaviour::OnDestroyNative() { ScriptAccess::Destroy(this); }

    void GameBehaviour::AttachScriptAsset()
    {
        if (MonoScript* asset = scriptAsset_.Get())
            if (const ScriptClass* s = asset->GetScriptClass())
            {
                ScriptAccess::SetScript(this, s);
                return;
            }
        ScriptAccess::ResolveScript(this);
    }

    u32 GameBehaviour::ResolveYaml(const serialization::yaml::Tree& tree, u32 root, serialization::ReferenceTable* refs)
    {
        ScriptOnly only;
        Array<String> errors;
        serialization::ReadYaml(tree, root, only, errors, refs);
        if (MonoScript* asset = only.m_Script.Get())
            if (const ScriptClass* s = asset->GetScriptClass())
                if (s->language == ScriptLanguage::Cpp)
                    return s->engineClass;
        return kNoClass;
    }

    MonoScript* MonoScript::FromScriptClass(const ScriptClass& script)
    {
        MonoScript* m = NewObject<MonoScript>();
        m->SetPersistent(true);
        m->SetName(script.name);
        m->className = script.name;
        m->nameSpace = script.nameSpace;
        m->assemblyName = script.assembly;
        return m;
    }

    // ---------------------------------------------------------------------------------------------- the runtime

    namespace
    {
        void Prepare(Component& c, void* user)
        {
            ScriptAccess::SetScript(static_cast<GameBehaviour*>(&c), static_cast<const ScriptClass*>(user));
        }
    }

    GameBehaviour* AddScript(GameObject* gameObject, const ScriptClass& script)
    {
        if (!gameObject || !script.alive)
            return nullptr;
        u32 klass = script.language == ScriptLanguage::Cpp ? script.engineClass : ClassOf<GameBehaviour>();
        return static_cast<GameBehaviour*>(gameObject->AddComponent(klass, &Prepare, const_cast<ScriptClass*>(&script)));
    }

    void RunScriptPhase(ScriptMessage phase) { ScriptAccess::Run(phase); }

    void CallScriptMessage(GameBehaviour* b, ScriptMessage m, bool argument)
    {
        const ScriptClass* s = b ? b->script() : nullptr;
        if (!s || !s->Has(m) || !s->alive)
            return;
        if (s->language == ScriptLanguage::Cpp)
        {
            void* self = static_cast<Object*>(b);
            if (ScriptMessageTakesBool(m))
                s->callBool[u32(m)](self, argument);
            else
                s->call[u32(m)](self);
            return;
        }
        if (gManaged.runOne && Handle(b))
            gManaged.runOne(Handle(b), u32(m), argument ? 1 : 0);
    }

    void BroadcastScriptMessage(ScriptMessage m, bool argument)
    {
        Array<GameBehaviour*> all;
        ForEachObjectOfClass(ClassOf<GameBehaviour>(), [&](Object* o) {
            auto* b = static_cast<GameBehaviour*>(o);
            if (b->isActiveAndEnabled() && b->script() && b->script()->Has(m))
                all.Push(b);
            return true;
        });
        for (usize i = 1; i < all.Size(); i++)
            for (usize k = i; k > 0 && ScriptAccess::Before(all[k], all[k - 1]); k--)
            {
                GameBehaviour* t = all[k];
                all[k] = all[k - 1];
                all[k - 1] = t;
            }
        for (GameBehaviour* b : all)
            CallScriptMessage(b, m, argument);
    }

    u32 ScriptPhaseCount(ScriptMessage phase)
    {
        i32 i = PhaseIndex(phase);
        return i < 0 ? 0 : u32(RT().phases[i].list.Size());
    }

    bool ManagedScriptsReady() { return gManaged.create != nullptr; }

    // ---------------------------------------------------------------------------------------------- C++ script classes

    namespace
    {
        void RetireScript(ScriptClass* s)
        {
            if (!s->alive)
                return;
            Array<Object*> objects;
            ForEachObjectOfClass(s->engineClass, [&](Object* o) {
                objects.Push(o);
                return true;
            });
            for (Object* o : objects)
                DestroyImmediate(o, true);
            s->alive = false;
            if (s->language == ScriptLanguage::Cpp)
                DisableClass(s->engineClass);
        }

        void OnNativeScript(const scripting::NativeScriptClass& n, bool added, void*)
        {
            if (!added)
            {
                for (u32 i = 0; i < ScriptClassCount(); i++)
                    if (ScriptClass* s = MutableScriptClass(i); s && s->type == n.type && s->alive)
                        RetireScript(s);
                return;
            }
            ScriptClass s;
            s.language = ScriptLanguage::Cpp;
            s.assembly = String(n.module);
            s.nameSpace = String(n.nameSpace);
            s.name = String(n.name);
            s.type = n.type;
            s.defaultOrder = n.executionOrder;
            for (const scripting::NativeScriptMethod& m : n.methods)
            {
                ScriptMessage message = ScriptMessageOf(m.name);
                if (message == ScriptMessage::Count)
                    continue;
                bool takesBool = ScriptMessageTakesBool(message);
                if (takesBool ? !m.callBool : !m.call)
                    continue;
                s.messages |= 1u << u32(message);
                s.call[u32(message)] = m.call;
                s.callBool[u32(message)] = m.callBool;
            }
            ScriptClass* registered = MutableScriptClass(RegisterScriptClass(s));
            // Its engine class: GameBehaviour's, made as the C++ class.
            ClassInfo info = GetClass(ClassOf<GameBehaviour>());
            info.name = registered->name;
            info.base = ClassOf<GameBehaviour>();
            info.flags = (info.flags & ~u32(ClassAbstract)) | ClassScript;
            info.size = n.size;
            info.alignment = n.alignment;
            info.create = nullptr;
            info.clone = nullptr;
            info.createScript = n.construct;
            info.cloneScript = n.clone;
            info.type = n.type;
            registered->engineClass = RegisterClass(Move(info));
        }

        struct Module
        {
            String name;
            platform::DynamicLibrary* library = nullptr;
        };

        Array<Module>& Modules()
        {
            static Array<Module> m;
            return m;
        }

        String Ident(StringView name)
        {
            String s;
            for (char c : name)
                s.Append((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ? c : '_');
            return s;
        }
    }

    namespace detail
    {
        // The engine's exports for the C# side and the handler of C++ script classes (the bindings provider runs it).
        void StartScriptRuntime();
    }

    bool LoadGameModule(StringView path, String& error)
    {
        detail::StartScriptRuntime();
        StringView file = platform::GetFileName(path);
        StringView stem = file.Substr(0, file.FindLast('.') == StringView::npos ? file.Size() : file.FindLast('.'));
        if (stem.StartsWith("lib"))
            stem = stem.Substr(3);
        auto* library = new platform::DynamicLibrary();
        if (!library->Open(path))
        {
            delete library;
            error = Format("{} does not load", path);
            return false;
        }
        String symbol = Format("HxBindings_{}_Register", Ident(stem));
        auto reg = std::bit_cast<bool (*)()>(library->Symbol(symbol.CStr()));
        if (!reg || !reg())
        {
            delete library;
            error = Format("{} has no bindings ({} is missing or failed)", path, symbol);
            return false;
        }
        Modules().Push(Module{String(stem), library});
        return true;
    }

    bool RegisterLinkedGameModule(StringView module, String& error)
    {
        detail::StartScriptRuntime();
        String symbol = Format("HxBindings_{}_Register", Ident(module));
        auto reg = std::bit_cast<bool (*)()>(scripting::FindNativeExport(symbol));
        if (!reg || !reg())
        {
            error = Format("the linked module {} has no bindings ({} is not published)", module, symbol);
            return false;
        }
        Modules().Push(Module{String(module), nullptr});
        return true;
    }

    void UnloadGameModule(StringView module)
    {
        Array<Module>& all = Modules();
        for (usize i = 0; i < all.Size(); i++)
            if (all[i].name == module)
            {
                // The module's own Unregister (it can register again), else its classes and types directly.
                String symbol = Format("HxBindings_{}_Unregister", Ident(module));
                const void* unregister = all[i].library ? all[i].library->Symbol(symbol.CStr()) : scripting::FindNativeExport(symbol);
                if (unregister)
                    std::bit_cast<void (*)()>(unregister)();
                else
                {
                    scripting::UnregisterNativeScriptClasses(module);
                    scripting::UnloadModule(scripting::FindModule(module));
                }
                delete all[i].library;
                all.RemoveAt(i);
                return;
            }
    }

    // ---------------------------------------------------------------------------------------------- the C# side

    namespace
    {
        bool SetManaged(void* const* table, u32 count)
        {
            if (count != 3)
                return false;
            gManaged.create = std::bit_cast<decltype(gManaged.create)>(table[0]);
            gManaged.runBatch = std::bit_cast<decltype(gManaged.runBatch)>(table[1]);
            gManaged.runOne = std::bit_cast<decltype(gManaged.runOne)>(table[2]);
            return true;
        }

        // A C# script class: its type in the scripting core (in the module of its assembly, below its base's type) and
        // its script class; the index, ~0 when the type cannot be declared.
        u32 RegisterManagedClass(const char* assembly, i32 an, const char* ns, i32 nn, const char* name, i32 n, u32 baseType, u32 messages, i32 order,
                                 u32* typeOut)
        {
            ScriptClass s;
            s.language = ScriptLanguage::CSharp;
            s.assembly = String(StringView(assembly, usize(an)));
            s.nameSpace = String(StringView(ns, usize(nn)));
            s.name = String(StringView(name, usize(n)));
            scripting::ModuleId module = scripting::FindModule(s.assembly);
            if (!module.IsValid())
                module = scripting::RegisterModule(s.assembly, scripting::ModuleKind::Managed);
            String full(s.nameSpace);
            if (!full.Empty())
                full.Append(".");
            full.Append(s.name);
            scripting::TypeId type = scripting::FindType(full);
            if (!type.IsValid() || scripting::GetType(type)->module != module)
            {
                type = scripting::DeclareType(module, s.nameSpace, s.name, scripting::TypeKind::Class, scripting::TypeId{baseType});
                if (!type.IsValid())
                    return ~0u;
                scripting::TypeBuilder(type).Finish();
            }
            if (const ScriptClass* known = FindScriptClass(type))
            {
                *typeOut = type.value;
                return known->index;
            }
            s.type = type;
            s.messages = messages;
            s.defaultOrder = order;
            s.engineClass = ClassOf<GameBehaviour>();
            *typeOut = type.value;
            return RegisterScriptClass(s);
        }

        void Log(i32 level, const char* text, i32 size)
        {
            LogLevel l = level <= 0 ? LogLevel::Trace : level >= i32(LogLevel::Fatal) ? LogLevel::Fatal : LogLevel(level);
            WriteLog(gScriptLog, l, __FILE__, __LINE__, StringView(text, usize(size)));
        }
    }

    void detail::StartScriptRuntime()
    {
        static bool started = false;
        if (started)
            return;
        started = true;
        scripting::RegisterNativeExport("HxEngineScripts_SetManaged", std::bit_cast<const void*>(&SetManaged));
        scripting::RegisterNativeExport("HxEngineScripts_RegisterClass", std::bit_cast<const void*>(&RegisterManagedClass));
        scripting::RegisterNativeExport("HxEngine_Log", std::bit_cast<const void*>(&Log));
        scripting::SetNativeScriptHandler(&OnNativeScript, nullptr);
    }
}
