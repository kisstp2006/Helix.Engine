#pragma once

#include <Helix/Engine/GameObject/Tags.h>
#include <Helix/Engine/Object/Object.h>
#include <Helix/Engine/Object/SmallArray.h>

// GameObject, Component and Behaviour (Transform is in Transform/Transform.h).
namespace helix::engine
{
    class Component;
    class Transform;
    struct TransformHandle;
    struct Scene;

    // The scene culling mask of objects in the main stage, seen in game and scene views.
    inline constexpr u64 kDefaultSceneCullingMask = 0xE000000000000000ull;

    // The links of an object of a prefab instance to its source, as text files write them: null in objects the loader
    // resolved (players keep no links); binary files leave them out.
    template <class V>
    void TransferPrefabLinks(V& v)
    {
        if constexpr (V::kText)
        {
            serialization::AssetReference link;
            v.Field("m_CorrespondingSourceObject", link);
            v.Field("m_PrefabInstance", link);
            v.Field("m_PrefabAsset", link);
        }
    }

    HX_CLASS()
    class GameObject final : public Object
    {
        HX_ENGINE_CLASS(GameObject, Object, ClassId::GameObject, ClassNone)

    public:
        // A GameObject with its Transform.
        HX_FUNCTION(Constructor) static GameObject* Create(StringView name = "GameObject");
        // A GameObject with its Transform and the components of the given classes, in order.
        static GameObject* Create(StringView name, Span<const u32> components);

        u32 layer() const { return layer_; }
        // Components with a layer hook (renderers) hear of the change.
        void SetLayer(u32 layer);
        TagHandle tag() const { return tag_; }
        StringView TagString() const { return TagName(tag_); }
        // False (tag unchanged) when the tag is not defined.
        bool SetTag(StringView tag);
        bool CompareTag(StringView tag) const;
        bool CompareTag(TagHandle tag) const { return tag_ == tag; }
        u32 staticEditorFlags = 0;
        bool isStatic() const { return staticEditorFlags != 0; }

        HX_PROPERTY(ReadOnly) bool activeSelf() const { return activeSelf_; }
        HX_PROPERTY(ReadOnly) bool activeInHierarchy() const { return activeInHierarchy_; }
        // activeSelf only; components hear about it when activeInHierarchy changes.
        HX_FUNCTION() void SetActive(bool value);

        Transform* transform() const;
        TransformHandle transformHandle() const;
        // The scene of its root.
        Scene scene() const;
        u64 sceneCullingMask() const { return kDefaultSceneCullingMask; }
        static Scene GetScene(EntityId id);

        // AddComponent adds the classes the new one requires first; null for abstract classes, classes that are not
        // components, a second component of a DisallowMultipleComponent class or a second Transform.
        Component* AddComponent(u32 klass);
        // With `prepare` run on the new component before it hears of its GameObject's state (a script's C# object).
        Component* AddComponent(u32 klass, void (*prepare)(Component& component, void* user), void* user);
        template <class T>
        T* AddComponent()
        {
            return static_cast<T*>(AddComponent(ClassOf<T>()));
        }

        // By the type of either language: an engine class or a script class.
        HX_FUNCTION(Generic = "T : Component") Component* AddComponent(scripting::TypeId type);
        HX_FUNCTION(Generic = "T : Component") Component* GetComponent(scripting::TypeId type) const;

        Component* GetComponent(u32 klass) const;
        Component* GetComponent(StringView className) const;
        template <class T>
        T* GetComponent() const
        {
            return static_cast<T*>(GetComponent(ClassOf<T>()));
        }
        template <class T>
        bool TryGetComponent(T*& out) const
        {
            out = GetComponent<T>();
            return out != nullptr;
        }
        void GetComponents(u32 klass, Array<Component*>& out) const;
        u32 GetComponentCount() const { return u32(components_.Size()); }
        Component* GetComponentAtIndex(u32 index) const;
        // -1 when the component is not on this GameObject.
        i32 GetComponentIndex(const Component* component) const;

        // Self first, then the children depth first; inactive GameObjects only with includeInactive.
        Component* GetComponentInChildren(u32 klass, bool includeInactive = false) const;
        void GetComponentsInChildren(u32 klass, bool includeInactive, Array<Component*>& out) const;
        // Self first, then the parents.
        Component* GetComponentInParent(u32 klass, bool includeInactive = false) const;
        void GetComponentsInParent(u32 klass, bool includeInactive, Array<Component*>& out) const;

