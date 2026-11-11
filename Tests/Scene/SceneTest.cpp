#include <Support/TestClasses.h>

#include <Helix/Engine/Scene/SceneFile.h>
#include <Helix/Jobs/Jobs.h>
#include <Helix/Testing/Test.h>

#include <chrono>
#include <thread>

using namespace helix;
using namespace helix::engine;
using namespace helix::engine::testing;

namespace
{
    // Every scene but the active one goes; then every object.
    void KeepOnlyActive()
    {
        Scene keep = SceneManager::GetActiveScene();
        for (u32 i = SceneManager::sceneCount(); i-- > 0;)
        {
            Scene s = SceneManager::GetSceneAt(i);
            if (s != keep && s.isLoaded())
                SceneManager::UnloadSceneAsync(s);
        }
        SceneManager::ProcessSceneLoads();
        Cleanup();
    }

    Scene Fresh(StringView name)
    {
        Scene s = SceneManager::CreateScene(name);
        SceneManager::SetActiveScene(s);
        return s;
    }

    void Record(void* user, Scene s, LoadSceneMode) { static_cast<Array<String>*>(user)->Push(Format("loaded:{}", s.name())); }
    void RecordUnloaded(void* user, Scene s) { static_cast<Array<String>*>(user)->Push(Format("unloaded:{}", s.name())); }
    void RecordActive(void* user, Scene a, Scene b)
    {
        static_cast<Array<String>*>(user)->Push(Format("active:{}>{}", a.IsValid() ? a.name() : StringView("-"), b.name()));
    }

    // root (Logger, Linker -> b) with children a (Logger, inactive child a1) and b; local values to compare.
    GameObject* BuildSample()
    {
        GameObject* root = GameObject::Create("root");
        GameObject* a = GameObject::Create("a");
        GameObject* a1 = GameObject::Create("a1");
        GameObject* b = GameObject::Create("b");
        a->transform()->SetParent(root->transform(), false);
        a1->transform()->SetParent(a->transform(), false);
        b->transform()->SetParent(root->transform(), false);
        root->transform()->SetLocalPosition(Vector3(1, 2, 3));
        a->transform()->SetLocalRotation(Quaternion::Euler(0, 90, 0));
        b->transform()->SetLocalScale(Vector3(2, 3, 4));
        root->AddComponent<Logger>();
        a1->AddComponent<Logger>();
        Linker* l = root->AddComponent<Linker>();
        l->target = PPtr<GameObject>(b);
        l->list.Push(PPtr<Object>(a->transform()));
        l->value = 42;
        a1->SetActive(false);
        b->SetLayer(5);
        return root;
    }

    // Names below a root in node order, with local positions.
    String Describe(GameObject* root)
    {
        String out;
        const TransformHierarchy& h = root->transform()->Hierarchy();
        for (u32 j = 0; j < h.count; j++)
        {
            Transform* t = h.transforms[j];
            Vector3 p = t->localPosition();
            out.Append(Format("{}({} {} {})[{}] ", t->Name(), p.x, p.y, p.z, t->gameObject()->GetComponentCount()));
        }
        return out;
    }
}

