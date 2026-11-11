#include <Helix/Engine/GameObject/Implementation/CloneAccess.h>
#include <Helix/Engine/GameObject/Implementation/GameObjectAccess.h>
#include <Helix/Engine/Prefab/Prefab.h>
#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Engine/Object/Implementation/ObjectAccess.h>
#include <Helix/Engine/Scene/Implementation/SceneAccess.h>
#include <Helix/Engine/Scene/SceneFile.h>
#include <Helix/Engine/Transform/Implementation/TransformAccess.h>

#include <Helix/Core/Logging/Log.h>
#include <Helix/Serialization/Yaml/Parser.h>

#include <cstring>

namespace helix::engine
{
    using namespace serialization;

    namespace
    {
        LogChannel SceneFileLog{"Engine"};

        // ------------------------------------------------------------------------------------------------ writing

        // The scene's objects in node order (each GameObject before its components), the roots in order. GameObjects
        // with DontSaveInEditor are left out with their children, components with it on their own.
        struct Collected
        {
            Array<Object*> objects;
            Array<Transform*> roots;
        };

        bool Saved(const Object* o) { return !Has(o->hideFlags, HideFlags::DontSaveInEditor); }

        void Collect(u32 slot, Collected& c)
        {
            for (TransformHierarchy* h = FirstRootHierarchy(slot); h; h = h->nextRoot)
            {
                for (u32 j = 0; j < h->count;)
                {
                    Transform* t = h->transforms[j];
                    GameObject* g = t ? t->gameObject() : nullptr;
                    if (!g || !Saved(g))
                    {
                        j += h->size[j];
                        continue;
                    }
                    if (j == 0)
                        c.roots.Push(t);
                    c.objects.Push(g);
                    for (const GameObject::ComponentEntry& e : g->Components())
                        if (Component* comp = e.component.Get())
                            if (comp == t || Saved(comp))
                                c.objects.Push(comp);
                    j++;
                }
            }
        }

        // Objects of the file to their fileIDs; anything else (other scenes, unsaved objects) is written as 0.
        struct WriteTable final : ReferenceTable
        {
            HashMap<EntityId, i64> ids;
            AssetReference ToFile(u64 object) override
            {
                AssetReference r;
                if (const i64* f = ids.Find(EntityId::FromULong(object)))
                    r.fileID = *f;
                return r;
            }
            u64 FromFile(const AssetReference&) override { return 0; }
        };

        // fileIDs for the collected objects: those the file had keep theirs; the scene remembers them all.
        bool AssignFileIds(u32 slot, const Collected& c, WriteTable& table, Array<i64>& fileIds, String& error)
        {
            HashMap<EntityId, i64>* known = SceneAccess::FileIds(slot);
            if (!known)
                return error = "the scene is not valid", false;
            HashMap<i64, EntityId> used;
            used.Reserve(c.objects.Size());
            table.ids.Reserve(c.objects.Size());
            for (Object* o : c.objects)
                if (const i64* f = known->Find(o->GetEntityId()))
                    if (!used.Find(*f))
                        used.Set(*f, o->GetEntityId());
            fileIds.Reserve(c.objects.Size());
            HashMap<EntityId, i64> now;
            now.Reserve(c.objects.Size());
            for (Object* o : c.objects)
            {
                const i64* f = known->Find(o->GetEntityId());
                i64 id = f && *used.Find(*f) == o->GetEntityId() ? *f : 0;
                if (!id)
                {
                    id = SceneAccess::NewFileId(slot, used);
                    used.Set(id, o->GetEntityId());
                }
                fileIds.Push(id);
                table.ids.Set(o->GetEntityId(), id);
                now.Set(o->GetEntityId(), id);
            }
            *known = Move(now);
            return true;
        }

        SceneRoots RootsOf(const Collected& c)
        {
            SceneRoots r;
            for (Transform* t : c.roots)
                r.m_Roots.Push(PPtr<Transform>(t));
            return r;
        }

        // ------------------------------------------------------------------------------------------------ building