        // An active GameObject by name, or by path ("a/b/c") from a root.
        static GameObject* Find(StringView nameOrPath);
        static GameObject* FindWithTag(StringView tag);
        static void FindGameObjectsWithTag(StringView tag, Array<GameObject*>& out);
        // Many GameObjects at once.
        static void SetGameObjectsActive(Span<const EntityId> ids, bool value);

        // The script API's members (the C# signatures over the members above).
        HX_PROPERTY(Name = "layer", Setter = "SetLayerApi") i32 LayerApi() const { return i32(layer_); }
        void SetLayerApi(i32 value) { SetLayer(u32(value)); }
        HX_PROPERTY(Name = "tag", Setter = "SetTagApi") StringView TagApi() const { return TagString(); }
        void SetTagApi(StringView value) { SetTag(value); }
        HX_PROPERTY(Name = "isStatic", ReadOnly) bool IsStaticApi() const { return isStatic(); }
        HX_PROPERTY(Name = "transform", ReadOnly) Transform* TransformApi() const { return transform(); }
        HX_FUNCTION(Name = "CompareTag") bool CompareTagApi(StringView tag) const { return CompareTag(tag); }
        HX_FUNCTION(Name = "GetComponent") Component* GetComponentByName(StringView type) const { return GetComponent(type); }
        HX_FUNCTION(Generic = "T : Component", Lists) Array<Component*> GetComponents(scripting::TypeId type) const;
        HX_FUNCTION(Generic = "T : Component") Component* GetComponentInChildren(scripting::TypeId type, bool includeInactive = false) const;
        HX_FUNCTION(Generic = "T : Component", Lists) Array<Component*> GetComponentsInChildren(scripting::TypeId type, bool includeInactive = false) const;
        HX_FUNCTION(Generic = "T : Component") Component* GetComponentInParent(scripting::TypeId type, bool includeInactive = false) const;
        HX_FUNCTION(Generic = "T : Component", Lists) Array<Component*> GetComponentsInParent(scripting::TypeId type, bool includeInactive = false) const;
        HX_FUNCTION(Out = "component", Generic = "T : Component") bool TryGetComponent(scripting::TypeId type, Component*& component) const;
        HX_FUNCTION(Name = "GetComponentAtIndex") Component* GetComponentAtIndexApi(i32 index) const;
        HX_FUNCTION(Name = "GetComponentCount") i32 GetComponentCountApi() const { return i32(GetComponentCount()); }
        HX_FUNCTION(Name = "GetComponentIndex") i32 GetComponentIndexApi(Component* component) const { return GetComponentIndex(component); }
        HX_FUNCTION(Name = "Find") static GameObject* FindApi(StringView name) { return Find(name); }
        HX_FUNCTION(Name = "FindWithTag") static GameObject* FindWithTagApi(StringView tag) { return FindWithTag(tag); }
        HX_FUNCTION(Name = "FindGameObjectWithTag") static GameObject* FindGameObjectWithTagApi(StringView tag) { return FindWithTag(tag); }
        HX_FUNCTION(Name = "FindGameObjectsWithTag") static Array<GameObject*> FindGameObjectsWithTagApi(StringView tag);

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            TransferPrefabLinks(v);
            // Visitors take arrays: the list goes through one and back (the remap changes the references in it).
            Array<ComponentEntry> list;
            list.Reserve(components_.Size());
            for (const ComponentEntry& e : components_)
                list.Push(e);
            v.Field("m_Component", list);
            components_.Clear();
            for (const ComponentEntry& e : list)
                components_.Push(e);
            u32 layer = layer_;
            v.Field("m_Layer", layer);
            layer_ = u8(layer < kLayerCount ? layer : 0);
            v.Field("m_Name", name_);
            String tag = String(TagName(tag_));
            v.Field("m_TagString", tag);
            if constexpr (V::kReading)
                SetTag(tag);
            v.Field("m_StaticEditorFlags", staticEditorFlags);
            u8 active = activeSelf_;
            v.Field("m_IsActive", active);
            activeSelf_ = active != 0;
        }

