#include <Support/TestClasses.h>

#include <Helix/Engine/Transform/TransformHandle.h>
#include <Helix/Jobs/Jobs.h>
#include <Helix/Serialization/Transfer/Yaml.h>
#include <Helix/Serialization/Yaml/Parser.h>
#include <Helix/Testing/Test.h>

#include <atomic>

using namespace helix;
using namespace helix::engine;
using namespace helix::engine::testing;

namespace
{
    bool Near(Vector3 a, Vector3 b, f32 eps = 1e-4f) { return (a - b).magnitude() <= eps; }
    bool Near(Quaternion a, Quaternion b, f32 eps = 1e-4f) { return 1.0f - mathf::Abs(Quaternion::Dot(a, b)) <= eps; }

    Transform* Make(StringView name, Transform* parent = nullptr)
    {
        Transform* t = GameObject::Create(name)->transform();
        if (parent)
            t->SetParent(parent, false);
        return t;
    }

    // Reparents in its enable hook, which is refused while activation runs.
    class Reparenter final : public Behaviour
    {
        HX_ENGINE_CLASS(Reparenter, Behaviour, 2000010, ClassNone)

    public:
        static inline Transform* target = nullptr;
        static inline int result = -1;
        void OnEnableNative()
        {
            if (target)
                result = transform()->SetParent(target) ? 1 : 0;
        }
    };
}

HX_TEST(Transform, NodesInDepthFirstOrder)
{
    Transform* root = Make("root");
    Transform* a = Make("a", root);
    Transform* b = Make("b", root);
    Transform* a1 = Make("a1", a);
    Transform* a2 = Make("a2", a);
    // root, a, a1, a2, b: subtrees are ranges.
    HX_CHECK(root->hierarchyCount() == 5u && &a2->Hierarchy() == &root->Hierarchy());
    HX_CHECK(root->NodeIndex() == 0u && a->NodeIndex() == 1u && a1->NodeIndex() == 2u && a2->NodeIndex() == 3u && b->NodeIndex() == 4u);
    HX_CHECK(root->childCount() == 2u && a->childCount() == 2u && root->GetChild(1) == b && root->GetChild(2) == nullptr);
    HX_CHECK(a2->GetSiblingIndex() == 1 && b->GetSiblingIndex() == 1 && a2->root() == root && a2->parent() == a);
    HX_CHECK(a2->IsChildOf(root) && a2->IsChildOf(a2) && !a2->IsChildOf(b) && !root->IsChildOf(a));
    HX_CHECK(root->Find("a/a2") == a2 && root->Find("b") == b && root->Find("a/zz") == nullptr);
    // Moving a subtree between hierarchies moves its range.
    Transform* other = Make("other");
    a->SetParent(other, false);
    HX_CHECK(root->hierarchyCount() == 2u && other->hierarchyCount() == 4u && a1->root() == other && a2->NodeIndex() == 3u);
    HX_CHECK(root->childCount() == 1u && b->NodeIndex() == 1u && b->GetSiblingIndex() == 0);
    // A cycle is refused.
    HX_CHECK(!other->SetParent(a2) && other->parent() == nullptr);
    Cleanup();
}

HX_TEST(Transform, WorldPositionStays)
{
    Transform* parent = Make("parent");
    parent->SetLocalPosition(Vector3(10, 0, 0));
    parent->SetLocalRotation(Quaternion::Euler(0, 90, 0));
    parent->SetLocalScale(Vector3(2, 2, 2));
    Transform* child = Make("child");
    child->SetPosition(Vector3(1, 2, 3));
    child->SetRotation(Quaternion::Euler(10, 20, 30));
    Quaternion before = child->rotation();
    HX_REQUIRE(child->SetParent(parent));
    HX_CHECK(Near(child->position(), Vector3(1, 2, 3)) && Near(child->rotation(), before) && Near(child->lossyScale(), Vector3(1, 1, 1)));
    HX_CHECK(Near(child->localScale(), Vector3(0.5f, 0.5f, 0.5f)));
    // Without worldPositionStays the local values stay.
    Transform* local = Make("local");
    local->SetLocalPosition(Vector3(1, 0, 0));
    local->SetParent(parent, false);
    HX_CHECK(Near(local->localPosition(), Vector3(1, 0, 0)) && Near(local->position(), Vector3(10, 0, -2)));
    // Back to a root keeps the world placement too.
    child->SetParent(nullptr);
    HX_CHECK(Near(child->position(), Vector3(1, 2, 3)) && Near(child->localPosition(), Vector3(1, 2, 3)));
    Cleanup();
}