        // Maps the file's fileIDs to the objects made for them; external references wait for the asset runtime.
        struct ReadTable final : ReferenceTable
        {
            HashMap<i64, EntityId> objects;
            // Objects of classes the engine does not have: references to them read as null without a complaint.
            HashMap<i64, u8> skipped;
            Array<String>* errors = nullptr;
            AssetReference ToFile(u64) override { return {}; }
            u64 FromFile(const AssetReference& r) override
            {
                if (r.guid.IsValid() || r.fileID == 0)
                    return 0;
                if (const EntityId* id = objects.Find(r.fileID))
                    return EntityId::ToULong(*id);
                if (skipped.Find(r.fileID))
                    return 0;
                errors->Push(Format("a reference to fileID {}, which the file does not have", r.fileID));
                return 0;
            }
        };

        class Builder final : public SceneBuilder
        {
        public:
            Builder(u32 slot, Array<String>& errors, const SceneBuildOptions& options)
                : slot_(slot), errors_(errors), scene_(TransformAccess::NewRootScene()), options_(options)
            {
                // Every root made from now on comes after this one.
                for (TransformHierarchy* h = FirstRootHierarchy(slot); h; h = h->nextRoot)
                    lastRoot_ = h;
                table_.errors = &errors;
                TransformAccess::NewRootScene() = slot;
                detail::gTransformLinks = &links_;
            }

            ~Builder() override
            {
                detail::gTransformLinks = nullptr;
                TransformAccess::NewRootScene() = scene_;
            }

            Object* Create(i32 classId, i64 fileId, u32 line) override
            {
                u32 klass = FindClassById(classId);
                if (klass == kNoClass)
                {
                    Warning(Format("line {}: class ID {} is not known; the object is skipped", line, classId));
                    table_.skipped.Set(fileId, u8(1));
                    return nullptr;
                }
                const ClassInfo& info = GetClass(klass);
                if (!info.create || !info.readYaml)
                {
                    Error(Format("line {}: {} objects cannot be made", line, info.name));
                    return nullptr;
                }
                if (fileId == 0 || table_.objects.Find(fileId))
                {
                    Error(Format("line {}: fileID {} is used twice or 0", line, fileId));
                    return nullptr;
                }
                Object* o = NewObjectOfClass(klass);
                if (options_.persistent)
                    o->SetPersistent(true);
                table_.objects.Set(fileId, o->GetEntityId());
                fileIds_.Push(fileId);
                created_.Push(o->GetEntityId());
                return o;
            }

            ReferenceTable& References() override { return table_; }
            void Reserve(u32 objects) override
            {
                table_.objects.Reserve(objects);
                fileIds_.Reserve(objects);
                created_.Reserve(objects);
                links_.Reserve(objects / 2);
            }
            void SetRoots(SceneRoots& roots) override { roots_ = Move(roots.m_Roots); }

            void AddStripped(i64 fileId, const AssetReference& source, i64 instanceFileId) override
            {
                stripped_.Push({fileId, source, instanceFileId});
            }

            void AddPrefabInstance(i64 fileId, PrefabInstanceData& instance, u32 line) override
            {
                Instance& in = instances_.Emplace();
                in.fileId = fileId;
                in.line = line;
                in.data = Move(instance);
            }

