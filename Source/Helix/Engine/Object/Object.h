#pragma once

#include <Helix/Core/Basics/Span.h>
#include <Helix/Core/Containers/HashMap.h>
#include <Helix/Core/Strings/String.h>
#include <Helix/Engine/Object/Class.h>
#include <Helix/Engine/Object/EntityId.h>
#include <Helix/Math/Quaternion.h>
#include <Helix/Scripting/Bindings/Interop.h>
#include <Helix/Scripting/Bindings/Tags.h>
#include <Helix/Serialization/SerializedFile/SerializedFile.h>
#include <Helix/Serialization/Transfer/Property.h>
#include <Helix/Serialization/Transfer/Yaml.h>

#include <type_traits>

// Object, the base of everything the engine manages, and the object table: every live object has a slot that its
// EntityId names, and each class keeps a dense list of its objects. The object model runs on the main thread.
namespace helix::engine
{
    HX_ENUM(Namespace = "HelixEngine", Attributes = "[System.Flags]")
    enum class HideFlags : i32
    {
        None = 0,
        HideInHierarchy = 1,
        HideInInspector = 2,
        DontSaveInEditor = 4,
        NotEditable = 8,
        DontSaveInBuild = 16,
        DontUnloadUnusedAsset = 32,
        DontSave = 52,
        HideAndDontSave = 61,
    };

    constexpr HideFlags operator|(HideFlags a, HideFlags b) { return HideFlags(u32(a) | u32(b)); }
    constexpr HideFlags operator&(HideFlags a, HideFlags b) { return HideFlags(u32(a) & u32(b)); }
    constexpr bool Has(HideFlags flags, HideFlags all) { return (u32(flags) & u32(all)) == u32(all); }

    namespace detail
    {
        template <class T>
        Object* Create(void* memory)
        {
            return new (memory) T();
        }
        template <class T>
        Object* Clone(void* memory, const Object& o)
        {
            return new (memory) T(static_cast<const T&>(o));
        }
        template <class T>
        void Remap(Object& o, ReferenceRemap& r);
        template <class T>
        void SetSerialization(ClassInfo& info);