HX_TEST(Transform, WorldValuesThroughTheChain)
{
    Transform* a = Make("a");
    Transform* b = Make("b", a);
    Transform* c = Make("c", b);
    a->SetLocalPosition(Vector3(1, 0, 0));
    a->SetLocalRotation(Quaternion::Euler(0, 0, 90));
    b->SetLocalPosition(Vector3(0, 2, 0));
    b->SetLocalScale(Vector3(2, 3, 4));
    c->SetLocalPosition(Vector3(1, 1, 1));
    // a turns x to y and y to -x: b sits at (1 - 2, 0, 0); c at b + rot(scale(1, 1, 1)).
    HX_CHECK(Near(b->position(), Vector3(-1, 0, 0)));
    HX_CHECK(Near(c->position(), Vector3(-1 - 3, 2, 4)));
    HX_CHECK(Near(c->lossyScale(), Vector3(2, 3, 4)) && Near(c->rotation(), Quaternion::Euler(0, 0, 90)));
    HX_CHECK(Near(c->right(), Vector3(0, 1, 0)) && Near(c->up(), Vector3(-1, 0, 0)) && Near(c->forward(), Vector3(0, 0, 1)));
    Vector3 p(0.5f, -2, 7);
    HX_CHECK(Near(c->InverseTransformPoint(c->TransformPoint(p)), p) && Near(c->TransformPoint(Vector3(0, 0, 0)), c->position()));
    HX_CHECK(Near(c->TransformDirection(Vector3(1, 0, 0)), Vector3(0, 1, 0)) && Near(c->TransformVector(Vector3(1, 0, 0)), Vector3(0, 2, 0)));
    HX_CHECK(Near(c->InverseTransformDirection(c->TransformDirection(p)), p) && Near(c->InverseTransformVector(c->TransformVector(p)), p));
    // Setting world values on a child.
    c->SetPosition(Vector3(5, 5, 5));
    c->SetRotation(Quaternion::Euler(30, 0, 0));
    HX_CHECK(Near(c->position(), Vector3(5, 5, 5)) && Near(c->rotation(), Quaternion::Euler(30, 0, 0)));
    c->SetEulerAngles(Vector3(0, 45, 0));
    HX_CHECK(Near(c->eulerAngles(), Vector3(0, 45, 0)) && Near((c->localToWorldMatrix() * c->worldToLocalMatrix()).MultiplyPoint3x4(p), p));
    // Batch conversions match the single ones, in place and into another span.
    Vector3 in[3] = {Vector3(1, 2, 3), Vector3(-4, 0, 1), Vector3(0, 0, 0)};
    Vector3 out[3];
    Vector3 same[3] = {in[0], in[1], in[2]};
    c->TransformPoints(Span<const Vector3>(in, 3), Span<Vector3>(out, 3));
    c->TransformPoints(Span<Vector3>(same, 3));
    bool match = true;
    for (u32 i = 0; i < 3; i++)
        match = match && Near(out[i], c->TransformPoint(in[i])) && Near(same[i], out[i]);
    c->TransformDirections(Span<const Vector3>(in, 3), Span<Vector3>(out, 3));
    for (u32 i = 0; i < 3; i++)
        match = match && Near(out[i], c->TransformDirection(in[i]));
    c->TransformVectors(Span<const Vector3>(in, 3), Span<Vector3>(out, 3));
    for (u32 i = 0; i < 3; i++)
        match = match && Near(out[i], c->TransformVector(in[i]));
    c->InverseTransformPoints(Span<const Vector3>(in, 3), Span<Vector3>(out, 3));
    for (u32 i = 0; i < 3; i++)
        match = match && Near(out[i], c->InverseTransformPoint(in[i]));
    c->InverseTransformDirections(Span<const Vector3>(in, 3), Span<Vector3>(out, 3));
    for (u32 i = 0; i < 3; i++)
        match = match && Near(out[i], c->InverseTransformDirection(in[i]));
    c->InverseTransformVectors(Span<const Vector3>(in, 3), Span<Vector3>(out, 3));
    for (u32 i = 0; i < 3; i++)
        match = match && Near(out[i], c->InverseTransformVector(in[i]), 1e-3f);
    HX_CHECK(match);
    Cleanup();
}