            void ResolvePrefabInstances() override
            {
                HashMap<EntityId, i64>* assetIds = SceneAccess::FileIds(kAssetSlot);
                // The copies: their objects get fileIDs by the instance rule (stripped placeholders name them too).
                for (Instance& in : instances_)
                {
                    GameObject* source = FindPrefab(in.data.m_SourcePrefab.guid);
                    char guid[33];
                    in.data.m_SourcePrefab.guid.ToChars(guid);
                    if (!source)
                    {
                        Error(Format("line {}: the prefab {} of a prefab instance is not loaded", in.line, StringView(guid, 32)));
                        continue;
                    }
                    Array<Object*> originals, clones;
                    if (!CloneAccess::Copy(source, slot_, originals, clones))
                    {
                        Error(Format("line {}: the prefab {} could not be copied", in.line, StringView(guid, 32)));
                        continue;
                    }
                    in.resolved = true;
                    in.bySource.Reserve(originals.Size());
                    for (u32 k = 0; k < clones.Size(); k++)
                    {
                        Object* c = clones[k];
                        if (options_.persistent)
                            c->SetPersistent(true);
                        if (c->ClassIndex() == ClassOf<GameObject>())
                            GameObjectAccess::ActiveInHierarchy(*static_cast<GameObject*>(c)) = false;
                        const i64* f = assetIds->Find(originals[k]->GetEntityId());
                        if (!f)
                            continue;
                        in.bySource.Set(*f, c->GetEntityId());
                        i64 id = InstanceObjectFileId(in.fileId, *f);
                        if (!table_.objects.Find(id))
                            table_.objects.Set(id, c->GetEntityId());
                        created_.Push(c->GetEntityId());
                        fileIds_.Push(id);
                    }
                    in.root = static_cast<GameObject*>(clones[0])->transform()->GetEntityId();
                }
                for (const Stripped& s : stripped_)
                {
                    Instance* in = FindInstance(s.instance);
                    const EntityId* id = in ? in->bySource.Find(s.source.fileID) : nullptr;
                    if (id)
                        table_.objects.Set(s.fileId, *id);
                    else
                        table_.skipped.Set(s.fileId, u8(1));
                }
                // Then the changes, when every copy exists (modifications may refer to objects of other instances).
                for (Instance& in : instances_)
                {
                    if (!in.resolved)
                        continue;
                    PrefabModification& m = in.data.m_Modification;
                    for (const PropertyModification& p : m.m_Modifications)
                    {
                        Object* o = SourceObject(in, p.target);
                        if (!o)
                        {
                            Warning(Format("line {}: a modification of {} is for fileID {}, which the prefab does not have", in.line, p.propertyPath, p.target.fileID));
                            continue;
                        }
                        String error;
                        if (!o->Class().applyProperty(*o, p.propertyPath, p.value, p.objectReference, &table_, error))
                            Warning(Format("line {}: {}", in.line, error));
                    }
                    for (const AssetReference& r : m.m_RemovedComponents)
                        if (Object* o = SourceObject(in, r); o && (o->Class().flags & ClassComponent) && !o->Is<Transform>())
                            DestroyImmediate(o, true);
                    for (const AssetReference& r : m.m_RemovedGameObjects)
                    {
                        Object* o = SourceObject(in, r);
                        if (o && o->Is<Transform>())
                            o = static_cast<Transform*>(o)->gameObject();
                        if (o && o->ClassIndex() == ClassOf<GameObject>())
                            DestroyImmediate(o, true);
                    }
                    for (const AddedObject& a : m.m_AddedComponents)
                        added_.Push({a.addedObject.fileID, SourceId(in, a.targetCorrespondingSourceObject), a.insertIndex, false});
                    for (const AddedObject& a : m.m_AddedGameObjects)
                        added_.Push({a.addedObject.fileID, SourceId(in, a.targetCorrespondingSourceObject), a.insertIndex, true});
                }
            }

            void Roots(Array<EntityId>& out) const { out = newRoots_; }
            void Error(StringView message) override { errors_.Push(String(message)); }
            void Warning(StringView message) override { HX_LOG_WARNING(SceneFileLog, "Scene file: {}", message); }

            // The checks, the transform blocks and the enabling.
            void Finish()
            {
                detail::gTransformLinks = nullptr;
                AttachAddedComponents();
                CheckGameObjects();
                Link();
                PlaceInstances();
                OrderRoots();
                Remember();
                Enable();
            }

        private:
            struct Stripped
            {
                i64 fileId;
                AssetReference source;
                i64 instance;
            };

            struct Instance
            {
                i64 fileId = 0;
                u32 line = 0;
                PrefabInstanceData data;
                // fileIDs of the source prefab's objects to their copies.
                HashMap<i64, EntityId> bySource = HashMap<i64, EntityId>();
                EntityId root;
                bool resolved = false;
            };

            // An object of the file added to an object of an instance (a component, or a GameObject as a child).
            struct Added
            {
                i64 fileId;
                EntityId target;
                i32 insertIndex;
                bool gameObject;
            };

            Instance* FindInstance(i64 fileId)
            {
                for (Instance& in : instances_)
                    if (in.fileId == fileId)
                        return &in;
                return nullptr;
            }

            EntityId SourceId(Instance& in, const AssetReference& r)
            {
                const EntityId* id = in.bySource.Find(r.fileID);
                return id ? *id : EntityId();
            }

            Object* SourceObject(Instance& in, const AssetReference& r) { return EntityIdToObject(SourceId(in, r)); }

            Object* FileObject(i64 fileId)
            {
                const EntityId* id = table_.objects.Find(fileId);
                return id ? EntityIdToObject(*id) : nullptr;
            }

