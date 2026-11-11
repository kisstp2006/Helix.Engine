#include <Support/TestClasses.h>

#include <Helix/Testing/Test.h>

using namespace helix;
using namespace helix::engine;
using namespace helix::engine::testing;

HX_TEST(GameObject, ComponentsTransformFirstAndLookups)
{
    GameObject* g = GameObject::Create("g");
    HX_REQUIRE(g->transform() && g->GetComponentCount() == 1u && g->GetComponentAtIndex(0) == g->transform());
    Logger* a = g->AddComponent<Logger>();
    SubLogger* b = g->AddComponent<SubLogger>();
    Linker* l = g->AddComponent<Linker>();
    HX_CHECK(g->GetComponentCount() == 4u && g->GetComponentIndex(b) == 2 && b->GetComponentIndex() == 2);
    // By base class: the first that derives from it.
    HX_CHECK(g->GetComponent<Behaviour>() == a && g->GetComponent<SubLogger>() == b && g->GetComponent<Component>() == g->transform());
    HX_CHECK(g->GetComponent("Linker") == l && g->GetComponent("Nothing") == nullptr);
    Array<Component*> list;
    g->GetComponents(ClassOf<Logger>(), list);
    HX_CHECK(list.Size() == 2u && list[0] == a && list[1] == b);
    Single* s = nullptr;
    HX_CHECK(!g->TryGetComponent(s) && s == nullptr);
    HX_CHECK(a->gameObject() == g && a->transform() == g->transform() && a->Name() == StringView("g"));
    a->SetName("renamed");
    HX_CHECK(g->Name() == StringView("renamed"));
    Cleanup();
}

HX_TEST(GameObject, AddComponentRules)
{
    GameObject* g = GameObject::Create();
    // Abstract classes, non components and a second Transform are refused.
    HX_CHECK(g->AddComponent(ClassOf<Behaviour>()) == nullptr);
    HX_CHECK(g->AddComponent(ClassOf<GameObject>()) == nullptr);
    HX_CHECK(g->AddComponent<Transform>() == nullptr);
    HX_CHECK(g->AddComponent<Single>() != nullptr && g->AddComponent<Single>() == nullptr);
    // RequireComponent: what is missing comes first.
    GameObject* h = GameObject::Create();
    Needy* n = h->AddComponent<Needy>();
    HX_REQUIRE(n);
    HX_CHECK(h->GetComponentCount() == 4u && h->GetComponentIndex(h->GetComponent<Linker>()) == 1 && h->GetComponentIndex(n) == 3);
    Cleanup();
}

HX_TEST(GameObject, TagsAndLayers)
{
    GameObject* g = GameObject::Create();
    HX_CHECK(g->TagString() == StringView("Untagged") && g->CompareTag("Untagged"));
    HX_CHECK(g->SetTag("Player") && g->CompareTag("Player"));
    TagHandle player;
    HX_CHECK(GetExistingTag("Player", player) && g->CompareTag(player));
    HX_CHECK(!g->SetTag("NoSuchTag") && g->CompareTag("Player"));
    TagHandle mine;
    HX_CHECK(AddTag("Mine", mine) && g->SetTag("Mine") && g->CompareTag(mine));
    HX_CHECK(LayerName(0) == StringView("Default") && NameToLayer("UI") == 5 && NameToLayer("") == -1);
    g->SetLayer(40);
    HX_CHECK_EQ(g->layer(), 0u);
    g->SetLayer(5);
    HX_CHECK_EQ(g->layer(), 5u);
    Cleanup();
}

HX_TEST(GameObject, HierarchyParentsAndFind)
{
    GameObject* root = GameObject::Create("root");
    GameObject* a = GameObject::Create("a");
    GameObject* b = GameObject::Create("b");
    HX_CHECK(a->transform()->SetParent(root->transform()) && b->transform()->SetParent(a->transform()));
    HX_CHECK(root->transform()->childCount() == 1u && b->transform()->root() == root->transform());
    HX_CHECK(b->transform()->IsChildOf(root->transform()) && b->transform()->IsChildOf(b->transform()) && !root->transform()->IsChildOf(b->transform()));
    // No cycles.
    HX_CHECK(!root->transform()->SetParent(b->transform()) && root->transform()->parent() == nullptr);
    HX_CHECK(root->transform()->Find("a/b") == b->transform() && root->transform()->Find("b") == nullptr);
    HX_CHECK(GameObject::Find("b") == b && GameObject::Find("a/b") == b && GameObject::Find("/root/a/b") == b && GameObject::Find("/a/b") == nullptr);
    GameObject* c = GameObject::Create("c");
    c->transform()->SetParent(root->transform());
    HX_CHECK(c->transform()->GetSiblingIndex() == 1 && a->transform()->GetSiblingIndex() == 0);
    a->transform()->SetParent(nullptr);
    HX_CHECK(root->transform()->childCount() == 1u && c->transform()->GetSiblingIndex() == 0 && a->transform()->parent() == nullptr);
    Cleanup();
}

