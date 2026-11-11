#include <Support/TestClasses.h>

#include <Helix/Engine/Pool/ObjectPool.h>
#include <Helix/Engine/Prefab/Prefab.h>
#include <Helix/Engine/Scene/SceneFile.h>
#include <Helix/Engine/Scene/SceneFormat.h>
#include <Helix/Testing/Test.h>

using namespace helix;
using namespace helix::engine;
using namespace helix::engine::testing;

namespace
{
    constexpr const char* kGuidA = "a0000000000000000000000000000001";
    constexpr const char* kGuidB = "b0000000000000000000000000000002";
    constexpr const char* kGuidV = "c0000000000000000000000000000003";

    Guid G(const char* text)
    {
        Guid g;
        Guid::TryParse(text, g);
        return g;
    }

    // Crate (Logger, Linker -> Lid) with the children Lid (Single) and Label.
    const char* kPrefabA = R"(%YAML 1.1
%TAG !h! tag:helixengine,2026:
--- !h!1 &100
GameObject:
  m_ObjectHideFlags: 0
  m_CorrespondingSourceObject: {fileID: 0}
  m_PrefabInstance: {fileID: 0}
  m_PrefabAsset: {fileID: 0}
  m_Component:
  - component: {fileID: 101}
  - component: {fileID: 102}
  - component: {fileID: 103}
  m_Name: Crate
  m_IsActive: 1
--- !h!4 &101
Transform:
  m_GameObject: {fileID: 100}
  m_Children:
  - {fileID: 201}
  - {fileID: 301}
  m_Father: {fileID: 0}
--- !h!2000001 &102
Logger:
  m_GameObject: {fileID: 100}
  m_Enabled: 1
--- !h!2000002 &103
Linker:
  m_GameObject: {fileID: 100}
  target: {fileID: 200}
  list: []
  value: 7
--- !h!1 &200
GameObject:
  m_Component:
  - component: {fileID: 201}
  - component: {fileID: 202}
  m_Name: Lid
  m_IsActive: 1
--- !h!4 &201
Transform:
  m_GameObject: {fileID: 200}
  m_LocalPosition: {x: 0, y: 1, z: 0}
  m_Children: []
  m_Father: {fileID: 101}
--- !h!2000003 &202
Single:
  m_GameObject: {fileID: 200}
--- !h!1 &300
GameObject:
  m_Component:
  - component: {fileID: 301}
  m_Name: Label
  m_IsActive: 1
--- !h!4 &301
Transform:
  m_GameObject: {fileID: 300}
  m_Children: []
  m_Father: {fileID: 101}
)";

    GameObject* LoadA()
    {
        Array<String> errors;
        GameObject* a = LoadPrefab(StringView(kPrefabA), errors, G(kGuidA));
        for (const String& e : errors)
            std::printf("    %.*s\n", int(e.Size()), e.Data());
        return a;
    }

    // The text with $G replaced by the guid and $N by the number.
    String Fill(const char* text, StringView guid, i64 number = 0)
    {
        String out;
        String n = Format("{}", number);
        for (const char* c = text; *c; c++)
        {
            if (c[0] == '$' && c[1] == 'G')
                out.Append(guid), c++;
            else if (c[0] == '$' && c[1] == 'N')
                out.Append(n), c++;
            else
                out.Append(*c);
        }
        return out;
    }

    Scene Fresh(StringView name)
    {
        Scene s = SceneManager::CreateScene(name);
        SceneManager::SetActiveScene(s);
        return s;
    }

    void Done(Scene previous, Scene s)
    {
        SceneManager::SetActiveScene(previous);
        if (s.IsValid())
            SceneManager::UnloadSceneAsync(s);
        SceneManager::ProcessSceneLoads();
        Cleanup();
    }
}