            static Transform* TransformOf(Object* o)
            {
                if (!o)
                    return nullptr;
                if (o->Is<Transform>())
                    return static_cast<Transform*>(o);
                return o->ClassIndex() == ClassOf<GameObject>() ? static_cast<GameObject*>(o)->transform() : nullptr;
            }

            // Components added to objects of instances, at their places (never before the Transform).
            void AttachAddedComponents()
            {
                for (const Added& a : added_)
                {
                    if (a.gameObject)
                        continue;
                    Object* o = FileObject(a.fileId);
                    GameObject* g = EntityIdToObject<GameObject>(a.target);
                    if (!o || !g || !(o->Class().flags & ClassComponent))
                        continue;
                    Component* c = static_cast<Component*>(o);
                    if (c->gameObject() != g || g->GetComponentIndex(c) >= 0)
                        continue;
                    SmallArray<GameObject::ComponentEntry, 4>& list = GameObjectAccess::Components(*g);
                    u32 at = a.insertIndex < 1 || u32(a.insertIndex) > list.Size() ? list.Size() : u32(a.insertIndex);
                    list.Insert(at, {PPtr<Component>(c), c->ClassIndex()});
                }
            }

            // Instance roots under their m_TransformParent; then the children of the file's parents in the order their
            // m_Children give.
            void PlaceInstances()
            {
                Array<EntityId> parents;
                for (Instance& in : instances_)
                {
                    Transform* root = EntityIdToObject<Transform>(in.root);
                    i64 parentId = in.data.m_Modification.m_TransformParent.fileID;
                    if (!root || parentId == 0)
                        continue;
                    Transform* parent = TransformOf(FileObject(parentId));
                    if (!parent || parent->IsChildOf(root))
                    {
                        Error(Format("line {}: the parent of a prefab instance (fileID {}) is not in the file", in.line, parentId));
                        continue;
                    }
                    TransformAccess::Place(root, parent, ~0u);
                    TransformAccess::MarkChanged(root->Hierarchy(), root->NodeIndex());
                    parents.Push(parent->GetEntityId());
                }
                if (parents.Empty())
                    return;
                for (const TransformLinks& l : links_)
                {
                    bool wanted = false;
                    for (EntityId p : parents)
                        wanted = wanted || p == l.transform->GetEntityId();
                    if (!wanted)
                        continue;
                    i32 k = 0;
                    for (const PPtr<Transform>& c : l.children)
                        if (Transform* child = c.Get(); child && child->parent() == l.transform)
                            child->SetSiblingIndex(k++);
                }
            }

            // The roots made here, those SceneRoots names first in its order, then the others as they came.
            void OrderRoots()
            {
                Array<TransformHierarchy*> mine;
                for (TransformHierarchy* h = lastRoot_ ? lastRoot_->nextRoot : FirstRootHierarchy(slot_); h; h = h->nextRoot)
                    mine.Push(h);
                HashMap<TransformHierarchy*, u8> taken;
                Array<TransformHierarchy*> order;
                for (const PPtr<Transform>& r : roots_)
                    if (Transform* t = r.Get(); t && !t->parent())
                    {
                        TransformHierarchy* h = &t->Hierarchy();
                        bool isMine = false;
                        for (TransformHierarchy* m : mine)
                            isMine = isMine || m == h;
                        if (isMine && !taken.Find(h))
                        {
                            taken.Set(h, u8(1));
                            order.Push(h);
                        }
                    }
                for (TransformHierarchy* h : mine)
                    if (!taken.Find(h))
                        order.Push(h);
                if (!roots_.Empty())
                    for (TransformHierarchy* h : order)
                        TransformAccess::MoveToScene(*h, slot_);
                newRoots_.Clear();
                for (TransformHierarchy* h : order)
                    if (Transform* t = h->transforms[0])
                        if (GameObject* g = t->gameObject())
                            newRoots_.Push(g->GetEntityId());
            }