        // The component list as scene files keep it.
        struct ComponentEntry
        {
            static constexpr StringView kTypeName = "ComponentPair";
            PPtr<Component> component;
            // Not serialized: the class of the component, for lookups without touching it.
            u32 klass = kNoClass;
            template <class V>
            void Transfer(V& v)
            {
                v.Field("component", component);
            }
        };
        const SmallArray<ComponentEntry, 4>& Components() const { return components_; }

    private:
        friend struct GameObjectAccess;
        // Transform first.
        SmallArray<ComponentEntry, 4> components_;
        u8 layer_ = 0;
        TagHandle tag_;
        bool activeSelf_ = true;
        bool activeInHierarchy_ = true;
    };

    HX_CLASS()
    class Component : public Object
    {
        HX_ENGINE_CLASS(Component, Object, ClassId::Component, ClassAbstract | ClassComponent)

    public:
        HX_PROPERTY(ReadOnly) GameObject* gameObject() const { return gameObject_.Get(); }
        Transform* transform() const;
        TransformHandle transformHandle() const;
        StringView Name() const override;
        void SetName(StringView name) override;
        StringView TagString() const;
        bool CompareTag(StringView tag) const;
        i32 GetComponentIndex() const;
        template <class T>
        T* GetComponent() const
        {
            GameObject* g = gameObject();
            return g ? g->GetComponent<T>() : nullptr;
        }

        // The script API's members: as the GameObject's.
        HX_PROPERTY(Name = "transform", ReadOnly) Transform* TransformApi() const { return transform(); }
        HX_PROPERTY(Name = "tag", Setter = "SetTagApi") StringView TagApi() const { return TagString(); }
        void SetTagApi(StringView value);
        HX_FUNCTION(Name = "CompareTag") bool CompareTagApi(StringView tag) const { return CompareTag(tag); }
        HX_FUNCTION(Name = "GetComponentIndex") i32 GetComponentIndexApi() const { return GetComponentIndex(); }
        HX_FUNCTION(Generic = "T : Component") Component* GetComponent(scripting::TypeId type) const;
        HX_FUNCTION(Name = "GetComponent") Component* GetComponentByName(StringView type) const;
        HX_FUNCTION(Generic = "T : Component", Lists) Array<Component*> GetComponents(scripting::TypeId type) const;
        HX_FUNCTION(Generic = "T : Component") Component* GetComponentInChildren(scripting::TypeId type, bool includeInactive = false) const;
        HX_FUNCTION(Generic = "T : Component", Lists) Array<Component*> GetComponentsInChildren(scripting::TypeId type, bool includeInactive = false) const;
        HX_FUNCTION(Generic = "T : Component") Component* GetComponentInParent(scripting::TypeId type, bool includeInactive = false) const;
        HX_FUNCTION(Generic = "T : Component", Lists) Array<Component*> GetComponentsInParent(scripting::TypeId type, bool includeInactive = false) const;
        HX_FUNCTION(Out = "component", Generic = "T : Component") bool TryGetComponent(scripting::TypeId type, Component*& component) const;

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            TransferPrefabLinks(v);
            v.Field("m_GameObject", gameObject_);
        }

    protected:
        Component() = default;
        Component(const Component&) = default;

    private:
        friend struct GameObjectAccess;
        PPtr<GameObject> gameObject_;
    };

    HX_CLASS()
    class Behaviour : public Component
    {
        HX_ENGINE_CLASS(Behaviour, Component, ClassId::Behaviour, ClassAbstract | ClassBehaviour)

    public:
        HX_PROPERTY() bool enabled() const { return enabled_; }
        void SetEnabled(bool value);
        // Enabled, on a GameObject active in the hierarchy, and its enable hook has run.
        HX_PROPERTY(ReadOnly) bool isActiveAndEnabled() const { return enableCalled_; }

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            u8 e = enabled_;
            v.Field("m_Enabled", e);
            enabled_ = e != 0;
        }

    protected:
        Behaviour() = default;
        Behaviour(const Behaviour& o) : Component(o), enabled_(o.enabled_) {}

    private:
        friend struct GameObjectAccess;
        bool enabled_ = true;
        bool enableCalled_ = false;
    };
}

#include <Helix/Engine/Scene/Scene.h>
#include <Helix/Engine/Transform/Transform.h>