HX_TEST(Prefab, AssetsLiveOutsideScenesAndCopyIntoThem)
{
    Events().Clear();
    GameObject* a = LoadA();
    HX_REQUIRE(a != nullptr);
    // An asset: persistent, in no scene, never enabled, not found among scene objects.
    HX_CHECK(a->IsPersistent() && !a->scene().IsValid() && !a->activeInHierarchy() && Events().Empty());
    HX_CHECK(FindPrefab(G(kGuidA)) == a && a->transform()->childCount() == 2u && a->GetComponent<Linker>()->target.Get() == a->transform()->GetChild(0)->gameObject());
    Array<GameObject*> found;
    FindObjectsByType(found);
    bool listed = false;
    for (GameObject* g : found)
        listed = listed || g == a;
    HX_CHECK(!listed);
    // Copies go into the active scene, enabled, their references into the copy.
    Scene start = SceneManager::GetActiveScene();
    for (int i = 0; i < 2; i++)
    {
        Events().Clear();
        GameObject* c = Instantiate(a);
        HX_REQUIRE(c != nullptr);
        HX_CHECK(!c->IsPersistent() && c->scene() == start && c->Name() == StringView("Crate(Clone)") && HasEvents({"Crate(Clone):enable"}));
        HX_CHECK(c->GetComponent<Linker>()->target.Get() == c->transform()->GetChild(0)->gameObject() && c->GetComponent<Linker>()->value == 7);
        HX_CHECK(c->transform()->GetChild(0)->localPosition() == Vector3(0, 1, 0) && c->transform()->GetChild(1)->Name() == StringView("Label"));
    }
    // Many at once through the batch API.
    EntityId ids[5], transforms[5];
    HX_CHECK(InstantiateGameObjects(a->GetEntityId(), 5, Span<EntityId>(ids, 5), Span<EntityId>(transforms, 5)));
    HX_CHECK(EntityIdToObject<GameObject>(ids[4])->transform()->GetEntityId() == transforms[4]);
    // Unloading the prefab leaves its copies.
    UnloadPrefab(a);
    HX_CHECK(!FindPrefab(G(kGuidA)) && EntityIdIsValid(ids[0]));
    Cleanup();
}

