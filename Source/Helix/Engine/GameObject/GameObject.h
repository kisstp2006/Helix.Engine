#pragma once

#include <Helix/Engine/GameObject/Tags.h>
#include <Helix/Engine/Object/Object.h>
#include <Helix/Engine/Object/SmallArray.h>
#include <Helix/Math/Quaternion.h>
#include <Helix/Math/Vector.h>

// GameObject, Component, Behaviour and Transform. The Transform here keeps parent, children and local TRS as plain
// fields; the hierarchy storage of step 2 replaces its insides behind the same interface.
namespace helix::engine
{
    class Component;
    class Transform;

    class GameObject final : public Object
    {
        HX_ENGINE_CLASS(GameObject, Object, ClassId::GameObject, ClassNone)

    public:
        // A GameObject with its Transform.
        static GameObject* Create(StringView name = "GameObject");
        // A GameObject with its Transform and the components of the given classes, in order.
        static GameObject* Create(StringView name, Span<const u32> components);

        u32 layer() const { return layer_; }
        void SetLayer(u32 layer) { layer_ = u8(layer < kLayerCount ? layer : 0); }
        TagHandle tag() const { return tag_; }
        StringView TagString() const { return TagName(tag_); }
        // False (tag unchanged) when the tag is not defined.
        bool SetTag(StringView tag);
        bool CompareTag(StringView tag) const;
        bool CompareTag(TagHandle tag) const { return tag_ == tag; }
        u32 staticEditorFlags = 0;
        bool isStatic() const { return staticEditorFlags != 0; }

        bool activeSelf() const { return activeSelf_; }
        bool activeInHierarchy() const { return activeInHierarchy_; }
        // activeSelf only; components hear about it when activeInHierarchy changes.
        void SetActive(bool value);

        Transform* transform() const;

        // AddComponent adds the classes the new one requires first; null for abstract classes, classes that are not
        // components, a second component of a DisallowMultipleComponent class or a second Transform.
        Component* AddComponent(u32 klass);
        template <class T>
        T* AddComponent()
        {
            return static_cast<T*>(AddComponent(ClassOf<T>()));
        }

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

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("m_Component", components_);
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

    class Component : public Object
    {
        HX_ENGINE_CLASS(Component, Object, ClassId::Component, ClassAbstract | ClassComponent)

    public:
        GameObject* gameObject() const { return gameObject_.Get(); }
        Transform* transform() const;
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

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("m_GameObject", gameObject_);
        }

    protected:
        Component() = default;
        Component(const Component&) = default;

    private:
        friend struct GameObjectAccess;
        PPtr<GameObject> gameObject_;
    };

    class Behaviour : public Component
    {
        HX_ENGINE_CLASS(Behaviour, Component, ClassId::Behaviour, ClassAbstract | ClassBehaviour)

    public:
        bool enabled() const { return enabled_; }
        void SetEnabled(bool value);
        // Enabled, on a GameObject active in the hierarchy, and its enable hook has run.
        bool isActiveAndEnabled() const { return enableCalled_; }

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

    class Transform final : public Component
    {
        HX_ENGINE_CLASS(Transform, Component, ClassId::Transform, ClassDisallowMultiple)

    public:
        Transform* parent() const { return parent_.Get(); }
        u32 childCount() const { return u32(children_.Size()); }
        Transform* GetChild(u32 index) const { return index < children_.Size() ? children_[index].Get() : nullptr; }
        Transform* root() const;
        // Moves the transform under `parent` (null: a root) as its last child; false for cycles.
        bool SetParent(Transform* parent);
        bool IsChildOf(const Transform* parent) const;
        i32 GetSiblingIndex() const;
        // A child by name or path ("a/b").
        Transform* Find(StringView path) const;

        Vector3 localPosition = Vector3(0, 0, 0);
        Quaternion localRotation = Quaternion::identity();
        Vector3 localScale = Vector3(1, 1, 1);

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            Value<4> r{&localRotation.x};
            Value<3> p{&localPosition.x}, s{&localScale.x};
            v.Field("m_LocalRotation", r);
            v.Field("m_LocalPosition", p);
            v.Field("m_LocalScale", s);
            v.Field("m_Children", children_);
            v.Field("m_Father", parent_);
        }

    private:
        friend struct GameObjectAccess;
        // A vector or quaternion as scene files write it: {x: 0, y: 0, z: 0[, w: 1]}.
        template <u32 N>
        struct Value
        {
            static constexpr StringView kTypeName = N == 4 ? StringView("Quaternionf") : StringView("Vector3f");
            static constexpr bool kFlow = true;
            float* f;
            template <class V>
            void Transfer(V& v)
            {
                const char* names[4] = {"x", "y", "z", "w"};
                for (u32 i = 0; i < N; i++)
                    v.Field(names[i], f[i]);
            }
        };
        PPtr<Transform> parent_;
        SmallArray<PPtr<Transform>, 4> children_;
    };
}
