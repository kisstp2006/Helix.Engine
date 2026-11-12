#include <Helix/Engine/GameObject/GameObject.h>
#include <Helix/Engine/Object/Implementation/ScriptingTypes.h>
#include <Helix/Engine/Script/GameBehaviour.h>
#include <Helix/Engine/Transform/Transform.h>

// The members of GameObject and Component the script API has by type (an engine class or a script class of either
// language), over the engine's lookups by class.
namespace helix::engine
{
    namespace
    {
        // Whether a component is of the type: an engine class (or a subclass), or a script of the type (or a type
        // deriving from it).
        struct TypeMatch
        {
            u32 klass = kNoClass;
            scripting::TypeId script;

            explicit TypeMatch(scripting::TypeId type)
            {
                const scripting::TypeInfo* t = scripting::GetType(type);
                if (t && t->module == detail::EngineModule() && t->kind == scripting::TypeKind::Class)
                    klass = u32(t->user);
                else
                    script = type;
            }

            bool operator()(const Component* c) const
            {
                if (!c)
                    return false;
                if (klass != kNoClass)
                    return IsDerivedFrom(c->ClassIndex(), klass);
                if (!script.IsValid() || !IsDerivedFrom(c->ClassIndex(), ClassOf<GameBehaviour>()))
                    return false;
                const ScriptClass* s = static_cast<const GameBehaviour*>(c)->script();
                return s && scripting::IsAssignableFrom(script, s->type);
            }
        };

        void Collect(const GameObject* g, const TypeMatch& match, Array<Component*>& out, bool first)
        {
            for (u32 i = 0; i < g->GetComponentCount() && !(first && !out.Empty()); i++)
            {
                Component* c = g->GetComponentAtIndex(i);
                if (match(c))
                    out.Push(c);
            }
        }

        // Self, then the children depth first.
        void CollectInChildren(const GameObject* g, const TypeMatch& match, bool includeInactive, Array<Component*>& out, bool first)
        {
            if (!includeInactive && !g->activeInHierarchy())
                return;
            Collect(g, match, out, first);
            Transform* t = g->transform();
            for (u32 i = 0; t && i < t->childCount() && !(first && !out.Empty()); i++)
                if (Transform* child = t->GetChild(i))
                    CollectInChildren(child->gameObject(), match, includeInactive, out, first);
        }

        // Self, then the parents.
        void CollectInParent(const GameObject* g, const TypeMatch& match, bool includeInactive, Array<Component*>& out, bool first)
        {
            for (Transform* t = g->transform(); t && !(first && !out.Empty()); t = t->parent())
            {
                GameObject* o = t->gameObject();
                if (o && (includeInactive || o->activeInHierarchy()))
                    Collect(o, match, out, first);
            }
        }
    }

    Array<Component*> GameObject::GetComponents(scripting::TypeId type) const
    {
        Array<Component*> out;
        Collect(this, TypeMatch(type), out, false);
        return out;
    }

    Component* GameObject::GetComponentInChildren(scripting::TypeId type, bool includeInactive) const
    {
        Array<Component*> out;
        CollectInChildren(this, TypeMatch(type), includeInactive, out, true);
        return out.Empty() ? nullptr : out[0];
    }

    Array<Component*> GameObject::GetComponentsInChildren(scripting::TypeId type, bool includeInactive) const
    {
        Array<Component*> out;
        CollectInChildren(this, TypeMatch(type), includeInactive, out, false);
        return out;
    }

    Component* GameObject::GetComponentInParent(scripting::TypeId type, bool includeInactive) const
    {
        Array<Component*> out;
        CollectInParent(this, TypeMatch(type), includeInactive, out, true);
        return out.Empty() ? nullptr : out[0];
    }

    Array<Component*> GameObject::GetComponentsInParent(scripting::TypeId type, bool includeInactive) const
    {
        Array<Component*> out;
        CollectInParent(this, TypeMatch(type), includeInactive, out, false);
        return out;
    }

    bool GameObject::TryGetComponent(scripting::TypeId type, Component*& component) const
    {
        component = GetComponent(type);
        return component != nullptr;
    }

    Component* GameObject::GetComponentAtIndexApi(i32 index) const { return index >= 0 ? GetComponentAtIndex(u32(index)) : nullptr; }

    Array<GameObject*> GameObject::FindGameObjectsWithTagApi(StringView tag)
    {
        Array<GameObject*> out;
        FindGameObjectsWithTag(tag, out);
        return out;
    }

    void Component::SetTagApi(StringView value)
    {
        if (GameObject* g = gameObject())
            g->SetTag(value);
    }

    Component* Component::GetComponent(scripting::TypeId type) const
    {
        GameObject* g = gameObject();
        return g ? g->GetComponent(type) : nullptr;
    }

    Component* Component::GetComponentByName(StringView type) const
    {
        GameObject* g = gameObject();
        return g ? g->GetComponent(type) : nullptr;
    }

    Array<Component*> Component::GetComponents(scripting::TypeId type) const
    {
        GameObject* g = gameObject();
        return g ? g->GetComponents(type) : Array<Component*>();
    }

    Component* Component::GetComponentInChildren(scripting::TypeId type, bool includeInactive) const
    {
        GameObject* g = gameObject();
        return g ? g->GetComponentInChildren(type, includeInactive) : nullptr;
    }

    Array<Component*> Component::GetComponentsInChildren(scripting::TypeId type, bool includeInactive) const
    {
        GameObject* g = gameObject();
        return g ? g->GetComponentsInChildren(type, includeInactive) : Array<Component*>();
    }

    Component* Component::GetComponentInParent(scripting::TypeId type, bool includeInactive) const
    {
        GameObject* g = gameObject();
        return g ? g->GetComponentInParent(type, includeInactive) : nullptr;
    }

    Array<Component*> Component::GetComponentsInParent(scripting::TypeId type, bool includeInactive) const
    {
        GameObject* g = gameObject();
        return g ? g->GetComponentsInParent(type, includeInactive) : Array<Component*>();
    }

    bool Component::TryGetComponent(scripting::TypeId type, Component*& component) const
    {
        component = GetComponent(type);
        return component != nullptr;
    }
}