            void CheckGameObjects()
            {
                u32 transformClass = ClassOf<Transform>();
                u32 listed = 0, components = 0;
                for (EntityId id : created_)
                {
                    Object* o = EntityIdToObject(id);
                    if (o && (o->Class().flags & ClassComponent))
                        components++;
                    GameObject* g = o && o->ClassIndex() == ClassOf<GameObject>() ? static_cast<GameObject*>(o) : nullptr;
                    if (!g)
                        continue;
                    SmallArray<GameObject::ComponentEntry, 4>& list = GameObjectAccess::Components(*g);
                    for (u32 i = 0; i < list.Size();)
                    {
                        Component* c = list[i].component.Get();
                        if (!c || c->gameObject() != g)
                        {
                            if (c)
                                Error(Format("{} lists a component that belongs to another GameObject", g->Name()));
                            list.RemoveAt(i);
                            continue;
                        }
                        list[i].klass = c->ClassIndex();
                        i++;
                    }
                    listed += list.Size();
                    // Transform first, exactly one.
                    u32 found = ~0u;
                    for (u32 i = 0; i < list.Size(); i++)
                        if (IsDerivedFrom(list[i].klass, transformClass))
                        {
                            if (found == ~0u)
                                found = i;
                            else
                                Error(Format("{} has a second Transform", g->Name()));
                        }
                    if (found == ~0u)
                    {
                        Error(Format("{} has no Transform; it gets one", g->Name()));
                        Component* t = static_cast<Component*>(NewObjectOfClass(transformClass));
                        GameObjectAccess::SetGameObject(*t, g);
                        list.Insert(0, {PPtr<Component>(t), transformClass});
                        components++;
                        listed++;
                    }
                    else if (found != 0)
                    {
                        Error(Format("the Transform of {} is not its first component", g->Name()));
                        GameObject::ComponentEntry e = list[found];
                        list.RemoveAt(found);
                        list.Insert(0, e);
                    }
                }
                // Components no GameObject lists go (each GameObject lists only its own: equal counts say none).
                if (listed == components)
                    return;
                for (EntityId id : created_)
                {
                    Object* o = EntityIdToObject(id);
                    if (!o || !(o->Class().flags & ClassComponent))
                        continue;
                    Component* c = static_cast<Component*>(o);
                    GameObject* g = c->gameObject();
                    if (g && g->GetComponentIndex(c) >= 0)
                        continue;
                    // A component of an object of a prefab instance that m_AddedComponents does not name.
                    if (g && !o->Is<Transform>())
                    {
                        GameObjectAccess::Components(*g).Push({PPtr<Component>(c), c->ClassIndex()});
                        continue;
                    }
                    Error(Format("a {} belongs to no GameObject that lists it; it is removed", o->Class().name));
                    if (o->Is<Transform>())
                    {
                        for (usize i = 0; i < links_.Size(); i++)
                            if (links_[i].transform == o)
                            {
                                links_.RemoveAt(i);
                                break;
                            }
                        DeleteObject(o);
                    }
                    else
                        DestroyImmediate(o, true);
                }
            }