HX_TEST(GameObject, ActivationAndEnableHooks)
{
    GameObject* parent = GameObject::Create("p");
    GameObject* child = GameObject::Create("c");
    child->transform()->SetParent(parent->transform());
    Events().Clear();
    Logger* pl = parent->AddComponent<Logger>();
    Logger* cl = child->AddComponent<Logger>();
    HX_CHECK(HasEvents({"p:enable", "c:enable"}));
    HX_CHECK(pl->isActiveAndEnabled() && cl->isActiveAndEnabled());
    // SetActive changes activeSelf only; activeInHierarchy follows; hooks parents first.
    parent->SetActive(false);
    HX_CHECK(!parent->activeSelf() && child->activeSelf() && !child->activeInHierarchy());
    HX_CHECK(HasEvents({"p:disable", "c:disable"}));
    HX_CHECK(!cl->isActiveAndEnabled());
    // Disabling a component under an inactive parent does nothing more; enabling the parent brings back what is on.
    cl->SetEnabled(false);
    HX_CHECK(HasEvents({}));
    parent->SetActive(true);
    HX_CHECK(HasEvents({"p:enable"}));
    cl->SetEnabled(true);
    HX_CHECK(HasEvents({"c:enable"}));
    // Moving under an inactive parent deactivates.
    GameObject* off = GameObject::Create("off");
    off->SetActive(false);
    child->transform()->SetParent(off->transform());
    HX_CHECK(!child->activeInHierarchy() && HasEvents({"c:disable"}));
    EntityId offId = off->GetEntityId();
    GameObject::SetGameObjectsActive(Span<const EntityId>(&offId, 1), true);
    HX_CHECK(child->activeInHierarchy() && HasEvents({"c:enable"}));
    Cleanup();
}

HX_TEST(GameObject, ComponentsInChildrenAndParents)
{
    GameObject* root = GameObject::Create("root");
    GameObject* mid = GameObject::Create("mid");
    GameObject* leaf = GameObject::Create("leaf");
    mid->transform()->SetParent(root->transform());
    leaf->transform()->SetParent(mid->transform());
    Logger* ml = mid->AddComponent<Logger>();
    Logger* ll = leaf->AddComponent<Logger>();
    HX_CHECK(root->GetComponentInChildren(ClassOf<Logger>()) == ml && leaf->GetComponentInParent(ClassOf<Logger>()) == ll);
    Array<Component*> list;
    root->GetComponentsInChildren(ClassOf<Logger>(), false, list);
    HX_CHECK(list.Size() == 2u && list[0] == ml && list[1] == ll);
    // Inactive GameObjects only with includeInactive.
    mid->SetActive(false);
    HX_CHECK(root->GetComponentInChildren(ClassOf<Logger>()) == nullptr && root->GetComponentInChildren(ClassOf<Logger>(), true) == ml);
    leaf->GetComponentsInParent(ClassOf<Logger>(), true, list);
    HX_CHECK(list.Size() == 2u && list[0] == ll && list[1] == ml);
    leaf->GetComponentsInParent(ClassOf<Logger>(), false, list);
    HX_CHECK(list.Empty());
    // Find sees active GameObjects only.
    HX_CHECK(GameObject::Find("leaf") == nullptr && GameObject::Find("root") == root);
    Cleanup();
}

HX_TEST(GameObject, FindByTag)
{
    GameObject* a = GameObject::Create("a");
    GameObject* b = GameObject::Create("b");
    GameObject* c = GameObject::Create("c");
    a->SetTag("Player"), b->SetTag("Player"), c->SetTag("Player");
    c->SetActive(false);
    Array<GameObject*> found;
    GameObject::FindGameObjectsWithTag("Player", found);
    HX_CHECK_EQ(found.Size(), usize(2));
    HX_CHECK(GameObject::FindWithTag("Player") != nullptr && GameObject::FindWithTag("Finish") == nullptr);
    Cleanup();
}