HX_TEST(Transform, Movement)
{
    Transform* t = Make("t");
    t->Translate(Vector3(0, 0, 1));
    HX_CHECK(Near(t->position(), Vector3(0, 0, 1)));
    t->Rotate(Vector3(0, 90, 0));
    t->Translate(Vector3(0, 0, 1));
    HX_CHECK(Near(t->position(), Vector3(1, 0, 1)) && Near(t->forward(), Vector3(1, 0, 0)));
    t->Translate(Vector3(0, 0, 1), Space::World);
    HX_CHECK(Near(t->position(), Vector3(1, 0, 2)));
    // Around the world axis or the local one.
    t->SetRotation(Quaternion::identity());
    t->Rotate(Vector3(1, 0, 0), 90, Space::World);
    HX_CHECK(Near(t->forward(), Vector3(0, -1, 0)));
    t->Rotate(Vector3(0, 1, 0), 90, Space::Self);
    HX_CHECK(Near(t->rotation(), Quaternion::Euler(90, 0, 0) * Quaternion::Euler(0, 90, 0)));
    t->SetRotation(Quaternion::identity());
    t->Rotate(Vector3(0, 0, 90), Space::World);
    HX_CHECK(Near(t->right(), Vector3(0, 1, 0)));
    // RotateAround moves and turns.
    t->SetPositionAndRotation(Vector3(1, 0, 0), Quaternion::identity());
    t->RotateAround(Vector3(0, 0, 0), Vector3(0, 1, 0), 90);
    HX_CHECK(Near(t->position(), Vector3(0, 0, -1)) && Near(t->forward(), Vector3(1, 0, 0)));
    // LookAt, also along the up axis.
    t->SetPosition(Vector3(0, 0, 0));
    t->LookAt(Vector3(0, 0, -5));
    HX_CHECK(Near(t->forward(), Vector3(0, 0, -1)) && Near(t->up(), Vector3(0, 1, 0)));
    t->LookAt(Vector3(0, 3, 0));
    HX_CHECK(Near(t->forward(), Vector3(0, 1, 0)));
    Transform* target = Make("target");
    target->SetPosition(Vector3(4, 0, 0));
    t->LookAt(target);
    HX_CHECK(Near(t->forward(), Vector3(1, 0, 0)));
    // The axis setters.
    t->SetForward(Vector3(0, 0, -1));
    HX_CHECK(Near(t->forward(), Vector3(0, 0, -1)));
    t->SetUp(Vector3(1, 0, 0));
    HX_CHECK(Near(t->up(), Vector3(1, 0, 0)));
    t->SetRight(Vector3(0, 0, 1));
    HX_CHECK(Near(t->right(), Vector3(0, 0, 1)));
    // Relative to another transform.
    Transform* other = Make("other");
    other->SetRotation(Quaternion::Euler(0, 90, 0));
    t->SetPosition(Vector3(0, 0, 0));
    t->Translate(Vector3(0, 0, 2), other);
    HX_CHECK(Near(t->position(), Vector3(2, 0, 0)));
    Vector3 p;
    Quaternion q;
    other->GetPositionAndRotation(p, q);
    HX_CHECK(Near(q, Quaternion::Euler(0, 90, 0)));
    t->SetParent(other, false);
    t->SetLocalPositionAndRotation(Vector3(0, 0, 1), Quaternion::identity());
    t->GetLocalPositionAndRotation(p, q);
    HX_CHECK(Near(p, Vector3(0, 0, 1)) && Near(t->position(), Vector3(1, 0, 0)));
    t->SetLocalEulerAngles(Vector3(0, 0, 30));
    HX_CHECK(Near(t->localEulerAngles(), Vector3(0, 0, 30)));
    Cleanup();
}