        template <class T, u32 flags>
        u32 Register(StringView name, i32 id)
        {
            ClassInfo info;
            info.classId = id;
            info.name = name;
            info.size = u32(sizeof(T));
            info.alignment = u32(alignof(T));
            if constexpr (!std::is_same_v<typename T::Base, void>)
                info.base = T::Base::StaticClass();
            info.flags = flags | (info.base != kNoClass ? GetClass(info.base).flags & (ClassComponent | ClassBehaviour) : 0u);
            if constexpr ((flags & ClassAbstract) == 0)
            {
                info.create = &Create<T>, info.clone = &Clone<T>;
                SetSerialization<T>(info);
            }
            info.remap = &Remap<T>;
            if constexpr (requires(T& t) { t.OnActivateNative(); })
                info.onActivate = [](Object& o) { static_cast<T&>(o).OnActivateNative(); };
            if constexpr (requires(T& t) { t.OnDeactivateNative(); })
                info.onDeactivate = [](Object& o) { static_cast<T&>(o).OnDeactivateNative(); };
            if constexpr (requires { &T::ResolveYaml; })
                info.resolveYaml = &T::ResolveYaml;
            if constexpr (requires { &T::ResolveBinary; })
                info.resolveBinary = &T::ResolveBinary;
            if constexpr (requires(T& t, const T& o) { t.OnCloneNative(o); })
                info.afterClone = [](Object& c, const Object& o) { static_cast<T&>(c).OnCloneNative(static_cast<const T&>(o)); };
            if constexpr (requires(T& t) { t.OnDestroyNative(); })
                info.onDestroy = [](Object& o) { static_cast<T&>(o).OnDestroyNative(); };
            if constexpr (requires(T& t) { t.OnEnableNative(); })
                info.onEnable = [](Object& o) { static_cast<T&>(o).OnEnableNative(); };
            if constexpr (requires(T& t) { t.OnDisableNative(); })
                info.onDisable = [](Object& o) { static_cast<T&>(o).OnDisableNative(); };
            if constexpr (requires(T& t) { t.OnLayerChangedNative(); })
                info.onLayerChanged = [](Object& o) { static_cast<T&>(o).OnLayerChangedNative(); };
            if constexpr (requires(T& t) { t.OnLoadedNative(); })
                info.onLoaded = [](Object& o) { static_cast<T&>(o).OnLoadedNative(); };
            if constexpr (requires(T& t) { t.UnloadDataNative(); })
                info.unloadData = [](Object& o) { static_cast<T&>(o).UnloadDataNative(); };
            if constexpr (requires(Array<u32>& out) { T::Requires(out); })
                T::Requires(info.required);
            return RegisterClass(Move(info));
        }
    }

// Declares an engine class: its base, its class ID and its flags. Classes register on first use, on the
// main thread (the object model's thread); the index is a plain static, so asking for it costs a load and a compare.
#define HX_ENGINE_CLASS(Type, BaseType, Id, Flags) HX_ENGINE_CLASS_DOCUMENT(Type, BaseType, Id, Flags, #Type)
// The same with the type name its documents have (a class sharing another's class ID shares its name too).
#define HX_ENGINE_CLASS_DOCUMENT(Type, BaseType, Id, Flags, DocumentName)                           \
public:                                                                                             \
    using Base = BaseType;                                                                          \
    [[maybe_unused]] static constexpr ::helix::StringView kTypeName = DocumentName;                 \
    [[maybe_unused]] static constexpr ::helix::i32 kClassId = Id;                                   \
    static ::helix::u32 StaticClass()                                                               \
    {                                                                                               \
        static ::helix::u32 index = ::helix::engine::kNoClass;                                      \
        if (index == ::helix::engine::kNoClass) [[unlikely]]                                        \
            index = ::helix::engine::detail::Register<Type, Flags>(#Type, Id);                      \
        return index;                                                                               \
    }

    class Transform;
    enum class FindObjectsInactive : i32;
    enum class FindObjectsSortMode : i32;

    HX_CLASS(Root)
    class Object
    {
        HX_ENGINE_CLASS(Object, void, ClassId::Object, ClassAbstract)

    public:
        virtual ~Object() = default;

        HX_FUNCTION() EntityId GetEntityId() const { return id_; }
        u32 ClassIndex() const { return classIndex_; }
        const ClassInfo& Class() const { return GetClass(classIndex_); }
        // The class the object was made as (the registry index).
        u32 ClassIndexFast() const { return classIndex_; }
        template <class T>
        bool Is() const
        {
            return IsDerivedFrom(classIndex_, ClassOf<T>());
        }

        // Components forward their name to their GameObject.
        HX_PROPERTY(Name = "name") virtual StringView Name() const { return name_; }
        virtual void SetName(StringView name) { name_ = name; }

        HX_FIELD() HideFlags hideFlags = HideFlags::None;

        // The script API's statics (Lifecycle.h has the engine's functions they call).
        HX_FUNCTION() static Object* Instantiate(Object* original);
        HX_FUNCTION() static Object* Instantiate(Object* original, Transform* parent);
        HX_FUNCTION() static Object* Instantiate(Object* original, Transform* parent, bool instantiateInWorldSpace);
        HX_FUNCTION() static Object* Instantiate(Object* original, Vector3 position, Quaternion rotation);
        HX_FUNCTION() static Object* Instantiate(Object* original, Vector3 position, Quaternion rotation, Transform* parent);
        HX_FUNCTION() static void Destroy(Object* obj);
        HX_FUNCTION() static void Destroy(Object* obj, f32 t);
        HX_FUNCTION() static void DestroyImmediate(Object* obj);
        HX_FUNCTION() static void DestroyImmediate(Object* obj, bool allowDestroyingAssets);
        HX_FUNCTION() static void DontDestroyOnLoad(Object* target);
        HX_FUNCTION(Generic = "T : Object", Lists) static Array<Object*> FindObjectsByType(scripting::TypeId type, FindObjectsSortMode sortMode);
        HX_FUNCTION(Generic = "T : Object", Lists)
        static Array<Object*> FindObjectsByType(scripting::TypeId type, FindObjectsInactive findObjectsInactive, FindObjectsSortMode sortMode);
        HX_FUNCTION(Generic = "T : Object") static Object* FindAnyObjectByType(scripting::TypeId type);
        HX_FUNCTION(Generic = "T : Object") static Object* FindAnyObjectByType(scripting::TypeId type, FindObjectsInactive findObjectsInactive);
        HX_FUNCTION(Generic = "T : Object") static Object* FindFirstObjectByType(scripting::TypeId type);
        HX_FUNCTION(Generic = "T : Object") static Object* FindFirstObjectByType(scripting::TypeId type, FindObjectsInactive findObjectsInactive);

        // Assets (loaded or created as assets) as opposed to objects of scenes.
        bool IsPersistent() const { return persistent_; }
        void SetPersistent(bool persistent) { persistent_ = persistent; }

        template <class V>
        void Transfer(V& v)
        {
            u32 flags = u32(hideFlags);
            v.Field("m_ObjectHideFlags", flags);
            hideFlags = HideFlags(flags);
        }

    protected:
        Object() = default;
        // Fields only: a copy has no identity until the table gives it one.
        Object(const Object& other) : hideFlags(other.hideFlags), name_(other.name_) {}
        Object& operator=(const Object&) = delete;

        String name_;

    private:
        friend struct ObjectAccess;
        friend void DeleteObject(Object*);
        EntityId id_;
        u32 classIndex_ = kNoClass;
        u32 listPosition_ = 0;
        bool persistent_ = false;
        bool destroying_ = false;
        // Made in its class's pool (not by `new`).
        bool pooled_ = false;
        // The GC handle of the object's C# wrapper (0 without one), let go when the object goes.
        u64 wrapper_ = 0;
        friend struct scripting::ObjectHandles<Object>;
    };

    namespace detail
    {
        struct Slot
        {
            Object* object = nullptr;
            u32 generation = 1;
            u32 nextFree = 0;
        };
        // The object table's slots, for the inline lookup below (refreshed whenever the table grows).
        struct ObjectTables
        {
            const Slot* slots = nullptr;
            u32 count = 0;
            // The objects made as each class (by class index), dense.
            const Array<Object*>* perClass = nullptr;
        };
        extern HX_IMPORT_DATA ObjectTables gObjectTables;
    }

    // Calls f(Object*) for every object of the class and its subclasses until it returns false.
    template <class F>
    void ForEachObjectOfClass(u32 klass, F&& f)
    {
        const detail::ClassTables& c = detail::gClassTables;
        if (klass >= c.count)
            return;
        for (u32 o = c.orders[klass], end = o + c.sizes[klass]; o < end; o++)
            for (Object* object : detail::gObjectTables.perClass[c.byOrder[o]])
                if (!f(object))
                    return;
    }

    // One array access and a generation compare; null for None, ids of destroyed objects and out of range slots.
    inline Object* EntityIdToObject(EntityId id)
    {
        const detail::ObjectTables& t = detail::gObjectTables;
        u32 slot = id.Slot();
        if (slot == 0 || slot >= t.count)
            return nullptr;
        const detail::Slot& s = t.slots[slot];
        return s.generation == id.Generation() ? s.object : nullptr;
    }

    // A new object of a registered class, made in the class's pool; null for abstract classes.
    Object* NewObjectOfClass(u32 klass);
    template <class T>
    T* NewObject()
    {
        return static_cast<T*>(NewObjectOfClass(ClassOf<T>()));
    }
    // A copy of the object's fields as a new object of its class (no references remapped: see Instantiate).
    Object* CloneObject(const Object& original);
    // Adds an object made by `new` to the table (as an object of `klass`); returns it.
    Object* AddObject(Object* object, u32 klass);
    // Takes the object out of the table and frees it (no callbacks: see Lifecycle for the destroy rules).
    void DeleteObject(Object* object);
    // Room in the object table for `count` more objects (batches make their objects without regrowing it).
    void ReserveObjects(u32 count);

    template <class T>
    T* EntityIdToObject(EntityId id)
    {
        Object* o = EntityIdToObject(id);
        return o && o->Is<T>() ? static_cast<T*>(o) : nullptr;
    }
    bool EntityIdIsValid(EntityId id);
    // One entry per id, null where nothing matches.
    void EntityIdsToObjectList(Span<const EntityId> ids, Array<Object*>& out);
    // The ids that resolve, in order.
    void EntityIdsToValidArray(Span<const EntityId> ids, Array<EntityId>& out);
    u32 ObjectCount();

    HX_ENUM(Namespace = "HelixEngine")
    enum class FindObjectsInactive : i32
    {
        Exclude,
        Include,
    };

    // The order of FindObjectsByType's results: none (the fastest), or by id.
    HX_ENUM(Namespace = "HelixEngine")
    enum class FindObjectsSortMode : i32
    {
        None,
        InstanceID,
    };

    // Loaded objects of the class or a subclass, unsorted: no assets, no objects with DontSave, objects on inactive
    // GameObjects only with Include.
    void FindObjectsByType(u32 klass, FindObjectsInactive inactive, Array<Object*>& out);
    Object* FindAnyObjectByType(u32 klass, FindObjectsInactive inactive = FindObjectsInactive::Exclude);
    // Every object of the class or a subclass: assets and hidden objects too.
    void FindObjectsOfTypeAll(u32 klass, Array<Object*>& out);

    // What FindObjectsByType returns: no assets, no DontSave, objects on inactive GameObjects only when asked.
    bool IsFindable(const Object* object, bool includeInactive);

    template <class T>
    void FindObjectsByType(Array<T*>& out, FindObjectsInactive inactive = FindObjectsInactive::Exclude)
    {
        out.Clear();
        bool include = inactive == FindObjectsInactive::Include;
        ForEachObjectOfClass(ClassOf<T>(), [&](Object* o) {
            if (IsFindable(o, include))
                out.Push(static_cast<T*>(o));
            return true;
        });
    }
    template <class T>
    T* FindAnyObjectByType(FindObjectsInactive inactive = FindObjectsInactive::Exclude)
    {
        return static_cast<T*>(FindAnyObjectByType(ClassOf<T>(), inactive));
    }

    // Visits the object references of objects: a clone's references to objects of the original turn into references
    // to their clones. Classes name their references with PPtr fields in Transfer.
    // The binary layout of one object of a class whose fields are known at run time.
    struct ObjectLayout
    {
        const serialization::TypeTree* tree;
        Md5Digest hash;
    };

    class ReferenceRemap
    {
    public:
        static constexpr bool kReading = false;
        static constexpr bool kText = false;
        i32 Version() const { return 1; }

        HashMap<EntityId, EntityId> map;
        // Instead of the map: originals to their places (built once per prefab) and the clones' ids at those places.
        const HashMap<EntityId, u32>* index = nullptr;
        const EntityId* ids = nullptr;
        // Instead of changing references: every one visited is added here (reachability).
        Array<EntityId>* seen = nullptr;

        template <class T>
        void Field(StringView, T& value)
        {
            if constexpr (requires { value.Transfer(*this); })
                value.Transfer(*this);
            else if constexpr (!std::is_same_v<T, String> && requires { value.Data(); value.Size(); value.begin(); })
            {
                for (auto& item : value)
                    Field(StringView(), item);
            }
        }

        void Reference(EntityId& id)
        {
            if (seen)
            {
                if (id.IsValid())
                    seen->Push(id);
            }
            else if (index)
            {
                if (const u32* k = index->Find(id))
                    id = ids[*k];
            }
            else if (const EntityId* to = map.Find(id))
                id = *to;
        }

        // A reference as its EntityId value (fields known at run time).
        void ObjectReference(u64& raw)
        {
            EntityId id = EntityId::FromULong(raw);
            Reference(id);
            raw = EntityId::ToULong(id);
        }
    };

    template <class T>
    void detail::Remap(Object& o, ReferenceRemap& r)
    {
        static_cast<T&>(o).Transfer(r);
    }

    template <class T>
    void detail::SetSerialization(ClassInfo& info)
    {
        using namespace serialization;
        info.writeYaml = [](Object& o, yaml::Emitter& e, ReferenceTable* refs) { WriteYaml(e, static_cast<T&>(o), refs); };
        info.readYaml = [](Object& o, const yaml::Tree& t, u32 root, Array<String>& errors, ReferenceTable* refs) {
            return ReadYaml(t, root, static_cast<T&>(o), errors, refs);
        };
        // A class whose fields are known at run time (scripts) gives the type tree and hash of each object.
        info.writeBinary = [](Object& o, SerializedFileWriter& w, i64 fileID, ReferenceTable* refs) {
            T& t = static_cast<T&>(o);
            if constexpr (requires { t.ObjectLayout(); })
            {
                ObjectLayout l = t.ObjectLayout();
                w.Add(fileID, T::kClassId, t, *l.tree, l.hash, refs);
            }
            else
                w.Add(fileID, T::kClassId, t, refs);
        };
        info.readBinary = [](Object& o, const SerializedFile& f, u32 index, String& error, ReferenceTable* refs) {
            T& t = static_cast<T&>(o);
            if constexpr (requires { t.ObjectLayout(); })
            {
                // Its layout can depend on the data (the script its m_Script names).
                if constexpr (requires { t.PrepareBinaryRead(f, index, refs); })
                    t.PrepareBinaryRead(f, index, refs);
                return f.Read(index, t, t.ObjectLayout().hash, error, refs);
            }
            else
                return f.Read(index, t, error, refs);
        };
        info.applyProperty = [](Object& o, StringView path, StringView value, const AssetReference& reference, ReferenceTable* refs, String& error) {
            return ApplyProperty(static_cast<T&>(o), path, value, reference, refs, error);
        };
        info.readProperty = [](Object& o, StringView path, String& text, String& error) { return ReadProperty(static_cast<T&>(o), path, text, error); };
    }

    // A reference to an object: its EntityId, so a reference to a destroyed object reads null.
    template <class T>
    class PPtr
    {
    public:
        static constexpr StringView kTypeName = "PPtr";
        static constexpr bool kFlow = true;

        PPtr() = default;
        PPtr(T* object) : id_(object ? object->GetEntityId() : EntityId()) {}
        explicit PPtr(EntityId id) : id_(id) {}

        EntityId Id() const { return id_; }
        T* Get() const { return EntityIdToObject<T>(id_); }
        T* operator->() const { return Get(); }
        explicit operator bool() const { return Get() != nullptr; }
        bool operator==(const PPtr& o) const { return id_ == o.id_; }

        // The remap takes the id; file visitors write {fileID, guid, type} through their reference table (scene
        // files map fileIDs to objects); others see the raw value.
        template <class V>
        void Transfer(V& v)
        {
            if constexpr (requires { v.Reference(id_); })
                v.Reference(id_);
            else if constexpr (requires(u64& raw) { v.ObjectReference(raw); })
            {
                u64 raw = EntityId::ToULong(id_);
                v.ObjectReference(raw);
                id_ = EntityId::FromULong(raw);
            }
            else
            {
                u64 raw = id_.value;
                v.Field("m_Id", raw);
                id_.value = raw;
            }
        }

    private:
        EntityId id_;
    };
}

// Objects of the engine cross to C# as their EntityId, with their class's type as dynamic type.
template <>
struct helix::scripting::ObjectHandles<helix::engine::Object>
{
    static u64 ToId(const engine::Object* object) { return object->GetEntityId().value; }
    static engine::Object* FromId(u64 id) { return engine::EntityIdToObject(engine::EntityId::FromULong(id)); }
    static TypeId TypeOf(const engine::Object* object) { return object->Class().type; }
    static u64& WrapperOf(engine::Object* object) { return object->wrapper_; }
};
