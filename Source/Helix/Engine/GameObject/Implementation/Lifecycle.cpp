#include <Helix/Engine/GameObject/Implementation/GameObjectAccess.h>
#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Engine/Object/Implementation/ObjectAccess.h>

#include <Helix/Core/Logging/Log.h>
#include <Helix/Core/Strings/Format.h>

namespace helix::engine
{
    namespace
    {
        LogChannel LifecycleLog{"Engine"};

        struct Pending
        {
            EntityId id;
            double due;
        };

        struct Queue
        {
            Array<Pending> pending;
            double now = 0;
        };

        Queue& Destroys()
        {
            static Queue q;
            return q;
        }

        // Every GameObject and component of a hierarchy, parents before children, each GameObject before its components.
        void Collect(GameObject* g, Array<Object*>& out)
        {
            out.Push(g);
            for (const GameObject::ComponentEntry& e : g->Components())
                if (Component* c = e.component.Get())
                    out.Push(c);
            if (Transform* t = g->transform())
                for (u32 i = 0; i < t->childCount(); i++)
                    if (Transform* child = t->GetChild(i))
                        Collect(child->gameObject(), out);
        }

        // The component's disable and destroy hooks, then the object goes (its GameObject keeps no entry for it).
        void DestroyComponent(Component* c)
        {
            ObjectAccess::SetDestroying(c);
            const ClassInfo& info = c->Class();
            // A Behaviour that took part is disabled (Notify sees it is going); other components while active.
            if (info.flags & ClassBehaviour)
                GameObjectAccess::Notify(c);
            else if (info.onDisable && c->gameObject() && c->gameObject()->activeInHierarchy())
                info.onDisable(*c);
            if (info.onDestroy)
                info.onDestroy(*c);
            if (GameObject* g = c->gameObject())
            {
                SmallArray<GameObject::ComponentEntry, 4>& list = GameObjectAccess::Components(*g);
                for (u32 i = 0; i < list.Size(); i++)
                    if (list[i].component.Id() == c->GetEntityId())
                    {
                        list.RemoveAt(i);
                        break;
                    }
            }
            DeleteObject(c);
        }

        // Children first, then the components from the last to the first (the Transform last), then the GameObject.
        void DestroyGameObject(GameObject* g)
        {
            ObjectAccess::SetDestroying(g);
            if (Transform* t = g->transform())
                while (t->childCount())
                {
                    Transform* child = t->GetChild(t->childCount() - 1);
                    GameObjectAccess::Detach(child);
                    DestroyGameObject(child->gameObject());
                }
            if (Transform* t = g->transform())
                GameObjectAccess::Detach(t);
            SmallArray<GameObject::ComponentEntry, 4>& list = GameObjectAccess::Components(*g);
            while (!list.Empty())
            {
                Component* c = list.Back().component.Get();
                if (!c)
                {
                    list.RemoveAt(list.Size() - 1);
                    continue;
                }
                DestroyComponent(c);
            }
            if (g->Class().onDestroy)
                g->Class().onDestroy(*g);
            DeleteObject(g);
        }

        bool DestroyNow(Object* object)
        {
            if (!object)
                return false;
            if (object->Is<Transform>())
            {
                HX_LOG_ERROR(LifecycleLog, "Destroying the Transform of {} is not allowed; destroy the GameObject.", object->Name());
                return false;
            }
            if (object->Is<GameObject>())
                DestroyGameObject(static_cast<GameObject*>(object));
            else if (object->Is<Component>())
                DestroyComponent(static_cast<Component*>(object));
            else
            {
                if (object->Class().onDestroy)
                    object->Class().onDestroy(*object);
                DeleteObject(object);
            }
            return true;
        }