HX_TEST(Transform, SiblingsAndDetach)
{
    Transform* root = Make("root");
    Transform* a = Make("a", root);
    Transform* b = Make("b", root);
    Transform* c = Make("c", root);
    Transform* b1 = Make("b1", b);
    c->SetSiblingIndex(0);
    HX_CHECK(root->GetChild(0) == c && root->GetChild(1) == a && root->GetChild(2) == b && b1->parent() == b);
    c->SetAsLastSibling();
    a->SetAsLastSibling();
    HX_CHECK(root->GetChild(0) == b && root->GetChild(1) == c && root->GetChild(2) == a && b->GetSiblingIndex() == 0);
    HX_CHECK(b->NodeIndex() == 1u && b1->NodeIndex() == 2u && c->NodeIndex() == 3u && a->NodeIndex() == 4u);
    // Roots have their own order.
    Transform* r2 = Make("r2");
    i32 last = r2->GetSiblingIndex();
    HX_CHECK(last == root->GetSiblingIndex() + 1);
    r2->SetAsFirstSibling();
    HX_CHECK(r2->GetSiblingIndex() == 0 && root->GetSiblingIndex() == 1);
    // DetachChildren keeps the world placement.
    root->SetLocalPosition(Vector3(0, 5, 0));
    Vector3 at = b1->position();
    root->DetachChildren();
    HX_CHECK(root->childCount() == 0u && b->parent() == nullptr && b1->parent() == b && Near(b1->position(), at));
    Cleanup();
}

HX_TEST(Transform, HasChangedAndCapacity)
{
    Transform* root = Make("root");
    root->SetHierarchyCapacity(64);
    HX_CHECK(root->hierarchyCapacity() == 64u);
    Transform* child = Make("child", root);
    HX_CHECK(root->hierarchyCapacity() == 64u && root->hierarchyCount() == 2u);
    root->SetHasChanged(false);
    child->SetHasChanged(false);
    root->SetLocalPosition(Vector3(1, 0, 0));
    HX_CHECK(root->hasChanged() && child->hasChanged());
    child->SetHasChanged(false);
    child->SetLocalPosition(Vector3(1, 0, 0));
    root->SetHasChanged(false);
    child->SetLocalScale(Vector3(2, 2, 2));
    HX_CHECK(!root->hasChanged() && child->hasChanged());
    // Never below the nodes in use.
    root->SetHierarchyCapacity(1);
    HX_CHECK(root->hierarchyCapacity() == 2u && Near(child->position(), Vector3(2, 0, 0)));
    Cleanup();
}

