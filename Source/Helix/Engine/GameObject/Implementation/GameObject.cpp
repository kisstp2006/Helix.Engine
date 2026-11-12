#include <Helix/Engine/GameObject/Implementation/GameObjectAccess.h>
#include <Helix/Engine/Object/Implementation/ObjectAccess.h>
#include <Helix/Engine/Object/Implementation/ScriptingTypes.h>
#include <Helix/Engine/Script/ScriptRuntime.h>
#include <Helix/Engine/Transform/Implementation/TransformAccess.h>

#include <Helix/Core/Logging/Log.h>

namespace helix::engine
{
    namespace
    {
        LogChannel EngineLog{"Engine"};

        // klass derives from base: one subtraction on the compact order table.
        struct Range
        {
            const u32* orders;
            u32 begin, size;
            explicit Range(u32 base)
                : orders(detail::gClassTables.orders), begin(orders[base]), size(detail::gClassTables.sizes[base])
            {
            }
            bool Has(u32 klass) const { return orders[klass] - begin < size; }
        };

        GameObject* GameObjectOf(Transform* t) { return t ? t->gameObject() : nullptr; }
    }

    // ------------------------------------------------------------------------------------------------ activation

    void GameObjectAccess::Notify(Component* c)
    {
        GameObject* g = c->gameObject();
        const ClassInfo& info = c->Class();
        // Every time the component is on an active GameObject (scripts awake the first time, enabled or not).
        if (info.onActivate && g && g->activeInHierarchy() && !ObjectAccess::Destroying(c))
            info.onActivate(*c);
        if (info.onDeactivate && g && !g->activeInHierarchy())
            info.onDeactivate(*c);
        if (info.flags & ClassBehaviour)
        {
            Behaviour* b = static_cast<Behaviour*>(c);
            bool run = g && g->activeInHierarchy() && b->enabled() && !ObjectAccess::Destroying(c);
            bool& called = b->enableCalled_;
            if (run == called)
                return;
            called = run;
            if (run && info.onEnable)
                info.onEnable(*c);
            else if (!run && info.onDisable)
                info.onDisable(*c);
            return;
        }
        // Components without an enabled flag take part while their GameObject is active.
        if (g && g->activeInHierarchy() && info.onEnable)
            info.onEnable(*c);
        else if (g && !g->activeInHierarchy() && info.onDisable)
            info.onDisable(*c);
    }

    namespace
    {
        // Brings one GameObject in line with its parent; true if its activeInHierarchy changed.
        bool UpdateOne(GameObject* g)
        {
            Transform* t = g->transform();
            GameObject* parent = t ? GameObjectOf(t->parent()) : nullptr;
            bool now = g->activeSelf() && (!parent || parent->activeInHierarchy());
            bool& current = GameObjectAccess::ActiveInHierarchy(*g);
            if (now == current)
                return false;
            current = now;
            for (const GameObject::ComponentEntry& e : g->Components())
                if (Component* c = e.component.Get())
                    GameObjectAccess::Notify(c);
            return true;
        }
    }

    void GameObjectAccess::UpdateActive(GameObject* g)
    {
        Transform* t = g->transform();
        if (!t)
        {
            UpdateOne(g);
            return;
        }
        // Nothing below changes unless this one does.
        TransformHierarchy& h = t->Hierarchy();
        u32 first = t->NodeIndex(), end = first + h.size[first];
        if (end == first + 1)
        {
            UpdateOne(g);
            return;
        }
        GameObject* parent = GameObjectOf(t->parent());
        if ((g->activeSelf_ && (!parent || parent->activeInHierarchy_)) == g->activeInHierarchy_)
            return;
        // The subtree's GameObjects in node order (parents first), held by id: the hooks may destroy objects.
        SmallArray<EntityId, 16> ids;
        for (u32 j = first; j < end; j++)
            if (Transform* x = h.transforms[j])
                if (GameObject* xg = x->gameObject())
                    ids.Push(xg->GetEntityId());
        u32& activating = TransformAccess::Activating();
        activating++;
        for (EntityId id : ids)
            if (GameObject* x = EntityIdToObject<GameObject>(id))
                UpdateOne(x);
        activating--;
    }