HX_TEST(Prefab, InstancesInSceneFilesTakeTheirChanges)
{
    HX_REQUIRE(LoadA());
    Scene start = SceneManager::GetActiveScene();
    Scene s = Fresh("WithInstance");
    String file = Fill(R"(%YAML 1.1
%TAG !h! tag:helixengine,2026:
--- !h!1 &1
GameObject:
  m_Component:
  - component: {fileID: 2}
  m_Name: Holder
  m_IsActive: 1
--- !h!4 &2
Transform:
  m_GameObject: {fileID: 1}
  m_Children:
  - {fileID: 9001}
  m_Father: {fileID: 0}
--- !h!1001 &5000
PrefabInstance:
  m_ObjectHideFlags: 0
  serializedVersion: 2
  m_Modification:
    serializedVersion: 3
    m_TransformParent: {fileID: 2}
    m_Modifications:
    - target: {fileID: 100, guid: $G, type: 3}
      propertyPath: m_Name
      value: Big Crate
      objectReference: {fileID: 0}
    - target: {fileID: 201, guid: $G, type: 3}
      propertyPath: m_LocalPosition.y
      value: 2.5
      objectReference: {fileID: 0}
    - target: {fileID: 103, guid: $G, type: 3}
      propertyPath: value
      value: 42
      objectReference: {fileID: 0}
    - target: {fileID: 103, guid: $G, type: 3}
      propertyPath: target
      value: ''
      objectReference: {fileID: 1}
    m_RemovedComponents:
    - {fileID: 202, guid: $G, type: 3}
    m_RemovedGameObjects:
    - {fileID: 300, guid: $G, type: 3}
    m_AddedGameObjects:
    - targetCorrespondingSourceObject: {fileID: 201, guid: $G, type: 3}
      insertIndex: -1
      addedObject: {fileID: 11}
    m_AddedComponents:
    - targetCorrespondingSourceObject: {fileID: 100, guid: $G, type: 3}
      insertIndex: -1
      addedObject: {fileID: 21}
  m_SourcePrefab: {fileID: 100100000, guid: $G, type: 3}
--- !h!4 &9001 stripped
Transform:
  m_CorrespondingSourceObject: {fileID: 101, guid: $G, type: 3}
  m_PrefabInstance: {fileID: 5000}
  m_PrefabAsset: {fileID: 0}
--- !h!4 &9002 stripped
Transform:
  m_CorrespondingSourceObject: {fileID: 201, guid: $G, type: 3}
  m_PrefabInstance: {fileID: 5000}
  m_PrefabAsset: {fileID: 0}
--- !h!1 &9003 stripped
GameObject:
  m_CorrespondingSourceObject: {fileID: 100, guid: $G, type: 3}
  m_PrefabInstance: {fileID: 5000}
  m_PrefabAsset: {fileID: 0}
--- !h!1 &10
GameObject:
  m_Component:
  - component: {fileID: 11}
  m_Name: Sticker
  m_IsActive: 1
--- !h!4 &11
Transform:
  m_GameObject: {fileID: 10}
  m_Children: []
  m_Father: {fileID: 9002}
--- !h!2000005 &21
SubLogger:
  m_GameObject: {fileID: 9003}
  m_Enabled: 1
--- !h!1660057539 &9223372036854775807
SceneRoots:
  m_Roots:
  - {fileID: 2}
)", kGuidA);
    Events().Clear();
    Array<String> errors;
    bool ok = ReadSceneFile(StringView(file), s, errors);
    for (const String& e : errors)
        std::printf("    %.*s\n", int(e.Size()), e.Data());
    HX_CHECK(ok);
    Array<GameObject*> roots = s.GetRootGameObjects();
    HX_REQUIRE(roots.Size() == 1u && roots[0]->Name() == StringView("Holder"));
    Transform* holder = roots[0]->transform();
    HX_REQUIRE(holder->childCount() == 1u);
    GameObject* crate = holder->GetChild(0)->gameObject();
    HX_CHECK(crate->Name() == StringView("Big Crate") && !crate->IsPersistent() && crate->scene() == s);
    // Label removed, Lid moved up with Sticker added under it, Single removed from Lid.
    HX_REQUIRE(crate->transform()->childCount() == 1u);
    Transform* lid = crate->transform()->GetChild(0);
    HX_CHECK(lid->Name() == StringView("Lid") && lid->localPosition() == Vector3(0, 2.5f, 0) && lid->gameObject()->GetComponentCount() == 1u);
    HX_CHECK(lid->childCount() == 1u && lid->GetChild(0)->Name() == StringView("Sticker"));
    // Linker changed, SubLogger added last; the enable hooks ran once, parents first.
    Linker* l = crate->GetComponent<Linker>();
    HX_CHECK(l->value == 42 && l->target.Get() == roots[0] && crate->GetComponentCount() == 4u && crate->GetComponentAtIndex(3)->Is<SubLogger>());
    HX_CHECK(HasEvents({"Big Crate:enable", "Big Crate:enable"}));
    // The instance objects have fileIDs by the instance rule: written again they appear under them.
    String text, error;
    HX_REQUIRE(WriteSceneYaml(s, text, error));
    HX_CHECK(text.View().Contains(Format("--- !h!1 &{}", InstanceObjectFileId(5000, 100))));
    HX_CHECK(text.View().Contains(Format("--- !h!4 &{}", InstanceObjectFileId(5000, 201))));
    Done(start, s);
}