HX_TEST(Scene, NewObjectsGoIntoTheActiveScene)
{
    Scene start = SceneManager::GetActiveScene();
    HX_REQUIRE(start.IsValid() && start.isLoaded());
    Array<String> events;
    u32 a = SceneManager::activeSceneChanged().Add(&RecordActive, &events);
    u32 u = SceneManager::sceneUnloaded().Add(&RecordUnloaded, &events);
    Scene s = Fresh("Arena");
    HX_CHECK(s.IsValid() && SceneManager::GetActiveScene() == s && s.path().Empty() && s.buildIndex() == -1);
    HX_CHECK(!SceneManager::CreateScene("arena").IsValid() && SceneManager::GetSceneByName("ARENA") == s);
    GameObject* g = GameObject::Create("g");
    GameObject* child = GameObject::Create("child");
    child->transform()->SetParent(g->transform());
    GameObject* h = GameObject::Create("h");
    HX_CHECK(g->scene() == s && child->scene() == s && GameObject::GetScene(h->GetEntityId()) == s && s.rootCount() == 2u);
    Array<GameObject*> roots = s.GetRootGameObjects();
    HX_CHECK(roots.Size() == 2u && roots[0] == g && roots[1] == h);
    // Only roots move, into loaded scenes.
    HX_CHECK(!SceneManager::MoveGameObjectToScene(child, start) && SceneManager::MoveGameObjectToScene(h, start) && h->scene() == start);
    HX_CHECK(!SceneManager::SetActiveScene(Scene()) && !SceneManager::SetActiveScene(DontDestroyOnLoadScene()));
    // Clones go where they are told, or into the active scene.
    GameObject* c1 = Instantiate(g, start);
    InstantiateParameters p;
    p.scene = start;
    GameObject* c2 = Instantiate(g, p);
    GameObject* c3 = Instantiate(g);
    HX_CHECK(c1->scene() == start && c2->scene() == start && c3->scene() == s && c1->transform()->childCount() == 1u);
    // Unloading waits for ProcessSceneLoads.
    AsyncOperation op = SceneManager::UnloadSceneAsync(s);
    HX_CHECK(op.IsValid() && !op.isDone() && s.IsValid());
    EntityId gone = g->GetEntityId();
    SceneManager::ProcessSceneLoads();
    HX_CHECK(op.isDone() && op.progress() == 1.0f && !s.IsValid() && !EntityIdIsValid(gone) && SceneManager::GetActiveScene() == start);
    // Unloading the active scene makes the next loaded one active; the unloaded one is gone by then.
    HX_CHECK(events.Size() == 3u && events[0] == Format("active:{}>Arena", start.name()) && events[1] == StringView("unloaded:Arena"));
    HX_CHECK(events[2] == Format("active:->{}", start.name()));
    SceneManager::activeSceneChanged().Remove(a);
    SceneManager::sceneUnloaded().Remove(u);
    KeepOnlyActive();
}

HX_TEST(Scene, LastSceneAndMerging)
{
    KeepOnlyActive();
    Scene only = SceneManager::GetActiveScene();
    HX_CHECK(SceneManager::loadedSceneCount() == 1u && !SceneManager::UnloadSceneAsync(only).IsValid());
    Scene other = SceneManager::CreateScene("Other");
    SceneManager::SetActiveScene(other);
    GameObject* x = GameObject::Create("x");
    GameObject* y = GameObject::Create("y");
    SceneManager::SetActiveScene(only);
    GameObject* z = GameObject::Create("z");
    SceneManager::MergeScenes(other, only);
    Array<GameObject*> roots = only.GetRootGameObjects();
    HX_CHECK(!other.IsValid() && roots.Size() == 3u && roots[0] == z && roots[1] == x && roots[2] == y && x->scene() == only);
    KeepOnlyActive();
}

HX_TEST(Scene, YamlRoundTripKeepsEverything)
{
    KeepOnlyActive();
    Scene start = SceneManager::GetActiveScene();
    Scene s = Fresh("Source");
    GameObject* root = BuildSample();
    String text, error;
    HX_REQUIRE(WriteSceneYaml(s, text, error));
    HX_CHECK(text.View().StartsWith("%YAML 1.1") && text.View().Contains("--- !h!1 &") && text.View().Contains("--- !h!4 &"));
    HX_CHECK(text.View().Contains("--- !h!1660057539 &9223372036854775807") && text.View().Contains("m_Roots:"));
    HX_CHECK(text.View().Contains("m_Component:") && text.View().Contains("- component: {fileID: "));
    String expected = Describe(root);
    // Into another scene: the same layout, references into the copy, enabled parents first.
    Scene t = SceneManager::CreateScene("Copy");
    Events().Clear();
    Array<String> errors;
    HX_REQUIRE(ReadSceneFile(StringView(text), t, errors));
    HX_CHECK(errors.Empty());
    HX_CHECK(HasEvents({"root:enable"}));
    Array<GameObject*> roots = t.GetRootGameObjects();
    HX_REQUIRE(roots.Size() == 1u);
    GameObject* copy = roots[0];
    HX_CHECK(copy != root && Describe(copy) == expected);
    Linker* l = copy->GetComponent<Linker>();
    HX_REQUIRE(l != nullptr);
    GameObject* b = copy->transform()->GetChild(1)->gameObject();
    HX_CHECK(l->target.Get() == b && l->value == 42 && l->list.Size() == 1u && l->list[0].Get() == copy->transform()->GetChild(0));
    GameObject* a1 = copy->transform()->Find("a/a1")->gameObject();
    HX_CHECK(!a1->activeSelf() && !a1->GetComponent<Logger>()->isActiveAndEnabled() && b->layer() == 5u);
    HX_CHECK(copy->transform()->GetChild(0)->localRotation() == Quaternion::Euler(0, 90, 0));
    // Written again, the copy gives the same text: objects read from a file keep their fileIDs.
    String again;
    HX_REQUIRE(WriteSceneYaml(t, again, error));
    HX_CHECK(again == text);
    SceneManager::SetActiveScene(start);
    KeepOnlyActive();
}