    bool IsFindable(const Object* o, bool includeInactive) { return ObjectAccess::Findable(o, includeInactive); }

    bool ObjectAccess::Findable(const Object* o, bool includeInactive)
    {
        if (o->IsPersistent() || Has(o->hideFlags, HideFlags::DontSave) || o->destroying_)
            return false;
        if (includeInactive)
            return true;
        const ClassInfo& info = o->Class();
        if (info.flags & ClassComponent)
        {
            GameObject* g = static_cast<const Component*>(o)->gameObject();
            return g && g->activeInHierarchy();
        }
        if (o->ClassIndex() == ClassOf<GameObject>())
            return static_cast<const GameObject*>(o)->activeInHierarchy();
        return true;
    }

    // ------------------------------------------------------------------------------------------------ GameObject

    GameObject* GameObject::Create(StringView name)
    {
        GameObject* g = NewObject<GameObject>();
        g->name_ = name;
        g->AddComponent<Transform>();
        return g;
    }

    GameObject* GameObject::Create(StringView name, Span<const u32> components)
    {
        GameObject* g = Create(name);
        for (u32 klass : components)
            g->AddComponent(klass);
        return g;
    }

    bool GameObject::SetTag(StringView tag)
    {
        TagHandle h;
        if (!GetExistingTag(tag, h))
        {
            HX_LOG_ERROR(EngineLog, "Tag: {} is not defined.", tag);
            return false;
        }
        tag_ = h;
        return true;
    }

    bool GameObject::CompareTag(StringView tag) const
    {
        TagHandle h;
        if (!GetExistingTag(tag, h))
        {
            HX_LOG_ERROR(EngineLog, "Tag: {} is not defined.", tag);
            return false;
        }
        return h == tag_;
    }

    void GameObject::SetActive(bool value)
    {
        if (activeSelf_ == value)
            return;
        activeSelf_ = value;
        GameObjectAccess::UpdateActive(this);
    }

    Transform* GameObject::transform() const
    {
        // Transform is always the first component.
        return components_.Empty() ? nullptr : static_cast<Transform*>(components_[0].component.Get());
    }

    Component* GameObject::AddComponent(u32 klass) { return AddComponent(klass, nullptr, nullptr); }

    Component* GameObject::AddComponent(u32 klass, void (*prepare)(Component& component, void* user), void* user)
    {
        if (klass >= ClassCount())
            return nullptr;
        const ClassInfo& info = GetClass(klass);
        if (!(info.flags & ClassComponent) || (!info.create && !info.createScript))
        {
            HX_LOG_ERROR(EngineLog, "AddComponent: {} is not a component that can be added.", info.name);
            return nullptr;
        }
        bool isTransform = IsDerivedFrom(klass, ClassOf<Transform>());
        if (isTransform != components_.Empty())
        {
            HX_LOG_ERROR(EngineLog, "AddComponent: a GameObject has exactly one Transform, first ({}).", info.name);
            return nullptr;
        }
        if (info.flags & ClassDisallowMultiple)
            for (const ComponentEntry& e : components_)
                if (e.klass == klass)
                {
                    HX_LOG_ERROR(EngineLog, "AddComponent: {} allows one per GameObject.", info.name);
                    return nullptr;
                }
        for (u32 required : info.required)
            if (!GetComponent(required) && !AddComponent(required))
                return nullptr;
        Component* c = static_cast<Component*>(NewObjectOfClass(klass));
        GameObjectAccess::SetGameObject(*c, this);
        components_.Push({PPtr<Component>(c), klass});
        if (prepare)
            prepare(*c, user);
        GameObjectAccess::Notify(c);
        return c;
    }

    Component* GameObject::GetComponent(u32 klass) const
    {
        if (klass >= ClassCount())
            return nullptr;
        Range r(klass);
        // The entries name live components of the classes they record: no class check on the way back.
        for (const ComponentEntry& e : components_)
            if (r.Has(e.klass))
                return static_cast<Component*>(EntityIdToObject(e.component.Id()));
        return nullptr;
    }

    namespace
    {
        // The engine class of a type of the HelixEngine module (kNoClass for other types).
        u32 ClassOfType(scripting::TypeId type)
        {
            const scripting::TypeInfo* t = scripting::GetType(type);
            return t && t->module == detail::EngineModule() && t->kind == scripting::TypeKind::Class ? u32(t->user) : kNoClass;
        }
    }