HX_TEST(Prefab, NestedPrefabsAndVariants)
{
    HX_REQUIRE(LoadA());
    // B: a Pallet with an instance of A under it whose Lid is renamed.
    String b = Fill(R"(%YAML 1.1
%TAG !h! tag:helixengine,2026:
--- !h!1 &100
GameObject:
  m_Component:
  - component: {fileID: 101}
  m_Name: Pallet
  m_IsActive: 1
--- !h!4 &101
Transform:
  m_GameObject: {fileID: 100}
  m_Children:
  - {fileID: 7001}
  m_Father: {fileID: 0}
--- !h!1001 &7000
PrefabInstance:
  m_Modification:
    m_TransformParent: {fileID: 101}
    m_Modifications:
    - target: {fileID: 200, guid: $G, type: 3}
      propertyPath: m_Name
      value: InnerLid
      objectReference: {fileID: 0}
    m_RemovedComponents: []
    m_RemovedGameObjects: []
    m_AddedGameObjects: []
    m_AddedComponents: []
  m_SourcePrefab: {fileID: 100100000, guid: $G, type: 3}
--- !h!4 &7001 stripped
Transform:
  m_CorrespondingSourceObject: {fileID: 101, guid: $G, type: 3}
  m_PrefabInstance: {fileID: 7000}
)", kGuidA);
    Array<String> errors;
    GameObject* pallet = LoadPrefab(StringView(b), errors, G(kGuidB));
    HX_REQUIRE(pallet != nullptr && errors.Empty());
    HX_CHECK(pallet->transform()->GetChild(0)->Name() == StringView("Crate") && pallet->transform()->Find("Crate/InnerLid") != nullptr);
    HX_CHECK(pallet->transform()->GetChild(0)->gameObject()->IsPersistent());
    // An instance of B in a scene reaches the nested Lid through the composed fileID.
    Scene start = SceneManager::GetActiveScene();
    Scene s = Fresh("Nested");
    String scene = Fill(R"(%YAML 1.1
%TAG !h! tag:helixengine,2026:
--- !h!1001 &6000
PrefabInstance:
  m_Modification:
    m_TransformParent: {fileID: 0}
    m_Modifications:
    - target: {fileID: $N, guid: $G, type: 3}
      propertyPath: m_LocalPosition.x
      value: 3
      objectReference: {fileID: 0}
  m_SourcePrefab: {fileID: 100100000, guid: $G, type: 3}
)", kGuidB, InstanceObjectFileId(7000, 201));
    errors.Clear();
    HX_CHECK(ReadSceneFile(StringView(scene), s, errors) && errors.Empty());
    Array<GameObject*> roots = s.GetRootGameObjects();
    HX_REQUIRE(roots.Size() == 1u);
    Transform* inner = roots[0]->transform()->Find("Crate/InnerLid");
    HX_REQUIRE(inner != nullptr);
    HX_CHECK(inner->localPosition() == Vector3(3, 1, 0) && roots[0]->Name() == StringView("Pallet"));
    // A variant: an instance of A at the root with its own name.
    String v = Fill(R"(%YAML 1.1
%TAG !h! tag:helixengine,2026:
--- !h!1001 &8000
PrefabInstance:
  m_Modification:
    m_TransformParent: {fileID: 0}
    m_Modifications:
    - target: {fileID: 100, guid: $G, type: 3}
      propertyPath: m_Name
      value: Crate Variant
      objectReference: {fileID: 0}
  m_SourcePrefab: {fileID: 100100000, guid: $G, type: 3}
)", kGuidA);
    errors.Clear();
    GameObject* variant = LoadPrefab(StringView(v), errors, G(kGuidV));
    HX_REQUIRE(variant != nullptr && errors.Empty());
    HX_CHECK(variant->Name() == StringView("Crate Variant") && variant->transform()->childCount() == 2u && variant->IsPersistent());
    GameObject* copy = Instantiate(variant);
    HX_CHECK(copy->Name() == StringView("Crate Variant(Clone)") && copy->GetComponent<Linker>()->target.Get() == copy->transform()->GetChild(0)->gameObject());
    Done(start, s);
}

HX_TEST(Prefab, MissingSourcesAndBadChangesAreReported)
{
    Scene start = SceneManager::GetActiveScene();
    Scene s = Fresh("Missing");
    HX_REQUIRE(LoadA());
    String file = Fill(R"(%YAML 1.1
%TAG !h! tag:helixengine,2026:
--- !h!1001 &1
PrefabInstance:
  m_Modification:
    m_TransformParent: {fileID: 0}
  m_SourcePrefab: {fileID: 100100000, guid: dddddddddddddddddddddddddddddddd, type: 3}
--- !h!1001 &2
PrefabInstance:
  m_Modification:
    m_TransformParent: {fileID: 0}
    m_Modifications:
    - target: {fileID: 100, guid: $G, type: 3}
      propertyPath: m_NoSuchField
      value: 1
      objectReference: {fileID: 0}
    - target: {fileID: 999, guid: $G, type: 3}
      propertyPath: m_Name
      value: x
      objectReference: {fileID: 0}
  m_SourcePrefab: {fileID: 100100000, guid: $G, type: 3}
--- !h!1 &3
GameObject:
  m_Component:
  - component: {fileID: 4}
  m_Name: Plain
--- !h!4 &4
Transform:
  m_GameObject: {fileID: 3}
  m_Father: {fileID: 0}
)", kGuidA);
    Array<String> errors;
    HX_CHECK(!ReadSceneFile(StringView(file), s, errors) && errors.Size() == 1u);
    // The missing prefab is an error, the bad modifications only warnings; the rest loads.
    HX_CHECK(s.rootCount() == 2u);
    Done(start, s);
}

