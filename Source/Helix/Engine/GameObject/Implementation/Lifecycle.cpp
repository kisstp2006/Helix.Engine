#include <Helix/Engine/GameObject/Implementation/CloneAccess.h>
#include <Helix/Engine/GameObject/Implementation/GameObjectAccess.h>
#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Engine/Object/Implementation/ObjectAccess.h>
#include <Helix/Engine/Transform/Implementation/TransformAccess.h>

#include <Helix/Engine/Scene/SceneManager.h>

#include <Helix/Core/Logging/Log.h>

#include <chrono>
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
            // The scene slot of a clone without a parent.
            u32 scene = ~0u;
        };

        u32 SlotOf(Scene scene) { return scene.IsValid() ? scene.Slot() : TransformAccess::NewRootScene(); }

        // A prefab asset's objects in clone order, its Transforms among them and their places for remapping, built once
        // (prefab assets do not change while the game runs; InvalidatePrefabTemplate is for tools that change them).
        struct Template
        {
            Array<EntityId> objects = Array<EntityId>();
            Array<u32> transforms = Array<u32>();
            HashMap<EntityId, u32> index = HashMap<EntityId, u32>();
        };

        HashMap<EntityId, Template>& Templates()
        {
            static HashMap<EntityId, Template> t;
            return t;
        }

        // Valid until the next TemplateOf.
        Template* TemplateOf(GameObject* root)
        {
            HashMap<EntityId, Template>& all = Templates();
            if (Template* found = all.Find(root->GetEntityId()))
            {
                // Objects of the asset destroyed since: built again.
                bool valid = true;
                for (EntityId id : found->objects)
                    valid = valid && EntityIdIsValid(id);
                if (valid)
                    return found;
                all.Remove(root->GetEntityId());
            }
            Array<Object*> objects;
            Array<Transform*> transforms;
            Collect(root, objects, transforms);
            Template* t = &all.Set(root->GetEntityId(), Template());
            t->objects.Reserve(objects.Size());
            t->index.Reserve(objects.Size());
            for (u32 k = 0; k < objects.Size(); k++)
            {
                t->objects.Push(objects[k]->GetEntityId());
                t->index.Set(objects[k]->GetEntityId(), k);
                if (objects[k]->Is<Transform>())
                    t->transforms.Push(k);
            }
            return t;
        }

        // The copies of the objects, their hierarchy as a new root of `scene` and their references remapped; not named,
        // placed or enabled. Prefab assets go through their template (no map built per copy).
        bool CopyObjects(Object* original, GameObject* root, u32 scene, Array<Object*>& originals, Array<Object*>& clones,
                         Array<Transform*>& originalTransforms, Array<Transform*>& cloneTransforms)
        {
            ReferenceRemap remap;
            Array<EntityId> ids;
            Template* t = root && root->IsPersistent() ? TemplateOf(root) : nullptr;
            if (t)
            {
                originals.Reserve(t->objects.Size());
                for (EntityId id : t->objects)
                    originals.Push(EntityIdToObject(id));
                for (u32 k : t->transforms)
                    originalTransforms.Push(static_cast<Transform*>(originals[k]));
                remap.index = &t->index;
            }
            else if (root)
                Collect(root, originals, originalTransforms);
            else
                originals.Push(original);
            if (!t)
                remap.map.Reserve(originals.Size());
            clones.Reserve(originals.Size());
            ids.Reserve(originals.Size());
            cloneTransforms.Reserve(originalTransforms.Size());
            ReserveObjects(u32(originals.Size()));
            for (Object* o : originals)
            {
                Object* c = CloneObject(*o);
                if (!c)
                {
                    for (Object* made : clones)
                        DeleteObject(made);
                    clones.Clear();
                    return false;
                }
                if (!t)
                    remap.map.Set(o->GetEntityId(), c->GetEntityId());
                ids.Push(c->GetEntityId());
                clones.Push(c);
                if (c->Is<Transform>())
                    cloneTransforms.Push(static_cast<Transform*>(c));
            }
            remap.ids = ids.Data();
            if (root)
                // The copy's hierarchy: the original's nodes, parents and local values in one block, a new root.
                TransformAccess::BuildClone(Span<Transform* const>(originalTransforms.Data(), originalTransforms.Size()),
                                            Span<Transform* const>(cloneTransforms.Data(), cloneTransforms.Size()), scene);
            for (Object* c : clones)
                c->Class().remap(*c, remap);
            return true;
        }

        Object* CloneHierarchy(Object* original, const Placement& where)
        {
            GameObject* root = original->Is<GameObject>()   ? static_cast<GameObject*>(original)
                               : original->Is<Component>() ? static_cast<Component*>(original)->gameObject()
                                                           : nullptr;
            Array<Object*> originals, clones;
            Array<Transform*> originalTransforms, cloneTransforms;
            if (!CopyObjects(original, root, where.scene == ~0u ? TransformAccess::NewRootScene() : where.scene, originals, clones,
                             originalTransforms, cloneTransforms))
                return nullptr;
            Object* top = clones[0];
            if (root)
            {
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
            for (u32 k = 0; k < originals.Size(); k++)
                if (originals[k] == original)
                    return clones[k];
            return top;
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
        where.scene = SlotOf(parameters.scene);
        return Clone(original, where);
    }

    Object* Instantiate(Object* original, Transform* parent, bool instantiateInWorldSpace)
    {
        Placement where;
        where.parent = parent;
        where.worldSpace = instantiateInWorldSpace;
        return Clone(original, where);
    }

    Object* Instantiate(Object* original, Scene scene)
    {
        Placement where;
        where.scene = SlotOf(scene);
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

    bool InstantiateGameObjects(EntityId source, u32 count, Span<EntityId> ids, Span<EntityId> transformIds, Scene scene)
    {
        GameObject* g = EntityIdToObject<GameObject>(source);
        if (!g || ids.Size() < count || transformIds.Size() < count)
            return false;
        Placement where;
        where.scene = SlotOf(scene);
        for (u32 i = 0; i < count; i++)
        {
            GameObject* c = static_cast<GameObject*>(CloneHierarchy(g, where));
            if (!c)
                return false;
            ids[i] = c->GetEntityId();
            transformIds[i] = c->transform()->GetEntityId();
        }
        return true;
    }

    // ---------------------------------------------------------------------------------------------- prefab copies

    bool CloneAccess::Copy(GameObject* source, u32 scene, Array<Object*>& originals, Array<Object*>& clones)
    {
        Array<Transform*> originalTransforms, cloneTransforms;
        return CopyObjects(source, source, scene, originals, clones, originalTransforms, cloneTransforms);
    }

    void CloneAccess::ForgetTemplate(EntityId root) { Templates().Remove(root); }

    // ---------------------------------------------------------------------------------------------- InstantiateAsync

    namespace
    {
        struct AsyncCopy
        {
            EntityId original;
            u32 count = 0;
            Array<Vector3> positions = Array<Vector3>();
            Array<Quaternion> rotations = Array<Quaternion>();
            EntityId parent;
            bool worldSpace = false;
            u32 scene = ~0u;
            Array<EntityId> made = Array<EntityId>();
            bool done = false;
            bool allow = true;
            i32 priority = 0;
            struct Callback
            {
                void (*callback)(void*, AsyncInstantiateOperation);
                void* user;
            };
            Array<Callback> completed = Array<Callback>();
        };

        struct AsyncState
        {
            HashMap<u32, AsyncCopy> ops;
            // Operations not done yet, in the order they were asked for; finished ones are kept for a while.
            Array<u32> pending;
            Array<u32> finished;
            u32 next = 0;
            f32 budgetMs = 2.0f;
        };

        AsyncState& Async()
        {
            static AsyncState s;
            return s;
        }

        void Finish(u32 id)
        {
            AsyncState& s = Async();
            AsyncCopy* op = s.ops.Find(id);
            if (!op || op->done)
                return;
            op->done = true;
            for (usize i = 0; i < s.pending.Size(); i++)
                if (s.pending[i] == id)
                {
                    s.pending.RemoveAt(i);
                    break;
                }
            Array<AsyncCopy::Callback> callbacks = Move(op->completed);
            for (const AsyncCopy::Callback& c : callbacks)
                c.callback(c.user, AsyncInstantiateOperation{id});
            s.finished.Push(id);
            if (s.finished.Size() > 256)
            {
                s.ops.Remove(s.finished[0]);
                s.finished.RemoveAt(0);
            }
        }

        // One copy of the operation; false when the original is gone. Hooks of the copy may start operations of their
        // own, so the operation is found again afterwards.
        bool MakeOne(u32 id)
        {
            AsyncCopy& op = *Async().ops.Find(id);
            Object* original = EntityIdToObject(op.original);
            if (!original)
            {
                HX_LOG_ERROR(LifecycleLog, "InstantiateAsync: the original was destroyed; {} of {} copies were made.", op.made.Size(), op.count);
                return false;
            }
            Placement where;
            where.parent = EntityIdToObject<Transform>(op.parent);
            where.worldSpace = op.worldSpace;
            where.scene = op.scene;
            u32 i = u32(op.made.Size());
            if (!op.positions.Empty())
            {
                where.placed = true;
                where.position = op.positions[i % op.positions.Size()];
                where.rotation = op.rotations.Empty() ? Quaternion::identity : op.rotations[i % op.rotations.Size()];
            }
            Object* c = CloneHierarchy(original, where);
            if (!c)
                return false;
            Async().ops.Find(id)->made.Push(c->GetEntityId());
            return true;
        }

        void Complete(u32 id)
        {
            AsyncCopy* op = Async().ops.Find(id);
            while (op && !op->done && op->made.Size() < op->count)
            {
                if (!MakeOne(id))
                    break;
                op = Async().ops.Find(id);
            }
            Finish(id);
        }
    }

    AsyncInstantiateOperation InstantiateAsync(Object* original, u32 count, Span<const Vector3> positions, Span<const Quaternion> rotations,
                                               const InstantiateParameters& parameters)
    {
        if (!original || count == 0)
        {
            HX_LOG_ERROR(LifecycleLog, "InstantiateAsync: {}.", original ? "the count is 0" : "the object to clone is null");
            return {};
        }
        AsyncState& s = Async();
        u32 id = ++s.next ? s.next : ++s.next;
        AsyncCopy& op = s.ops.Set(id, AsyncCopy());
        op.original = original->GetEntityId();
        op.count = count;
        op.positions.Append(positions.Data(), positions.Size());
        op.rotations.Append(rotations.Data(), rotations.Size());
        op.parent = parameters.parent ? parameters.parent->GetEntityId() : EntityId();
        op.worldSpace = parameters.worldSpace;
        op.scene = SlotOf(parameters.scene);
        op.made.Reserve(count);
        s.pending.Push(id);
        return AsyncInstantiateOperation{id};
    }

    void ProcessAsyncInstantiations()
    {
        AsyncState& s = Async();
        if (s.pending.Empty())
            return;
        auto start = std::chrono::steady_clock::now();
        auto budget = std::chrono::duration<double, std::milli>(s.budgetMs);
        // Higher priority first, otherwise in the order asked for; at least one copy a frame so nothing starves.
        Array<u32> order = s.pending;
        for (usize i = 1; i < order.Size(); i++)
            for (usize j = i; j > 0 && s.ops.Find(order[j])->priority > s.ops.Find(order[j - 1])->priority; j--)
            {
                u32 x = order[j];
                order[j] = order[j - 1];
                order[j - 1] = x;
            }
        bool madeAny = false;
        for (u32 id : order)
        {
            AsyncCopy* op = s.ops.Find(id);
            if (!op || op->done || !op->allow)
                continue;
            while (op->made.Size() < op->count && (!madeAny || std::chrono::steady_clock::now() - start < budget))
            {
                if (!MakeOne(id))
                {
                    op = s.ops.Find(id);
                    op->count = u32(op->made.Size());
                    break;
                }
                madeAny = true;
                op = s.ops.Find(id);
            }
            if (op->made.Size() >= op->count)
                Finish(id);
            if (std::chrono::steady_clock::now() - start >= budget)
                break;
        }
    }

    bool AsyncInstantiateOperation::isDone() const
    {
        const AsyncCopy* op = Async().ops.Find(id);
        return !op || op->done;
    }

    f32 AsyncInstantiateOperation::progress() const
    {
        const AsyncCopy* op = Async().ops.Find(id);
        if (!op || op->done)
            return 1.0f;
        if (!op->allow && op->made.Empty())
            return 0.9f;
        return f32(op->made.Size()) / f32(op->count);
    }

    i32 AsyncInstantiateOperation::priority() const
    {
        const AsyncCopy* op = Async().ops.Find(id);
        return op ? op->priority : 0;
    }

    void AsyncInstantiateOperation::SetPriority(i32 value) const
    {
        if (AsyncCopy* op = Async().ops.Find(id))
            op->priority = value;
    }

    bool AsyncInstantiateOperation::allowSceneActivation() const
    {
        const AsyncCopy* op = Async().ops.Find(id);
        return !op || op->allow;
    }

    void AsyncInstantiateOperation::SetAllowSceneActivation(bool value) const
    {
        if (AsyncCopy* op = Async().ops.Find(id))
            op->allow = value;
    }

    bool AsyncInstantiateOperation::IsWaitingForSceneActivation() const
    {
        const AsyncCopy* op = Async().ops.Find(id);
        return op && !op->done && !op->allow;
    }

    void AsyncInstantiateOperation::WaitForCompletion() const { Complete(id); }

    void AsyncInstantiateOperation::Cancel() const
    {
        AsyncCopy* op = Async().ops.Find(id);
        if (!op || op->done)
            return;
        Array<EntityId> made = Move(op->made);
        for (EntityId e : made)
            if (Object* o = EntityIdToObject(e))
                DestroyImmediate(o);
        op = Async().ops.Find(id);
        if (op)
            op->made.Clear();
        Finish(id);
    }

    void AsyncInstantiateOperation::Result(Array<Object*>& out) const
    {
        out.Clear();
        if (const AsyncCopy* op = Async().ops.Find(id))
            for (EntityId e : op->made)
                if (Object* o = EntityIdToObject(e))
                    out.Push(o);
    }

    void AsyncInstantiateOperation::AddCompleted(void (*callback)(void*, AsyncInstantiateOperation), void* user) const
    {
        AsyncCopy* op = Async().ops.Find(id);
        if (!op || op->done)
        {
            callback(user, *this);
            return;
        }
        op->completed.Push({callback, user});
    }

    void AsyncInstantiateOperation::SetIntegrationTimeMS(f32 milliseconds) { Async().budgetMs = milliseconds > 0 ? milliseconds : 0; }
    f32 AsyncInstantiateOperation::GetIntegrationTimeMS() { return Async().budgetMs; }

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
        TransformAccess::MoveToScene(g->transform()->Hierarchy(), DontDestroyOnLoadScene().Slot());
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