        Object* CloneHierarchy(Object* original, Transform* parent)
        {
            GameObject* root = original->Is<GameObject>()   ? static_cast<GameObject*>(original)
                               : original->Is<Component>() ? static_cast<Component*>(original)->gameObject()
                                                           : nullptr;
            ReferenceRemap remap;
            Array<Object*> originals, clones;
            if (root)
                Collect(root, originals);
            else
                originals.Push(original);
            remap.map.Reserve(originals.Size());
            clones.Reserve(originals.Size());
            for (Object* o : originals)
            {
                Object* c = CloneObject(*o);
                if (!c)
                    return nullptr;
                remap.map.Set(o->GetEntityId(), c->GetEntityId());
                clones.Push(c);
            }
            for (Object* c : clones)
                c->Class().remap(*c, remap);
            Object* top = clones[0];
            top->SetName(Format("{}(Clone)", original->Name()));
            if (root)
            {
                // The root's parent was outside the clone: the clone hangs where it is told to, or at the root.
                GameObject* g = static_cast<GameObject*>(top);
                Transform* t = g->transform();
                GameObjectAccess::Parent(*t) = PPtr<Transform>();
                if (parent)
                {
                    GameObjectAccess::Parent(*t) = PPtr<Transform>(parent);
                    GameObjectAccess::Children(*parent).Push(PPtr<Transform>(t));
                }
                // Copies start inactive and enable through the normal path, parents first.
                for (Object* c : clones)
                    if (c->Is<GameObject>())
                        GameObjectAccess::ActiveInHierarchy(*static_cast<GameObject*>(c)) = false;
                GameObjectAccess::UpdateActive(g);
            }
            return EntityIdToObject(*remap.map.Find(original->GetEntityId()));
        }
    }

    Object* Instantiate(Object* original, const InstantiateParameters& parameters)
    {
        if (!original)
        {
            HX_LOG_ERROR(LifecycleLog, "Instantiate: the object to clone is null.");
            return nullptr;
        }
        return CloneHierarchy(original, parameters.parent);
    }

    Object* Instantiate(Object* original, Transform* parent, bool worldPositionStays)
    {
        InstantiateParameters p;
        p.parent = parent;
        p.worldSpace = worldPositionStays;
        return Instantiate(original, p);
    }

    bool InstantiateGameObjects(EntityId source, u32 count, Span<EntityId> ids, Span<EntityId> transformIds)
    {
        GameObject* g = EntityIdToObject<GameObject>(source);
        if (!g || ids.Size() < count || transformIds.Size() < count)
            return false;
        for (u32 i = 0; i < count; i++)
        {
            GameObject* c = static_cast<GameObject*>(CloneHierarchy(g, nullptr));
            ids[i] = c->GetEntityId();
            transformIds[i] = c->transform()->GetEntityId();
        }
        return true;
    }

    void Destroy(Object* object, float delay)
    {
        if (!object || ObjectAccess::Destroying(object))
            return;
        Queue& q = Destroys();
        q.pending.Push({object->GetEntityId(), q.now + (delay > 0 ? double(delay) : 0.0)});
    }

    bool DestroyImmediate(Object* object, bool allowDestroyingAssets)
    {
        if (!object || ObjectAccess::Destroying(object))
            return false;
        if (object->IsPersistent() && !allowDestroyingAssets)
        {
            HX_LOG_ERROR(LifecycleLog, "Destroying assets is not permitted to avoid data loss ({}).", object->Name());
            return false;
        }
        return DestroyNow(object);
    }

    bool DontDestroyOnLoad(Object* target)
    {
        GameObject* g = !target ? nullptr : target->Is<GameObject>() ? static_cast<GameObject*>(target)
                                          : target->Is<Component>()  ? static_cast<Component*>(target)->gameObject()
                                                                     : nullptr;
        if (!g)
            return false;
        if (g->transform() && g->transform()->parent())
        {
            HX_LOG_WARNING(LifecycleLog, "DontDestroyOnLoad only works for root GameObjects or components on root GameObjects ({}).", g->Name());
            return false;
        }
        ObjectAccess::SetDontDestroyOnLoad(g, true);
        return true;
    }

    void SetLifecycleTime(double scaledTime) { Destroys().now = scaledTime; }
    double LifecycleTime() { return Destroys().now; }
    u32 PendingDestroyCount() { return u32(Destroys().pending.Size()); }

    u32 ProcessDestroyQueue()
    {
        Queue& q = Destroys();
        // Due entries out first (the hooks may queue more, for the next pass).
        Array<EntityId> due;
        usize kept = 0;
        for (usize i = 0; i < q.pending.Size(); i++)
            if (q.pending[i].due <= q.now)
                due.Push(q.pending[i].id);
            else
                q.pending[kept++] = q.pending[i];
        q.pending.Resize(kept);
        u32 destroyed = 0;
        for (EntityId id : due)
            if (Object* o = EntityIdToObject(id))
                if (!ObjectAccess::Destroying(o))
                    destroyed += DestroyNow(o) ? 1 : 0;
        return destroyed;
    }
}