            // Each root's block in one go, from the parents and children the Transforms read.
            void Link()
            {
                HashMap<EntityId, u32> index;
                index.Reserve(links_.Size());
                for (u32 i = 0; i < links_.Size(); i++)
                    index.Set(links_[i].transform->GetEntityId(), i);
                Array<u8> visited;
                visited.Resize(links_.Size(), u8(0));
                Array<u32> roots;
                for (const PPtr<Transform>& r : roots_)
                    if (const u32* i = index.Find(r.Id()))
                        if (!links_[*i].father.Get() && !visited[*i])
                        {
                            roots.Push(*i);
                            visited[*i] = 1;
                        }
                Array<u32> attached;
                for (u32 i = 0; i < links_.Size(); i++)
                {
                    if (visited[i])
                        continue;
                    Transform* father = links_[i].father.Get();
                    if (father && index.Find(father->GetEntityId()))
                        continue;
                    roots.Push(i);
                    visited[i] = 1;
                    if (father)
                        attached.Push(i);
                }
                Array<Transform*> nodes;
                Array<i32> parents;
                struct Item
                {
                    u32 link;
                    i32 parent;
                };
                Array<Item> stack;
                for (u32 root : roots)
                {
                    nodes.Clear();
                    parents.Clear();
                    stack.Push({root, -1});
                    while (!stack.Empty())
                    {
                        Item it = stack.Back();
                        stack.Pop();
                        i32 at = i32(nodes.Size());
                        nodes.Push(links_[it.link].transform);
                        parents.Push(it.parent);
                        Array<PPtr<Transform>>& children = links_[it.link].children;
                        // Pushed backwards: the first child comes out first.
                        for (usize k = children.Size(); k-- > 0;)
                        {
                            Transform* c = children[k].Get();
                            const u32* ci = c ? index.Find(c->GetEntityId()) : nullptr;
                            if (!ci)
                                continue;
                            if (visited[*ci] || links_[*ci].father.Get() != links_[it.link].transform)
                            {
                                Error(Format("{} names {} as a child, which has another parent or was named before",
                                             links_[it.link].transform->Name(), c->Name()));
                                continue;
                            }
                            visited[*ci] = 1;
                            stack.Push({*ci, at});
                        }
                    }
                    TransformAccess::BuildLoaded(Span<Transform* const>(nodes.Data(), nodes.Size()), Span<const i32>(parents.Data(), parents.Size()), slot_);
                }
                for (u32 i : attached)
                {
                    Transform* t = links_[i].transform;
                    Transform* father = links_[i].father.Get();
                    if (!father || father->IsChildOf(t))
                        continue;
                    u32 at = ~0u;
                    for (const Added& a : added_)
                        if (a.gameObject && TransformOf(FileObject(a.fileId)) == t && a.insertIndex >= 0)
                            at = u32(a.insertIndex);
                    TransformAccess::Place(t, father, at);
                    TransformAccess::MarkChanged(t->Hierarchy(), t->NodeIndex());
                }
                // Transforms no root reaches (parents that name them not, or cycles) stay roots of their own.
                for (u32 i = 0; i < links_.Size(); i++)
                    if (!visited[i])
                        Error(Format("{} is not reached from a root; it stays a root of its own", links_[i].transform->Name()));
            }

            void Remember()
            {
                if (HashMap<EntityId, i64>* known = SceneAccess::FileIds(slot_))
                {
                    known->Reserve(known->Size() + created_.Size());
                    for (u32 i = 0; i < created_.Size(); i++)
                        if (EntityIdIsValid(created_[i]))
                            known->Set(created_[i], fileIds_[i]);
                }
            }

            // Inactive first, then enabled through the normal path: the roots made here (all after the scene's roots
            // from before) in order, parents first.
            void Enable()
            {
                u32 gameObjectClass = ClassOf<GameObject>();
                for (EntityId id : created_)
                    if (Object* o = EntityIdToObject(id); o && o->ClassIndex() == gameObjectClass)
                        GameObjectAccess::ActiveInHierarchy(*static_cast<GameObject*>(o)) = false;
                // Assets are never enabled.
                if (!options_.enable)
                    return;
                Array<EntityId> roots;
                for (TransformHierarchy* h = lastRoot_ ? lastRoot_->nextRoot : FirstRootHierarchy(slot_); h; h = h->nextRoot)
                    if (Transform* t = h->transforms[0])
                        if (GameObject* g = t->gameObject())
                            roots.Push(g->GetEntityId());
                // By id: enable hooks may destroy objects.
                for (EntityId id : roots)
                    if (GameObject* g = EntityIdToObject<GameObject>(id))
                        GameObjectAccess::UpdateActive(g);
            }

            u32 slot_;
            TransformHierarchy* lastRoot_ = nullptr;
            Array<String>& errors_;
            u32 scene_;
            ReadTable table_;
            Array<TransformLinks> links_;
            Array<PPtr<Transform>> roots_;
            Array<EntityId> created_;
            Array<i64> fileIds_;
            SceneBuildOptions options_;
            Array<Stripped> stripped_;
            Array<Instance> instances_;
            Array<Added> added_;
            Array<EntityId> newRoots_;
        };

        // ------------------------------------------------------------------------------------------------ YAML

        // The links of a stripped placeholder: the source object and the instance it belongs to.
        struct StrippedLinks
        {
            [[maybe_unused]] static constexpr StringView kTypeName = "Stripped";
            AssetReference m_CorrespondingSourceObject;
            AssetReference m_PrefabInstance;
            template <class V>
            void Transfer(V& v)
            {
                v.Field("m_CorrespondingSourceObject", m_CorrespondingSourceObject);
                v.Field("m_PrefabInstance", m_PrefabInstance);
            }
        };

        class ParsedYaml final : public ParsedScene
        {
        public:
            yaml::Tree tree;
        };