    Component* GameObject::AddComponent(scripting::TypeId type)
    {
        u32 klass = ClassOfType(type);
        if (klass != kNoClass)
            return AddComponent(klass);
        if (const ScriptClass* script = FindScriptClass(type))
            return AddScript(this, *script);
        const scripting::TypeInfo* t = scripting::GetType(type);
        HX_LOG_ERROR(EngineLog, "AddComponent: {} is not a component type.", t ? t->fullName : StringView("an unknown type"));
        return nullptr;
    }

    Component* GameObject::GetComponent(scripting::TypeId type) const
    {
        u32 klass = ClassOfType(type);
        if (klass != kNoClass)
            return GetComponent(klass);
        // Scripts of the type or a type deriving from it.
        u32 behaviours = ClassOf<GameBehaviour>();
        for (const ComponentEntry& e : components_)
            if (IsDerivedFrom(e.klass, behaviours))
                if (auto* b = static_cast<GameBehaviour*>(EntityIdToObject(e.component.Id())))
                    if (b->script() && scripting::IsAssignableFrom(type, b->script()->type))
                        return b;
        return nullptr;
    }

    Component* GameObject::GetComponent(StringView className) const
    {
        u32 k = FindClassByName(className);
        return k == kNoClass ? nullptr : GetComponent(k);
    }

    void GameObject::GetComponents(u32 klass, Array<Component*>& out) const
    {
        out.Clear();
        if (klass >= ClassCount())
            return;
        Range r(klass);
        for (const ComponentEntry& e : components_)
            if (r.Has(e.klass))
                out.Push(e.component.Get());
    }

    Component* GameObject::GetComponentAtIndex(u32 index) const { return index < components_.Size() ? components_[index].component.Get() : nullptr; }

    i32 GameObject::GetComponentIndex(const Component* component) const
    {
        for (u32 i = 0; i < components_.Size(); i++)
            if (components_[i].component.Id() == component->GetEntityId())
                return i32(i);
        return -1;
    }

    namespace
    {
        // Self first, then the descendants in node order (depth first); inactive subtrees skipped unless asked.
        void InChildren(const GameObject* g, const Range& r, bool includeInactive, bool all, Array<Component*>& out)
        {
            auto visit = [&](const GameObject* x) {
                for (const GameObject::ComponentEntry& e : x->Components())
                    if (r.Has(e.klass))
                    {
                        out.Push(e.component.Get());
                        if (!all)
                            return true;
                    }
                return false;
            };
            Transform* t = g->transform();
            if (!t)
            {
                if (includeInactive || g->activeInHierarchy())
                    visit(g);
                return;
            }
            const TransformHierarchy& h = t->Hierarchy();
            for (u32 j = t->NodeIndex(), end = j + h.size[j]; j < end;)
            {
                const Transform* x = h.transforms[j];
                const GameObject* xg = x ? x->gameObject() : nullptr;
                if (!xg || (!includeInactive && !xg->activeInHierarchy()))
                {
                    j += h.size[j];
                    continue;
                }
                if (visit(xg))
                    return;
                j++;
            }
        }

        void InParent(const GameObject* g, const Range& r, bool includeInactive, bool all, Array<Component*>& out)
        {
            for (; g; g = GameObjectOf(g->transform() ? g->transform()->parent() : nullptr))
            {
                if (!includeInactive && !g->activeInHierarchy())
                    continue;
                for (const GameObject::ComponentEntry& e : g->Components())
                    if (r.Has(e.klass))
                    {
                        out.Push(e.component.Get());
                        if (!all)
                            return;
                    }
            }
        }
    }

    Component* GameObject::GetComponentInChildren(u32 klass, bool includeInactive) const
    {
        Array<Component*> out;
        if (klass < ClassCount())
            InChildren(this, Range(klass), includeInactive, false, out);
        return out.Empty() ? nullptr : out[0];
    }

    void GameObject::GetComponentsInChildren(u32 klass, bool includeInactive, Array<Component*>& out) const
    {
        out.Clear();
        if (klass < ClassCount())
            InChildren(this, Range(klass), includeInactive, true, out);
    }