HX_TEST(Prefab, InstantiateAsyncWithinTheFrameBudget)
{
    GameObject* a = LoadA();
    HX_REQUIRE(a != nullptr);
    f32 before = AsyncInstantiateOperation::GetIntegrationTimeMS();
    // No budget: one copy a frame.
    AsyncInstantiateOperation::SetIntegrationTimeMS(0);
    Vector3 positions[3] = {Vector3(1, 0, 0), Vector3(2, 0, 0), Vector3(3, 0, 0)};
    AsyncInstantiateOperation op = InstantiateAsync(a, 7, Span<const Vector3>(positions, 3), {});
    int completed = 0;
    op.AddCompleted([](void* user, AsyncInstantiateOperation) { ++*static_cast<int*>(user); }, &completed);
    HX_CHECK(!op.isDone() && op.progress() == 0.0f);
    ProcessAsyncInstantiations();
    HX_CHECK(!op.isDone() && op.progress() > 0.1f && op.progress() < 0.2f);
    for (int i = 0; i < 10 && !op.isDone(); i++)
        ProcessAsyncInstantiations();
    Array<Object*> result;
    op.Result(result);
    HX_REQUIRE(op.isDone() && completed == 1 && result.Size() == 7u);
    HX_CHECK(static_cast<GameObject*>(result[4])->transform()->position() == Vector3(2, 0, 0));
    // Held by allowSceneActivation, then finished at once.
    AsyncInstantiateOperation held = InstantiateAsync(a, 3);
    held.SetAllowSceneActivation(false);
    ProcessAsyncInstantiations();
    HX_CHECK(held.IsWaitingForSceneActivation() && held.progress() == 0.9f);
    held.WaitForCompletion();
    held.Result(result);
    HX_CHECK(held.isDone() && result.Size() == 3u);
    // Cancel destroys what was made.
    AsyncInstantiateOperation cancelled = InstantiateAsync(a, 5);
    ProcessAsyncInstantiations();
    cancelled.Result(result);
    EntityId made = result.Empty() ? EntityId() : result[0]->GetEntityId();
    cancelled.Cancel();
    cancelled.Result(result);
    HX_CHECK(cancelled.isDone() && result.Empty() && !EntityIdIsValid(made));
    AsyncInstantiateOperation::SetIntegrationTimeMS(before);
    Cleanup();
}

HX_TEST(Prefab, PoolsReuseCopies)
{
    GameObject* a = LoadA();
    HX_REQUIRE(a != nullptr);
    struct Use
    {
        GameObject* prefab;
        int made = 0, destroyed = 0;
    } use{a};
    PoolCallbacks<GameObject*> cb;
    cb.user = &use;
    cb.create = [](void* u) -> GameObject* {
        Use& x = *static_cast<Use*>(u);
        x.made++;
        return Instantiate(x.prefab);
    };
    cb.onGet = [](void*, GameObject*& g) { g->SetActive(true); };
    cb.onRelease = [](void*, GameObject*& g) { g->SetActive(false); };
    cb.onDestroy = [](void* u, GameObject*& g) {
        static_cast<Use*>(u)->destroyed++;
        DestroyImmediate(g);
    };
    {
        ObjectPool<GameObject*> pool(cb, true, 4, 2);
        GameObject* x = pool.Get();
        GameObject* y = pool.Get();
        GameObject* z = pool.Get();
        HX_CHECK(use.made == 3 && pool.CountAll() == 3u && pool.CountActive() == 3u);
        pool.Release(x);
        pool.Release(x);
        pool.Release(y);
        pool.Release(z);
        // maxSize 2: the third release destroys.
        HX_CHECK(pool.CountInactive() == 2u && use.destroyed == 1 && !x->activeSelf());
        GameObject* again = pool.Get();
        HX_CHECK(use.made == 3 && again->activeSelf() && pool.CountActive() == 1u);
        pool.Release(again);
        pool.Clear();
        HX_CHECK(use.destroyed == 3 && pool.CountAll() == 0u);
    }
    {
        LinkedPool<GameObject*> pool(cb, true, 10);
        GameObject* x = pool.Get();
        pool.Release(x);
        HX_CHECK(pool.Get() == x && pool.CountAll() == 1u && pool.CountInactive() == 0u);
        pool.Release(x);
    }
    HX_CHECK(use.destroyed == 4);
    Cleanup();
}