HX_TEST(Scene, BinaryRoundTripWithAndWithoutTypeTrees)
{
    KeepOnlyActive();
    Scene start = SceneManager::GetActiveScene();
    Scene s = Fresh("Source");
    GameObject* root = BuildSample();
    String expected = Describe(root);
    for (int trees = 0; trees < 2; trees++)
    {
        Array<u8> bytes;
        String error;
        HX_REQUIRE(WriteSceneBinary(s, bytes, error, trees == 1));
        Scene t = SceneManager::CreateScene(trees ? "WithTrees" : "Player");
        Array<String> errors;
        HX_CHECK(ReadSceneFile(Span<const u8>(bytes.Data(), bytes.Size()), t, errors) && errors.Empty());
        Array<GameObject*> roots = t.GetRootGameObjects();
        HX_REQUIRE(roots.Size() == 1u);
        HX_CHECK(Describe(roots[0]) == expected && roots[0]->GetComponent<Linker>()->target.Get() == roots[0]->transform()->GetChild(1)->gameObject());
    }
    SceneManager::SetActiveScene(start);
    KeepOnlyActive();
}

HX_TEST(Scene, ReadsTheReferenceLayout)
{
    KeepOnlyActive();
    // In the full layout of scene files: prefab fields, editor only fields, a class we do not have, the roots in their own
    // order and the children in theirs.
    const char* file = R"(%YAML 1.1
%TAG !h! tag:helixengine,2026:
--- !h!1 &100
GameObject:
  m_ObjectHideFlags: 0
  m_CorrespondingSourceObject: {fileID: 0}
  m_PrefabInstance: {fileID: 0}
  m_PrefabAsset: {fileID: 0}
  serializedVersion: 6
  m_Component:
  - component: {fileID: 101}
  m_Layer: 0
  m_Name: Second
  m_TagString: Untagged
  m_Icon: {fileID: 0}
  m_NavMeshLayer: 0
  m_StaticEditorFlags: 0
  m_IsActive: 1
--- !h!4 &101
Transform:
  m_ObjectHideFlags: 0
  m_CorrespondingSourceObject: {fileID: 0}
  m_PrefabInstance: {fileID: 0}
  m_PrefabAsset: {fileID: 0}
  m_GameObject: {fileID: 100}
  serializedVersion: 2
  m_LocalRotation: {x: 0, y: 0, z: 0, w: 1}
  m_LocalPosition: {x: 0, y: 5, z: 0}
  m_LocalScale: {x: 1, y: 1, z: 1}
  m_ConstrainProportionsScale: 0
  m_Children:
  - {fileID: 301}
  - {fileID: 201}
  m_Father: {fileID: 0}
  m_LocalEulerAnglesHint: {x: 0, y: 0, z: 0}
--- !h!1 &200
GameObject:
  m_Component:
  - component: {fileID: 201}
  - component: {fileID: 202}
  m_Name: Late
  m_IsActive: 1
--- !h!4 &201
Transform:
  m_GameObject: {fileID: 200}
  m_LocalPosition: {x: 0x3F800000(1), y: 0, z: 0}
  m_Children: []
  m_Father: {fileID: 101}
--- !h!99999 &202
SomethingElse:
  m_GameObject: {fileID: 200}
--- !h!1 &300
GameObject:
  m_Component:
  - component: {fileID: 301}
  m_Name: Early
  m_IsActive: 1
--- !h!4 &301
Transform:
  m_GameObject: {fileID: 300}
  m_Children: []
  m_Father: {fileID: 101}
--- !h!1 &400
GameObject:
  m_Component:
  - component: {fileID: 401}
  m_Name: First
  m_IsActive: 1
--- !h!4 &401
Transform:
  m_GameObject: {fileID: 400}
  m_Children: []
  m_Father: {fileID: 0}
--- !h!1660057539 &9223372036854775807
SceneRoots:
  m_ObjectHideFlags: 0
  m_Roots:
  - {fileID: 401}
  - {fileID: 101}
)";
    Scene s = SceneManager::CreateScene("Reference");
    Array<String> errors;
    bool ok = ReadSceneFile(StringView(file), s, errors);
    for (const String& e : errors)
        std::printf("    %.*s\n", int(e.Size()), e.Data());
    HX_CHECK(ok);
    Array<GameObject*> roots = s.GetRootGameObjects();
    HX_REQUIRE(roots.Size() == 2u);
    HX_CHECK(roots[0]->Name() == StringView("First") && roots[1]->Name() == StringView("Second"));
    Transform* second = roots[1]->transform();
    HX_CHECK(second->childCount() == 2u && second->GetChild(0)->Name() == StringView("Early") && second->GetChild(1)->Name() == StringView("Late"));
    HX_CHECK(second->localPosition() == Vector3(0, 5, 0) && second->GetChild(1)->localPosition() == Vector3(1, 0, 0));
    // The unknown component is skipped: Late keeps its Transform only.
    HX_CHECK(second->GetChild(1)->gameObject()->GetComponentCount() == 1u);
    KeepOnlyActive();
}

