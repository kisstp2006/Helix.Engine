#include <Support/TestClasses.h>

#include <Helix/Testing/Test.h>

using namespace helix;
using namespace helix::engine;
using namespace helix::engine::testing;

HX_TEST(Lifecycle, DestroyWaitsForTheEndOfTheUpdateAndTakesTheHierarchy)
{
    GameObject* root = GameObject::Create("root");
    GameObject* child = GameObject::Create("child");
    child->transform()->SetParent(root->transform());
    root->AddComponent<Logger>();
    root->AddComponent<SubLogger>()->SetName("root");
    child->AddComponent<Logger>();
    EntityId r = root->GetEntityId(), c = child->GetEntityId();
    PPtr<GameObject> ref(child);
    Events().Clear();
    Destroy(root);
    // Still there until the player loop processes the queue.
    HX_CHECK(EntityIdIsValid(r) && ref.Get() == child && PendingDestroyCount() == 1u);
    HX_CHECK_EQ(ProcessDestroyQueue(), 1u);
    HX_CHECK(!EntityIdIsValid(r) && !EntityIdIsValid(c) && !ref);
    // Children first; components last to first; each disabled before it is destroyed.
    HX_CHECK(HasEvents({"child:disable", "child:destroy", "root:disable", "root:destroy", "root:disable", "root:destroy"}));
    // Repeated, late and null destroys do nothing.
    Destroy(nullptr);
    HX_CHECK(!DestroyImmediate(nullptr));
    Cleanup();
}

HX_TEST(Lifecycle, DestroyingAComponentLeavesItsGameObject)
{
    GameObject* g = GameObject::Create("g");
    Logger* l = g->AddComponent<Logger>();
    Linker* k = g->AddComponent<Linker>();
    Events().Clear();
    Destroy(l);
    Destroy(l);
    ProcessDestroyQueue();
    HX_CHECK(EntityIdIsValid(g->GetEntityId()) && g->GetComponentCount() == 2u && g->GetComponentAtIndex(1) == k);
    HX_CHECK(HasEvents({"g:disable", "g:destroy"}));
    // The Transform goes only with its GameObject.
    Destroy(g->transform());
    ProcessDestroyQueue();
    HX_CHECK(g->transform() != nullptr);
    HX_CHECK(!DestroyImmediate(g->transform()));
    Cleanup();
}

HX_TEST(Lifecycle, DelaysCountScaledTime)
{
    GameObject* g = GameObject::Create();
    EntityId id = g->GetEntityId();
    SetLifecycleTime(10);
    Destroy(g, 2.5f);
    ProcessDestroyQueue();
    SetLifecycleTime(12);
    ProcessDestroyQueue();
    // Paused time (timeScale 0) does not advance it.
    ProcessDestroyQueue();
    HX_CHECK(EntityIdIsValid(id));
    SetLifecycleTime(12.5);
    HX_CHECK_EQ(ProcessDestroyQueue(), 1u);
    HX_CHECK(!EntityIdIsValid(id));
    Cleanup();
}

HX_TEST(Lifecycle, DestroyImmediateAndAssets)
{
    Data* asset = NewObject<Data>();
    asset->SetPersistent(true);
    HX_CHECK(!DestroyImmediate(asset) && EntityIdIsValid(asset->GetEntityId()));
    HX_CHECK(DestroyImmediate(asset, true));
    GameObject* g = GameObject::Create();
    EntityId id = g->GetEntityId();
    HX_CHECK(DestroyImmediate(g) && !EntityIdIsValid(id));
    Cleanup();
}

HX_TEST(Lifecycle, DontDestroyOnLoadForRoots)
{
    GameObject* root = GameObject::Create();
    GameObject* child = GameObject::Create();
    child->transform()->SetParent(root->transform());
    HX_CHECK(!DontDestroyOnLoad(child) && !child->IsDontDestroyOnLoad());
    HX_CHECK(DontDestroyOnLoad(root->transform()) && root->IsDontDestroyOnLoad());
    Cleanup();
}

HX_TEST(Lifecycle, InstantiateClonesTheHierarchyAndRemapsReferences)
{
    GameObject* outside = GameObject::Create("outside");
    GameObject* root = GameObject::Create("enemy");
    GameObject* gun = GameObject::Create("gun");
    gun->transform()->SetParent(root->transform());
    gun->transform()->localPosition = Vector3(1, 2, 3);
    Linker* link = root->AddComponent<Linker>();
    link->target = PPtr<GameObject>(gun);
    link->list.Push(PPtr<Object>(outside));
    link->list.Push(PPtr<Object>(gun->transform()));
    link->value = 7;
    Logger* log = gun->AddComponent<Logger>();
    Events().Clear();

    GameObject* copy = Instantiate(root);
    HX_REQUIRE(copy && copy != root);
    HX_CHECK(copy->Name() == StringView("enemy(Clone)") && copy->transform()->parent() == nullptr);
    Transform* copiedGun = copy->transform()->GetChild(0);
    HX_REQUIRE(copiedGun && copiedGun != gun->transform());
    // Only the root is renamed; fields are copied.
    HX_CHECK(copiedGun->gameObject()->Name() == StringView("gun") && copiedGun->localPosition.y == 2);
    Linker* copiedLink = copy->GetComponent<Linker>();
    HX_REQUIRE(copiedLink && copiedLink != link && copiedLink->value == 7);
    // Inside the clone: to the clone; outside: kept.
    HX_CHECK(copiedLink->target.Get() == copiedGun->gameObject());
    HX_CHECK(copiedLink->list[0].Get() == outside && copiedLink->list[1].Get() == copiedGun);
    HX_CHECK(copiedLink->gameObject() == copy && copiedGun->parent() == copy->transform());
    // The original is untouched; the clone's behaviours are enabled.
    HX_CHECK(link->target.Get() == gun && root->transform()->GetChild(0) == gun->transform());
    HX_CHECK(HasEvents({"gun:enable"}) && copiedGun->gameObject()->GetComponent<Logger>()->isActiveAndEnabled());

    // A component: its GameObject's hierarchy, the component's clone back; under a parent.
    Logger* logCopy = Instantiate(log, InstantiateParameters{outside->transform()});
    HX_REQUIRE(logCopy && logCopy != log);
    HX_CHECK(logCopy->gameObject()->Name() == StringView("gun(Clone)") && logCopy->transform()->parent() == outside->transform());
    // Other objects clone alone.
    Data* d = NewObject<Data>();
    d->SetName("data");
    d->other = PPtr<Object>(outside);
    Data* dc = Instantiate(d);
    HX_CHECK(dc && dc->Name() == StringView("data(Clone)") && dc->other.Get() == outside && !dc->IsPersistent());
    Cleanup();
}

HX_TEST(Lifecycle, InstantiateGameObjectsInABatch)
{
    GameObject* g = GameObject::Create("unit");
    g->AddComponent<Linker>();
    EntityId ids[8], transforms[8];
    HX_REQUIRE(InstantiateGameObjects(g->GetEntityId(), 8, Span<EntityId>(ids, 8), Span<EntityId>(transforms, 8)));
    bool ok = true;
    for (u32 i = 0; i < 8; i++)
    {
        GameObject* c = EntityIdToObject<GameObject>(ids[i]);
        ok = ok && c && c != g && c->transform()->GetEntityId() == transforms[i] && c->GetComponent<Linker>() != nullptr;
    }
    HX_CHECK(ok);
    HX_CHECK(!InstantiateGameObjects(EntityId::None(), 1, Span<EntityId>(ids, 8), Span<EntityId>(transforms, 8)));
    Cleanup();
}
