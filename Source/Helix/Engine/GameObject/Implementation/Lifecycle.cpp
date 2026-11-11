#include <Helix/Engine/GameObject/Implementation/GameObjectAccess.h>
#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Engine/Object/Implementation/ObjectAccess.h>
#include <Helix/Engine/Transform/Implementation/TransformAccess.h>

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

        // Every GameObject and component of a hierarchy in node order (parents before children), each GameObject
        // before its components; the Transforms in node order too.
        void Collect(GameObject* g, Array<Object*>& out, Array<Transform*>& transforms)
        {
            auto add = [&](GameObject* x) {
                out.Push(x);
                for (const GameObject::ComponentEntry& e : x->Components())
                    if (Component* c = e.component.Get())
                        out.Push(c);
            };
            Transform* t = g->transform();
            if (!t)
            {
                add(g);
                return;
            }
            const TransformHierarchy& h = t->Hierarchy();
            for (u32 j = t->NodeIndex(), end = j + h.size[j]; j < end;)
            {
                Transform* x = h.transforms[j];
                if (!x)
                {
                    j += h.size[j];
                    continue;
                }
                transforms.Push(x);
                add(x->gameObject());
                j++;
            }
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

        // Children first (the last first), then the components from the last to the first (the Transform last), then
        // the GameObject. The hierarchy stays as it is while the hooks run; destroyed children's nodes are marked dead
        // and the topmost destroyed Transform takes the whole range out at once.
        void DestroyGameObject(GameObject* g)
        {
            ObjectAccess::SetDestroying(g);
            if (Transform* t = g->transform())
            {
                // By id: the hooks may destroy other objects.
                SmallArray<EntityId, 8> children;
                for (u32 i = 0, n = t->childCount(); i < n; i++)
                    children.Push(t->GetChild(i)->GetEntityId());
                for (u32 i = u32(children.Size()); i-- > 0;)
                    if (Transform* child = EntityIdToObject<Transform>(children[i]))
                        if (GameObject* cg = child->gameObject(); cg && !ObjectAccess::Destroying(cg))
                            DestroyGameObject(cg);
            }
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

        // Where the clone's root goes: under `parent` (world or local placement kept), or a root with the original's
        // world placement; `placed` puts it at a world position and rotation instead.
        struct Placement
        {
            Transform* parent = nullptr;
            bool worldSpace = false;
            bool placed = false;
            Vector3 position;
            Quaternion rotation;
        };

        Object* CloneHierarchy(Object* original, const Placement& where)
        {
            GameObject* root = original->Is<GameObject>()   ? static_cast<GameObject*>(original)
                               : original->Is<Component>() ? static_cast<Component*>(original)->gameObject()
                                                           : nullptr;
            ReferenceRemap remap;
            Array<Object*> originals, clones;
            Array<Transform*> originalTransforms, cloneTransforms;
            if (root)
                Collect(root, originals, originalTransforms);
            else
                originals.Push(original);
            remap.map.Reserve(originals.Size());
            clones.Reserve(originals.Size());
            cloneTransforms.Reserve(originalTransforms.Size());
            for (Object* o : originals)
            {
                Object* c = CloneObject(*o);
                if (!c)
                    return nullptr;
                remap.map.Set(o->GetEntityId(), c->GetEntityId());
                clones.Push(c);
                if (c->Is<Transform>())
                    cloneTransforms.Push(static_cast<Transform*>(c));
            }
            Object* top = clones[0];
            if (root)
            {
                // The copy's hierarchy: the original's nodes, parents and local values in one block, a new root.
                TransformAccess::BuildClone(Span<Transform* const>(originalTransforms.Data(), originalTransforms.Size()),
                                            Span<Transform* const>(cloneTransforms.Data(), cloneTransforms.Size()));
                Transform* from = originalTransforms[0];
                Transform* t = cloneTransforms[0];
                if (where.parent)
                    TransformAccess::Place(t, where.parent, ~0u);
                if (where.placed)
                    t->SetPositionAndRotation(where.position, where.rotation);
                else if ((!where.parent || where.worldSpace) && from->parent())
                    TransformAccess::SetWorld(t, from->localToWorldMatrix(), from->rotation());
                else if (where.parent)
                    TransformAccess::MarkChanged(t->Hierarchy(), t->NodeIndex());
            }
            for (Object* c : clones)
                c->Class().remap(*c, remap);
            top->SetName(Format("{}(Clone)", original->Name()));
            if (root)
            {
                // Copies start inactive and enable through the normal path, parents first.
                GameObject* g = static_cast<GameObject*>(top);
                for (Object* c : clones)
                    if (c->Is<GameObject>())
                        GameObjectAccess::ActiveInHierarchy(*static_cast<GameObject*>(c)) = false;
                GameObjectAccess::UpdateActive(g);
            }
            return EntityIdToObject(*remap.map.Find(original->GetEntityId()));
        }

        Object* Clone(Object* original, const Placement& where)
        {
            if (!original)
            {
                HX_LOG_ERROR(LifecycleLog, "Instantiate: the object to clone is null.");
                return nullptr;
            }
            return CloneHierarchy(original, where);
        }
    }

    Object* Instantiate(Object* original, const InstantiateParameters& parameters)
    {
        Placement where;
        where.parent = parameters.parent;
        where.worldSpace = parameters.worldSpace;
        return Clone(original, where);
    }

    Object* Instantiate(Object* original, Transform* parent, bool instantiateInWorldSpace)
    {
        Placement where;
        where.parent = parent;
        where.worldSpace = instantiateInWorldSpace;
        return Clone(original, where);
    }

    Object* Instantiate(Object* original, Vector3 position, Quaternion rotation, Transform* parent)
    {
        Placement where;
        where.parent = parent;
        where.placed = true;
        where.position = position;
        where.rotation = rotation;
        return Clone(original, where);
    }

    bool InstantiateGameObjects(EntityId source, u32 count, Span<EntityId> ids, Span<EntityId> transformIds)
    {
        GameObject* g = EntityIdToObject<GameObject>(source);
        if (!g || ids.Size() < count || transformIds.Size() < count)
            return false;
        for (u32 i = 0; i < count; i++)
        {
            GameObject* c = static_cast<GameObject*>(CloneHierarchy(g, Placement()));
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