HX_TEST(Transform, ChangeDispatch)
{
    u32 renderer = RegisterTransformSystem("TestRenderer");
    HX_REQUIRE(renderer != kNoTransformSystem && RegisterTransformSystem("TestRenderer") == renderer);
    Transform* root = Make("root");
    Transform* seen = Make("seen", root);
    Transform* ignored = Make("ignored", root);
    SetTransformInterest(seen, renderer, true);
    HX_CHECK(TransformInterest(seen, renderer) && !TransformInterest(ignored, renderer));
    struct Visit
    {
        Array<Transform*>* out;
        Vector3* position;
        void operator()(TransformHierarchy& h, u32 i) const
        {
            out->Push(h.transforms[i]);
            *position = h.world[i].GetPosition();
        }
    };
    Array<Transform*> changed;
    Vector3 position;
    DispatchTransformChanges(renderer, Visit{&changed, &position}, false);
    HX_CHECK(changed.Empty());
    root->SetLocalPosition(Vector3(0, 3, 0));
    DispatchTransformChanges(renderer, Visit{&changed, &position}, false);
    // The world matrix is up to date when visited.
    HX_CHECK(changed.Size() == 1u && changed[0] == seen && Near(position, Vector3(0, 3, 0)));
    changed.Clear();
    DispatchTransformChanges(renderer, Visit{&changed, &position}, false);
    HX_CHECK(changed.Empty());
    // Many hierarchies in parallel.
    jobs::Initialize();
    Array<Transform*> all;
    for (u32 i = 0; i < 200; i++)
    {
        Transform* r = Make("r");
        Transform* c = Make("c", r);
        SetTransformInterest(c, renderer, true);
        all.Push(r);
    }
    for (Transform* r : all)
        r->SetLocalPosition(Vector3(1, 0, 0));
    std::atomic<u32> count{0};
    struct Count
    {
        std::atomic<u32>* count;
        void operator()(TransformHierarchy& h, u32 i) const
        {
            if (h.world[i].GetPosition().x == 1.0f)
                count->fetch_add(1);
        }
    };
    DispatchTransformChanges(renderer, Count{&count}, true);
    HX_CHECK(count.load() == 200u);
    jobs::Shutdown();
    // Without interest a change is not reported.
    SetTransformInterest(seen, renderer, false);
    root->SetLocalPosition(Vector3(0, 4, 0));
    changed.Clear();
    DispatchTransformChanges(renderer, Visit{&changed, &position}, false);
    HX_CHECK(changed.Empty());
    // UpdateWorldMatrices brings every stale hierarchy up to date.
    UpdateWorldMatrices();
    HX_CHECK(!(seen->Hierarchy().flags[seen->NodeIndex()] & TransformFlags::WorldDirty));
    Cleanup();
}

HX_TEST(Transform, DestroyInsideAHierarchy)
{
    GameObject* root = GameObject::Create("root");
    GameObject* a = GameObject::Create("a");
    GameObject* a1 = GameObject::Create("a1");
    GameObject* b = GameObject::Create("b");
    a->transform()->SetParent(root->transform());
    a1->transform()->SetParent(a->transform());
    b->transform()->SetParent(root->transform());
    a->AddComponent<Logger>();
    a1->AddComponent<Logger>();
    u32 roots = RootHierarchyCount();
    Events().Clear();
    HX_REQUIRE(DestroyImmediate(a));
    HX_CHECK(HasEvents({"a1:disable", "a1:destroy", "a:disable", "a:destroy"}));
    HX_CHECK(root->transform()->childCount() == 1u && root->transform()->hierarchyCount() == 2u && b->transform()->NodeIndex() == 1u);
    HX_CHECK(RootHierarchyCount() == roots);
    HX_REQUIRE(DestroyImmediate(root));
    HX_CHECK(RootHierarchyCount() == roots - 1 && ObjectCount() == 0u);
    Cleanup();
}