        class YamlFormat final : public SceneFormat
        {
        public:
            StringView Name() const override { return "YAML"; }

            bool Probe(Span<const u8> bytes) const override
            {
                // Text: the directives, a document marker or a comment first.
                usize i = 0;
                if (bytes.Size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
                    i = 3;
                while (i < bytes.Size() && (bytes[i] == ' ' || bytes[i] == '\t' || bytes[i] == '\r' || bytes[i] == '\n'))
                    i++;
                return i < bytes.Size() && (bytes[i] == '%' || bytes[i] == '-' || bytes[i] == '#');
            }

            ParsedScene* Parse(Span<const u8> bytes, String& error) const override
            {
                ParsedYaml* p = new ParsedYaml();
                if (yaml::Parse(StringView(reinterpret_cast<const char*>(bytes.Data()), bytes.Size()), p->tree, error))
                    return p;
                delete p;
                return nullptr;
            }

            void Build(ParsedScene& parsed, SceneBuilder& builder) const override
            {
                const yaml::Tree& tree = static_cast<ParsedYaml&>(parsed).tree;
                struct Made
                {
                    Object* object;
                    u32 root;
                };
                Array<Made> made;
                made.Reserve(tree.documents.Size());
                builder.Reserve(u32(tree.documents.Size()));
                u32 rootsDocument = yaml::kNoNode;
                for (const yaml::Document& d : tree.documents)
                {
                    if (d.classID == kSceneRootsClassId)
                    {
                        rootsDocument = d.root;
                        continue;
                    }
                    if (d.stripped)
                    {
                        StrippedLinks links;
                        if (tree.IsMapping(d.root) && tree.Count(d.root) == 1)
                        {
                            YamlRead r(tree);
                            r.Value(tree.Value(d.root, 0), "stripped", links);
                        }
                        builder.AddStripped(d.fileID, links.m_CorrespondingSourceObject, links.m_PrefabInstance.fileID);
                        continue;
                    }
                    if (d.classID == kPrefabInstanceClassId)
                    {
                        PrefabInstanceData instance;
                        Array<String> errors;
                        if (ReadYaml(tree, d.root, instance, errors))
                            builder.AddPrefabInstance(d.fileID, instance, d.line);
                        for (const String& e : errors)
                            builder.Error(e);
                        continue;
                    }
                    if (d.classID < 0 || d.classID > 0x7fffffff)
                    {
                        builder.Error(Format("line {}: a document without a class ID", d.line));
                        continue;
                    }
                    if (Object* o = builder.Create(i32(d.classID), d.fileID, d.line))
                        made.Push({o, d.root});
                }
                builder.ResolvePrefabInstances();
                Array<String> errors;
                for (const Made& m : made)
                    m.object->Class().readYaml(*m.object, tree, m.root, errors, &builder.References());
                if (rootsDocument != yaml::kNoNode)
                {
                    SceneRoots roots;
                    ReadYaml(tree, rootsDocument, roots, errors, &builder.References());
                    builder.SetRoots(roots);
                }
                for (const String& e : errors)
                    builder.Error(e);
            }
        };

        // ------------------------------------------------------------------------------------------------ binary

        class ParsedBinary final : public ParsedScene
        {
        public:
            SerializedFile file;
        };

        class BinaryFormat final : public SceneFormat
        {
        public:
            StringView Name() const override { return "SerializedFile"; }

            bool Probe(Span<const u8> bytes) const override { return bytes.Size() >= 4 && std::memcmp(bytes.Data(), "HXSF", 4) == 0; }

            ParsedScene* Parse(Span<const u8> bytes, String& error) const override
            {
                ParsedBinary* p = new ParsedBinary();
                if (p->file.Open(bytes, error))
                    return p;
                delete p;
                return nullptr;
            }