HX_TEST(Scene, DamagedFilesAreReported)
{
    KeepOnlyActive();
    Scene s = SceneManager::CreateScene("Damaged");
    Array<String> errors;
    u32 before = ObjectCount();
    HX_CHECK(!ReadSceneFile(StringView("--- !h!1 &1\nGameObject:\n  m_Name: [unclosed\n"), s, errors) && !errors.Empty() && ObjectCount() == before);
    errors.Clear();
    HX_CHECK(!ReadSceneFile(StringView("garbage"), s, errors) && !errors.Empty());
    // A missing reference, a GameObject without a Transform, a repeated fileID: reported, the rest stays.
    errors.Clear();
    const char* file = R"(%YAML 1.1
%TAG !h! tag:helixengine,2026:
--- !h!1 &1
GameObject:
  m_Component:
  - component: {fileID: 2}
  - component: {fileID: 77}
  m_Name: Kept
--- !h!4 &2
Transform:
  m_GameObject: {fileID: 1}
  m_Father: {fileID: 0}
--- !h!1 &3
GameObject:
  m_Name: Bare
--- !h!1 &3
GameObject:
  m_Name: Twice
)";
    HX_CHECK(!ReadSceneFile(StringView(file), s, errors));
    HX_CHECK(errors.Size() >= 3u);
    Array<GameObject*> roots = s.GetRootGameObjects();
    HX_REQUIRE(roots.Size() == 2u);
    GameObject* kept = roots[0]->Name() == StringView("Kept") ? roots[0] : roots[1];
    GameObject* bare = kept == roots[0] ? roots[1] : roots[0];
    HX_CHECK(kept->Name() == StringView("Kept") && kept->GetComponentCount() == 1u && bare->Name() == StringView("Bare") && bare->transform());
    KeepOnlyActive();
}