HX_TEST(Transform, InstantiateCopiesTheLayout)
{
    Transform* holder = Make("holder");
    holder->SetPosition(Vector3(100, 0, 0));
    Transform* root = Make("root", holder);
    root->SetLocalPosition(Vector3(1, 0, 0));
    Transform* a = Make("a", root);
    Make("a1", a);
    Make("b", root);
    a->SetLocalPosition(Vector3(0, 1, 0));
    // No parent: the original's world placement.
    GameObject* copy = Instantiate(root->gameObject());
    Transform* c = copy->transform();
    HX_CHECK(c->parent() == nullptr && c->hierarchyCount() == 4u && Near(c->position(), Vector3(101, 0, 0)));
    HX_CHECK(c->GetChild(0)->Name() == StringView("a") && c->GetChild(0)->GetChild(0)->Name() == StringView("a1") && c->GetChild(1)->Name() == StringView("b"));
    HX_CHECK(Near(c->GetChild(0)->localPosition(), Vector3(0, 1, 0)) && c->GetChild(0)->GetChild(0)->NodeIndex() == 2u);
    // Under a parent: local values, or the world placement with worldSpace.
    Transform* other = Make("other");
    other->SetPosition(Vector3(0, 0, 50));
    GameObject* local = Instantiate(root->gameObject(), other);
    HX_CHECK(local->transform()->parent() == other && Near(local->transform()->position(), Vector3(1, 0, 50)));
    GameObject* world = Instantiate(root->gameObject(), other, true);
    HX_CHECK(Near(world->transform()->position(), Vector3(101, 0, 0)) && other->childCount() == 2u);
    // At a position and rotation.
    GameObject* placed = Instantiate(root->gameObject(), Vector3(7, 8, 9), Quaternion::Euler(0, 90, 0), other);
    HX_CHECK(Near(placed->transform()->position(), Vector3(7, 8, 9)) && Near(placed->transform()->rotation(), Quaternion::Euler(0, 90, 0)));
    HX_CHECK(Near(placed->transform()->GetChild(0)->position(), Vector3(7, 9, 9)));
    Cleanup();
}

HX_TEST(Transform, NoReparentingWhileActivating)
{
    Transform* target = Make("target");
    GameObject* g = GameObject::Create("g");
    GameObject* child = GameObject::Create("child");
    child->transform()->SetParent(g->transform());
    child->AddComponent<Reparenter>();
    g->SetActive(false);
    Reparenter::target = target;
    Reparenter::result = -1;
    g->SetActive(true);
    HX_CHECK(Reparenter::result == 0 && child->transform()->parent() == g->transform());
    // Outside activation it works.
    HX_CHECK(child->transform()->SetParent(target));
    Reparenter::target = nullptr;
    Cleanup();
}

HX_TEST(Transform, Handles)
{
    Transform* root = Make("root");
    Transform* a = Make("a", root);
    Transform* b = Make("b", root);
    Make("a1", a);
    TransformHandle h = root->gameObject()->transformHandle();
    HX_CHECK(h.IsValid() && h.Get() == root && !h.HasParent() && h->childCount() == 2u);
    HX_CHECK(a->transformHandle().HasParent() && a->transformHandle().parent() == h && !TransformHandle::None().IsValid());
    Array<Transform*> kids;
    for (TransformHandle k : h.DirectChildren())
        kids.Push(k.Get());
    HX_CHECK(kids.Size() == 2u && kids[0] == a && kids[1] == b);
    TransformHandle::ChildEnumerator e = h.GetDirectChildrenEnumerator();
    HX_CHECK(e.MoveNext() && e.Current().Get() == a && e.MoveNext() && e.Current().Get() == b && !e.MoveNext());
    TransformHandle gone = b->transformHandle();
    DestroyImmediate(b->gameObject());
    HX_CHECK(!gone.IsValid() && gone.Get() == nullptr);
    Cleanup();
}

HX_TEST(Transform, TransferWritesAndReadsTheNode)
{
    Transform* root = Make("root");
    Transform* t = Make("t", root);
    t->SetLocalPosition(Vector3(1, 2, 3));
    t->SetLocalScale(Vector3(4, 5, 6));
    String text;
    serialization::yaml::Emitter emitter(text);
    serialization::WriteYaml(emitter, *t);
    HX_CHECK(text.View().Contains("m_LocalPosition") && text.View().Contains("m_Father") && text.View().Contains("m_Children"));
    serialization::yaml::Tree tree;
    String error;
    HX_REQUIRE(serialization::yaml::Parse(text, tree, error));
    Transform* back = Make("back");
    Array<String> errors;
    HX_CHECK(serialization::ReadYaml(tree, tree.documents.Empty() ? 0u : tree.documents[0].root, *back, errors));
    HX_CHECK(Near(back->localPosition(), Vector3(1, 2, 3)) && Near(back->localScale(), Vector3(4, 5, 6)) && back->parent() == nullptr);
    Cleanup();
}