            void Build(ParsedScene& parsed, SceneBuilder& builder) const override
            {
                const SerializedFile& f = static_cast<ParsedBinary&>(parsed).file;
                Array<Object*> made;
                Array<u32> at;
                made.Reserve(f.Objects().Size());
                at.Reserve(f.Objects().Size());
                builder.Reserve(u32(f.Objects().Size()));
                u32 rootsObject = kNoObject;
                for (u32 i = 0; i < f.Objects().Size(); i++)
                {
                    const SerializedObject& o = f.Objects()[i];
                    i32 classId = f.Types()[o.type].classID;
                    if (classId == kSceneRootsClassId)
                    {
                        rootsObject = i;
                        continue;
                    }
                    if (Object* obj = builder.Create(classId, o.fileID, 0))
                    {
                        made.Push(obj);
                        at.Push(i);
                    }
                }
                builder.ResolvePrefabInstances();
                for (u32 k = 0; k < made.Size(); k++)
                {
                    String error;
                    if (!made[k]->Class().readBinary(*made[k], f, at[k], error, &builder.References()))
                        builder.Error(Format("fileID {}: {}", f.Objects()[at[k]].fileID, error));
                }
                if (rootsObject != kNoObject)
                {
                    SceneRoots roots;
                    String error;
                    if (f.Read(rootsObject, roots, error, &builder.References()))
                        builder.SetRoots(roots);
                    else
                        builder.Error(Format("SceneRoots: {}", error));
                }
            }
        };

        YamlFormat gYaml;
        BinaryFormat gBinary;

        Array<SceneFormat*>& Formats()
        {
            static Array<SceneFormat*> formats = {&gYaml, &gBinary};
            return formats;
        }
    }

    void RegisterSceneFormat(SceneFormat* format)
    {
        if (format)
            Formats().Push(format);
    }

    const SceneFormat* FindSceneFormat(Span<const u8> bytes)
    {
        for (SceneFormat* f : Formats())
            if (f->Probe(bytes))
                return f;
        return nullptr;
    }

    bool SceneAccess::Build(const SceneFormat& format, ParsedScene& parsed, u32 slot, Array<String>& errors, const SceneBuildOptions& options,
                            Array<EntityId>* roots)
    {
        usize before = errors.Size();
        {
            Builder b(slot, errors, options);
            format.Build(parsed, b);
            b.Finish();
            if (roots)
                b.Roots(*roots);
        }
        return errors.Size() == before;
    }

    bool ReadSceneFile(Span<const u8> bytes, Scene scene, Array<String>& errors)
    {
        if (!scene.isLoaded())
        {
            errors.Push("the scene is not loaded");
            return false;
        }
        const SceneFormat* format = FindSceneFormat(bytes);
        if (!format)
        {
            errors.Push("no scene format reads the bytes");
            return false;
        }
        String error;
        ParsedScene* parsed = format->Parse(bytes, error);
        if (!parsed)
        {
            errors.Push(error);
            return false;
        }
        bool ok = SceneAccess::Build(*format, *parsed, scene.Slot(), errors);
        delete parsed;
        return ok;
    }

    bool WriteSceneYaml(Scene scene, String& out, String& error)
    {
        if (!scene.IsValid())
            return error = "the scene is not valid", false;
        Collected c;
        Collect(scene.Slot(), c);
        WriteTable table;
        Array<i64> fileIds;
        if (!AssignFileIds(scene.Slot(), c, table, fileIds, error))
            return false;
        out.Clear();
        yaml::Emitter e(out);
        e.Directives();
        for (usize i = 0; i < c.objects.Size(); i++)
        {
            Object& o = *c.objects[i];
            const ClassInfo& info = o.Class();
            e.BeginDocument(info.classId, fileIds[i]);
            info.writeYaml(o, e, &table);
        }
        SceneRoots roots = RootsOf(c);
        e.BeginDocument(kSceneRootsClassId, kSceneRootsFileId);
        WriteYaml(e, roots, &table);
        return true;
    }

    bool WriteSceneBinary(Scene scene, Array<u8>& out, String& error, bool typeTrees)
    {
        if (!scene.IsValid())
            return error = "the scene is not valid", false;
        Collected c;
        Collect(scene.Slot(), c);
        WriteTable table;
        Array<i64> fileIds;
        if (!AssignFileIds(scene.Slot(), c, table, fileIds, error))
            return false;
        SerializedFileWriter w(typeTrees);
        for (usize i = 0; i < c.objects.Size(); i++)
        {
            Object& o = *c.objects[i];
            o.Class().writeBinary(o, w, fileIds[i], &table);
        }
        SceneRoots roots = RootsOf(c);
        w.Add(kSceneRootsFileId, kSceneRootsClassId, roots, &table);
        out.Clear();
        return w.Finish(out, error);
    }
}