HX_TEST(Scene, LoadSceneRunsInTheNextProcessAndSingleReplaces)
{
    KeepOnlyActive();
    // Two build scenes written from objects, served from memory.
    Scene src = Fresh("Writer");
    BuildSample();
    String level1, level2, error;
    HX_REQUIRE(WriteSceneYaml(src, level1, error));
    Cleanup();
    GameObject::Create("lonely")->AddComponent<Logger>();
    HX_REQUIRE(WriteSceneYaml(src, level2, error));
    Cleanup();
    MemorySceneSource memory;
    memory.Set("Assets/Scenes/Level1.helix", StringView(level1));
    memory.Set("Assets/Scenes/Level2.helix", StringView(level2));
    SceneManager::SetSceneSource(&memory);
    StringView build[] = {"Assets/Scenes/Level1.helix", "Assets/Scenes/Level2.helix"};
    SceneManager::SetBuildScenes(Span<const StringView>(build, 2));
    HX_CHECK(SceneManager::sceneCountInBuildSettings() == 2u);
    Array<String> events;
    u32 l = SceneManager::sceneLoaded().Add(&Record, &events);
    u32 u = SceneManager::sceneUnloaded().Add(&RecordUnloaded, &events);
    u32 a = SceneManager::activeSceneChanged().Add(&RecordActive, &events);
    u32 count = SceneManager::sceneCount();
    Scene s1 = SceneManager::LoadScene("Level1", LoadSceneParameters{LoadSceneMode::Additive});
    HX_CHECK(s1.IsValid() && !s1.isLoaded() && SceneManager::sceneCount() == count + 1 && s1.buildIndex() == 0 && s1.name() == StringView("Level1"));
    HX_CHECK(s1.rootCount() == 0u);
    SceneManager::ProcessSceneLoads();
    HX_CHECK(s1.isLoaded() && s1.rootCount() == 1u && SceneManager::GetSceneByBuildIndex(0) == s1 && SceneManager::GetActiveScene() != s1);
    HX_CHECK(events.Size() == 1u && events[0] == StringView("loaded:Level1"));
    // Single: everything else goes but the DontDestroyOnLoad scene.
    GameObject* keeper = GameObject::Create("keeper");
    DontDestroyOnLoad(keeper);
    events.Clear();
    Events().Clear();
    Scene s2 = SceneManager::LoadScene(StringView("Assets/Scenes/Level2.helix"), LoadSceneParameters{LoadSceneMode::Single});
    SceneManager::ProcessSceneLoads();
    HX_CHECK(s2.isLoaded() && SceneManager::GetActiveScene() == s2 && !s1.IsValid() && SceneManager::sceneCount() == 1u);
    HX_CHECK(EntityIdIsValid(keeper->GetEntityId()) && keeper->scene() == DontDestroyOnLoadScene());
    // Unloads first, then the new scene becomes active, its objects are enabled, then sceneLoaded.
    HX_CHECK(events.Size() >= 3u && events[events.Size() - 1] == StringView("loaded:Level2"));
    HX_CHECK(events[events.Size() - 2].View().StartsWith("active:") && events[0].View().StartsWith("unloaded:"));
    // The old scenes' objects went before the new ones were enabled.
    HX_CHECK(!Events().Empty() && Events().Back() == StringView("lonely:enable") && Events()[0] != StringView("lonely:enable"));
    Events().Clear();
    // Unknown scenes do not load.
    HX_CHECK(!SceneManager::LoadScene("Nowhere").IsValid() && !SceneManager::LoadScene(7).IsValid());
    SceneManager::sceneLoaded().Remove(l);
    SceneManager::sceneUnloaded().Remove(u);
    SceneManager::activeSceneChanged().Remove(a);
    SceneManager::SetSceneSource(nullptr);
    KeepOnlyActive();
}

HX_TEST(Scene, LoadSceneAsyncWaitsForActivation)
{
    KeepOnlyActive();
    Scene src = Fresh("Writer2");
    BuildSample();
    String level, error;
    HX_REQUIRE(WriteSceneYaml(src, level, error));
    Cleanup();
    MemorySceneSource memory;
    memory.Set("A.helix", StringView(level));
    memory.Set("B.helix", StringView(level));
    SceneManager::SetSceneSource(&memory);
    jobs::Initialize();
    struct Done
    {
        static void Mark(void* user, AsyncOperation) { ++*static_cast<int*>(user); }
    };
    int completed = 0;
    AsyncOperation op = SceneManager::LoadSceneAsync("A.helix", LoadSceneMode::Additive);
    HX_REQUIRE(op.IsValid());
    op.SetAllowSceneActivation(false);
    op.AddCompleted(&Done::Mark, &completed);
    // Parsing runs on a worker: wait for it (up to 10 s on a loaded machine).
    for (int i = 0; i < 10000 && op.progress() < 0.9f; i++)
    {
        SceneManager::ProcessSceneLoads();
        if (op.progress() < 0.9f)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    HX_CHECK(op.progress() == 0.9f && !op.isDone() && completed == 0);
    op.SetAllowSceneActivation(true);
    SceneManager::ProcessSceneLoads();
    HX_CHECK(op.isDone() && completed == 1 && SceneManager::GetSceneByName("A").isLoaded());
    // Registered after the end: called at once.
    op.AddCompleted(&Done::Mark, &completed);
    HX_CHECK(completed == 2);
    // LoadScene makes a held async load finish too.
    AsyncOperation held = SceneManager::LoadSceneAsync("B.helix", LoadSceneMode::Additive);
    held.SetAllowSceneActivation(false);
    Scene sync = SceneManager::LoadScene("A.helix", LoadSceneParameters{LoadSceneMode::Additive});
    SceneManager::ProcessSceneLoads();
    HX_CHECK(held.isDone() && sync.isLoaded() && SceneManager::GetSceneByName("B").isLoaded());
    jobs::Shutdown();
    SceneManager::SetSceneSource(nullptr);
    SceneManager::SetActiveScene(SceneManager::GetSceneAt(0));
    KeepOnlyActive();
}
