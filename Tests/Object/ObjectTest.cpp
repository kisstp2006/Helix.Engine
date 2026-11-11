#include <Support/TestClasses.h>

#include <Helix/Testing/Test.h>

using namespace helix;
using namespace helix::engine;
using namespace helix::engine::testing;

HX_TEST(Object, EntityIdsResolveUntilTheObjectGoes)
{
    HX_CHECK(!EntityId::None().IsValid());
    HX_CHECK(EntityIdToObject(EntityId::None()) == nullptr);
    GameObject* a = GameObject::Create("a");
    GameObject* b = GameObject::Create("b");
    EntityId ia = a->GetEntityId(), ib = b->GetEntityId();
    HX_CHECK(ia.IsValid() && ib.IsValid() && ia != ib);
    HX_CHECK(EntityIdToObject(ia) == a && EntityIdToObject<GameObject>(ib) == b);
    HX_CHECK(EntityIdToObject<Transform>(ia) == nullptr);
    HX_CHECK(EntityId::FromULong(EntityId::ToULong(ia)) == ia);
    DestroyImmediate(a);
    HX_CHECK(!EntityIdIsValid(ia) && EntityIdIsValid(ib));
    // The freed slot goes to the next object under a new generation: the old id stays dead.
    GameObject* c = GameObject::Create("c");
    HX_CHECK(c->GetEntityId() != ia && EntityIdToObject(ia) == nullptr);
    const EntityId ids[] = {ia, ib, EntityId::None()};
    Array<Object*> objects;
    EntityIdsToObjectList(Span<const EntityId>(ids, 3), objects);
    HX_CHECK(objects.Size() == 3u && objects[0] == nullptr && objects[1] == b && objects[2] == nullptr);
    Array<EntityId> valid;
    EntityIdsToValidArray(Span<const EntityId>(ids, 3), valid);
    HX_CHECK(valid.Size() == 1u && valid[0] == ib);
    Cleanup();
}

HX_TEST(Object, ClassesHaveTheReferenceIdsAndATree)
{
    HX_CHECK_EQ(GetClass(ClassOf<GameObject>()).classId, 1);
    HX_CHECK_EQ(GetClass(ClassOf<Transform>()).classId, 4);
    HX_CHECK_EQ(GetClass(ClassOf<Behaviour>()).classId, 8);
    HX_CHECK(FindClassById(4) == ClassOf<Transform>() && FindClassByName("Component") == ClassOf<Component>());
    HX_CHECK(IsDerivedFrom(ClassOf<Transform>(), ClassOf<Component>()) && IsDerivedFrom(ClassOf<Transform>(), ClassOf<Object>()));
    HX_CHECK(!IsDerivedFrom(ClassOf<Transform>(), ClassOf<Behaviour>()) && !IsDerivedFrom(ClassOf<Component>(), ClassOf<Transform>()));
    // A class registered later slots into the tree; earlier answers stay right.
    u32 sub = ClassOf<SubLogger>();
    HX_CHECK(IsDerivedFrom(sub, ClassOf<Logger>()) && IsDerivedFrom(sub, ClassOf<Behaviour>()) && IsDerivedFrom(ClassOf<Logger>(), ClassOf<Component>()));
    HX_CHECK(!IsDerivedFrom(ClassOf<Logger>(), sub) && !IsDerivedFrom(ClassOf<Linker>(), ClassOf<Behaviour>()));
    HX_CHECK(GetClass(ClassOf<Logger>()).flags & ClassBehaviour);
    HX_CHECK(GetClass(ClassOf<Component>()).create == nullptr && GetClass(ClassOf<Linker>()).create != nullptr);
    HX_CHECK_EQ(u32(HideFlags::DontSave), 52u);
    HX_CHECK_EQ(u32(HideFlags::HideAndDontSave), 61u);
}

HX_TEST(Object, FindObjectsByTypeSkipsAssetsHiddenAndInactive)
{
    GameObject* active = GameObject::Create("active");
    GameObject* inactive = GameObject::Create("inactive");
    GameObject* hidden = GameObject::Create("hidden");
    GameObject* asset = GameObject::Create("asset");
    Logger* l1 = active->AddComponent<Logger>();
    inactive->AddComponent<SubLogger>();
    // HideFlags belong to each object: the component is hidden on its own.
    hidden->AddComponent<Logger>()->hideFlags = HideFlags::DontSave;
    inactive->SetActive(false);
    hidden->hideFlags = HideFlags::DontSave;
    asset->SetPersistent(true);
    Array<Logger*> found;
    FindObjectsByType(found);
    HX_CHECK(found.Size() == 1u && found[0] == l1);
    FindObjectsByType(found, FindObjectsInactive::Include);
    HX_CHECK_EQ(found.Size(), usize(2));
    Array<GameObject*> objects;
    FindObjectsByType(objects);
    HX_CHECK_EQ(objects.Size(), usize(1));
    HX_CHECK(FindAnyObjectByType<SubLogger>() == nullptr && FindAnyObjectByType<SubLogger>(FindObjectsInactive::Include) != nullptr);
    Array<Object*> all;
    FindObjectsOfTypeAll(ClassOf<GameObject>(), all);
    HX_CHECK_EQ(all.Size(), usize(4));
    Cleanup();
}