    Component* GameObject::GetComponentInParent(u32 klass, bool includeInactive) const
    {
        Array<Component*> out;
        if (klass < ClassCount())
            InParent(this, Range(klass), includeInactive, false, out);
        return out.Empty() ? nullptr : out[0];
    }

    void GameObject::GetComponentsInParent(u32 klass, bool includeInactive, Array<Component*>& out) const
    {
        out.Clear();
        if (klass < ClassCount())
            InParent(this, Range(klass), includeInactive, true, out);
    }

    GameObject* GameObject::Find(StringView path)
    {
        bool fromRoot = !path.Empty() && path[0] == '/';
        if (fromRoot)
            path = path.Substr(1);
        // The last name, then its parents' names backwards.
        usize slash = path.Size();
        while (slash > 0 && path[slash - 1] != '/')
            slash--;
        StringView last = path.Substr(slash);
        Array<GameObject*> all;
        engine::FindObjectsByType(all);
        for (GameObject* g : all)
        {
            if (g->Name() != last)
                continue;
            Transform* t = g->transform();
            StringView rest = slash ? path.Substr(0, slash - 1) : StringView();
            bool match = true;
            while (match && !rest.Empty())
            {
                usize s = rest.Size();
                while (s > 0 && rest[s - 1] != '/')
                    s--;
                t = t ? t->parent() : nullptr;
                match = t && t->gameObject()->Name() == rest.Substr(s);
                rest = s ? rest.Substr(0, s - 1) : StringView();
            }
            if (match && (!fromRoot || !t || !t->parent()))
                return g;
        }
        return nullptr;
    }

    GameObject* GameObject::FindWithTag(StringView tag)
    {
        TagHandle h;
        if (!GetExistingTag(tag, h))
        {
            HX_LOG_ERROR(EngineLog, "Tag: {} is not defined.", tag);
            return nullptr;
        }
        Array<GameObject*> all;
        engine::FindObjectsByType(all);
        for (GameObject* g : all)
            if (g->tag_ == h)
                return g;
        return nullptr;
    }

    void GameObject::FindGameObjectsWithTag(StringView tag, Array<GameObject*>& out)
    {
        out.Clear();
        TagHandle h;
        if (!GetExistingTag(tag, h))
        {
            HX_LOG_ERROR(EngineLog, "Tag: {} is not defined.", tag);
            return;
        }
        Array<GameObject*> all;
        engine::FindObjectsByType(all);
        for (GameObject* g : all)
            if (g->tag_ == h)
                out.Push(g);
    }

    void GameObject::SetGameObjectsActive(Span<const EntityId> ids, bool value)
    {
        for (EntityId id : ids)
            if (GameObject* g = EntityIdToObject<GameObject>(id))
                g->SetActive(value);
    }

    // ------------------------------------------------------------------------------------------------ Component

    Transform* Component::transform() const
    {
        GameObject* g = gameObject();
        return g ? g->transform() : nullptr;
    }

    StringView Component::Name() const
    {
        GameObject* g = gameObject();
        return g ? g->Name() : StringView();
    }

    void Component::SetName(StringView name)
    {
        if (GameObject* g = gameObject())
            g->SetName(name);
    }

    StringView Component::TagString() const
    {
        GameObject* g = gameObject();
        return g ? g->TagString() : StringView();
    }

    bool Component::CompareTag(StringView tag) const
    {
        GameObject* g = gameObject();
        return g && g->CompareTag(tag);
    }

    i32 Component::GetComponentIndex() const
    {
        GameObject* g = gameObject();
        return g ? g->GetComponentIndex(this) : -1;
    }

    void GameObject::SetLayer(u32 layer)
    {
        u8 now = u8(layer < kLayerCount ? layer : 0);
        if (now == layer_)
            return;
        layer_ = now;
        for (const ComponentEntry& e : components_)
            if (Component* c = e.component.Get())
                if (const ClassInfo& info = c->Class(); info.onLayerChanged)
                    info.onLayerChanged(*c);
    }

    void Behaviour::SetEnabled(bool value)
    {
        if (enabled_ == value)
            return;
        enabled_ = value;
        GameObjectAccess::Notify(this);
    }
}
