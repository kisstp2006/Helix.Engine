#include <Support/TestClasses.h>

#include <Helix/Engine/Scene/SceneFile.h>
#include <Helix/Engine/Scene/SceneManager.h>
#include <Helix/Engine/Events/HelixEvent.h>
#include <Helix/Engine/Transform/RectTransform.h>
#include <Helix/Engine/Transform/RectTransformUtility.h>
#include <Helix/Testing/Test.h>

#include <cmath>

using namespace helix;
using namespace helix::engine;
using namespace helix::engine::testing;

namespace
{
    bool Near(f32 a, f32 b, f32 eps = 1e-3f) { return std::fabs(a - b) <= eps; }
    bool Near(Vector2 a, Vector2 b, f32 eps = 1e-3f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps); }
    bool Near(Vector3 a, Vector3 b, f32 eps = 1e-3f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps); }
    bool Near(Rect a, Rect b, f32 eps = 1e-3f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.width, b.width, eps) && Near(a.height, b.height, eps); }

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

    RectTransform* MakeRect(StringView name, RectTransform* parent = nullptr)
    {
        GameObject* g = GameObject::Create(name);
        auto* r = static_cast<RectTransform*>(g->AddComponent(ClassOf<RectTransform>()));
        if (parent)
            r->SetParent(parent, false);
        return r;
    }
}

HX_TEST(RectTransform, ItReplacesTheTransformKeepingTheHierarchy)
{
    GameObject* parent = GameObject::Create("parent");
    GameObject* g = GameObject::Create("g");
    GameObject* child = GameObject::Create("child");
    g->transform()->SetParent(parent->transform(), false);
    child->transform()->SetParent(g->transform(), false);
    g->transform()->SetLocalPosition(Vector3(3, 4, 5));
    EntityId old = g->transform()->GetEntityId();
    Component* added = g->AddComponent(ClassOf<RectTransform>());
    HX_REQUIRE(added != nullptr && added->Is<RectTransform>());
    auto* r = static_cast<RectTransform*>(added);
    // The same place: parent, child, local values; the old Transform is gone.
    HX_CHECK(g->transform() == r && r->parent() == parent->transform() && r->childCount() == 1u && r->GetChild(0) == child->transform());
    HX_CHECK(child->transform()->parent() == r && Near(r->localPosition(), Vector3(3, 4, 5)) && !EntityIdIsValid(old));
    HX_CHECK(g->GetComponent<Transform>() == r && g->GetComponent<RectTransform>() == r);
    // Its defaults: centered anchors and pivot, 100 by 100; a second one is refused.
    HX_CHECK(Near(r->anchorMin(), Vector2(0.5f, 0.5f)) && Near(r->anchorMax(), Vector2(0.5f, 0.5f)) && Near(r->sizeDelta(), Vector2(100, 100)));
    HX_CHECK(g->AddComponent(ClassOf<RectTransform>()) == nullptr);
    DestroyImmediate(parent);
    Cleanup();
}

HX_TEST(RectTransform, AnchorsSizePivotAndTheAnchoredPosition)
{
    RectTransform* root = MakeRect("root");
    root->SetSizeDelta(Vector2(400, 300));
    HX_CHECK(Near(root->rect(), Rect(-200, -150, 400, 300)));
    // A child stretched over its parent less 10 on each side.
    RectTransform* child = MakeRect("child", root);
    child->SetAnchorMin(Vector2(0, 0));
    child->SetAnchorMax(Vector2(1, 1));
    child->SetSizeDelta(Vector2(-20, -20));
    child->SetAnchoredPosition(Vector2(0, 0));
    HX_CHECK(Near(child->rect(), Rect(-190, -140, 380, 280)) && Near(child->localPosition(), Vector3(0, 0, 0)));
    HX_CHECK(Near(child->offsetMin(), Vector2(10, 10)) && Near(child->offsetMax(), Vector2(-10, -10)));
    // A child anchored at the parent's top right with its pivot there: the anchored position is from that corner.
    RectTransform* corner = MakeRect("corner", root);
    corner->SetPivotAndAnchors(Vector2(1, 1));
    corner->SetSizeDelta(Vector2(50, 20));
    corner->SetAnchoredPosition(Vector2(-5, -5));
    HX_CHECK(Near(corner->localPosition(), Vector3(195, 145, 0)) && Near(corner->rect(), Rect(-50, -20, 50, 20)));
    // The parent grows: the corner keeps its anchored position, the stretched child its offsets.
    i32 heard = 0;
    u32 token = RectTransform::dimensionsChanged().Add([](void* u, RectTransform*) { ++*static_cast<i32*>(u); }, &heard);
    root->SetSizeDelta(Vector2(600, 300));
    HX_CHECK(Near(corner->anchoredPosition(), Vector2(-5, -5)) && Near(corner->localPosition(), Vector3(295, 145, 0)));
    HX_CHECK(Near(child->rect(), Rect(-290, -140, 580, 280)) && heard == 2);
    RectTransform::dimensionsChanged().Remove(token);
    // Offsets and anchored position 3D.
    child->SetOffsetMin(Vector2(20, 30));
    HX_CHECK(Near(child->offsetMin(), Vector2(20, 30)) && Near(child->offsetMax(), Vector2(-10, -10)));
    child->SetAnchoredPosition3D(Vector3(1, 2, 3));
    HX_CHECK(Near(child->anchoredPosition3D(), Vector3(1, 2, 3)) && Near(child->localPosition().z, 3));
    // The local position moves the anchored position with it.
    corner->SetLocalPosition(Vector3(290, 140, 0));
    HX_CHECK(Near(corner->anchoredPosition(), Vector2(-10, -10)));
    DestroyImmediate(root->gameObject());
    Cleanup();
}

HX_TEST(RectTransform, SettersEdgesCornersAndFitting)
{
    RectTransform* root = MakeRect("root");
    root->SetSizeDelta(Vector2(400, 200));
    RectTransform* r = MakeRect("r", root);
    // Pinned to the parent's left edge, 10 in, 80 wide.
    r->SetInsetAndSizeFromParentEdge(RectTransform::Edge::Left, 10, 80);
    HX_CHECK(Near(r->anchorMin().x, 0) && Near(r->anchorMax().x, 0) && Near(r->GetLocalLeft(), -190) && Near(r->GetLocalRight(), -110));
    r->SetInsetAndSizeFromParentEdge(RectTransform::Edge::Top, 5, 40);
    HX_CHECK(Near(r->GetLocalTop(), 95) && Near(r->GetLocalBottom(), 55));
    // A size with the current anchors (stretched: the size less the anchors' share).
    r->SetAnchorMin(Vector2(0, 0));
    r->SetAnchorMax(Vector2(0.5f, 1));
    r->SetSizeWithCurrentAnchors(RectTransform::Axis::Horizontal, 150);
    HX_CHECK(Near(r->rect().width, 150) && Near(r->sizeDelta().x, -50));
    // Edges: moved keeping the size, or alone.
    r->SetAnchors(Vector2(0.5f, 0.5f));
    r->SetSizeDelta(Vector2(100, 50));
    r->SetAnchoredPosition(Vector2(0, 0));
    r->SetLocalTop(40, true);
    HX_CHECK(Near(r->GetLocalTop(), 40) && Near(r->rect().height, 50));
    r->SetLocalRight(70, false);
    HX_CHECK(Near(r->GetLocalRight(), 70) && Near(r->GetLocalLeft(), -50) && Near(r->rect().width, 120));
    // Corners: bottom left, top left, top right, bottom right; the world ones through the transform.
    Vector3 local[4], world[4];
    r->GetLocalCorners(local);
    r->GetWorldCorners(world);
    Rect rr = r->rect();
    HX_CHECK(Near(local[0], Vector3(rr.x, rr.y, 0)) && Near(local[2], Vector3(rr.x + rr.width, rr.y + rr.height, 0)));
    root->SetLocalPosition(Vector3(1000, 0, 0));
    r->GetWorldCorners(world);
    HX_CHECK(Near(world[0].x, local[0].x + r->localPosition().x + 1000));
    HX_CHECK(root->Contains(r) && !r->Contains(root));
    // The pivot moved with the rectangle staying put.
    Rect before = r->GetWorldRect();
    r->SetPivotWithCounterAdjust(Vector2(0, 0));
    HX_CHECK(Near(r->GetWorldRect(), before) && Near(r->pivot(), Vector2(0, 0)));
    // Fitting inside a coplanar rectangle: moved in; too large is refused unless it may shrink.
    RectTransform* box = MakeRect("box", root);
    box->SetSizeDelta(Vector2(100, 100));
    box->SetAnchoredPosition(Vector2(0, 0));
    RectTransform* item = MakeRect("item", root);
    item->SetSizeDelta(Vector2(30, 30));
    item->SetAnchoredPosition(Vector2(80, 0));
    HX_CHECK(item->IsCoplanarWith(box) && item->TryFitInsideCoplanarRectTransform(box) == RectTransform::FitResult::Success);
    HX_CHECK(item->FitInsideCoplanarRectTransform(box) == RectTransform::FitResult::Success && box->Contains(item));
    HX_CHECK(item->FitInsideCoplanarRectTransform(box) == RectTransform::FitResult::AlreadyInside);
    item->SetSizeDelta(Vector2(150, 30));
    HX_CHECK(item->FitInsideCoplanarRectTransform(box) == RectTransform::FitResult::FailLargerThanTarget);
    HX_CHECK(item->FitInsideCoplanarRectTransform(box, true) == RectTransform::FitResult::Success && Near(item->rect().width, 100));
    item->SetLocalPosition(Vector3(0, 0, 50));
    HX_CHECK(!item->IsCoplanarWith(box) && item->FitInsideCoplanarRectTransform(box) == RectTransform::FitResult::FailNotCoplanar);
    DestroyImmediate(root->gameObject());
    Cleanup();
}

HX_TEST(RectTransform, DriversClaimTheirPropertiesAndFilesKeepTheRectangle)
{
    KeepOnlyActive();
    RectTransform* root = MakeRect("root");
    RectTransform* r = MakeRect("r", root);
    GameObject* driver = GameObject::Create("driver");
    DrivenRectTransformTracker tracker;
    tracker.Add(driver, r, DrivenTransformProperties::SizeDelta);
    HX_CHECK(r->drivenByObject() == driver && r->drivenProperties() == DrivenTransformProperties::SizeDelta);
    tracker.Add(driver, r, DrivenTransformProperties::AnchoredPosition);
    HX_CHECK(i32(r->drivenProperties()) == (i32(DrivenTransformProperties::SizeDelta) | i32(DrivenTransformProperties::AnchoredPosition)));
    tracker.Clear();
    HX_CHECK(r->drivenByObject() == nullptr && r->drivenProperties() == DrivenTransformProperties::None);
    // Written and read back: anchors, size, pivot and the anchored position.
    root->SetSizeDelta(Vector2(300, 200));
    r->SetAnchorMin(Vector2(0, 1));
    r->SetAnchorMax(Vector2(0, 1));
    r->SetPivot(Vector2(0, 1));
    r->SetSizeDelta(Vector2(60, 30));
    r->SetAnchoredPosition(Vector2(12, -8));
    Scene s = SceneManager::GetActiveScene();
    String text, error;
    HX_REQUIRE(WriteSceneYaml(s, text, error));
    HX_CHECK(text.View().Contains("--- !h!224 &") && text.View().Contains("m_AnchoredPosition:"));
    Scene t = SceneManager::CreateScene("RectCopy");
    Array<String> errors;
    HX_REQUIRE(ReadSceneFile(StringView(text), t, errors));
    HX_CHECK(errors.Empty());
    RectTransform* copy = nullptr;
    for (GameObject* g : t.GetRootGameObjects())
        if (g->Name() == StringView("root"))
            copy = static_cast<RectTransform*>(g->transform()->GetChild(0));
    HX_REQUIRE(copy != nullptr && copy->Is<RectTransform>());
    HX_CHECK(Near(copy->anchoredPosition(), Vector2(12, -8)) && Near(copy->sizeDelta(), Vector2(60, 30)) && Near(copy->pivot(), Vector2(0, 1)));
    HX_CHECK(Near(copy->rect(), Rect(0, -30, 60, 30)));
    SceneManager::UnloadSceneAsync(t);
    KeepOnlyActive();
    Cleanup();
}

HX_TEST(RectTransform, TheUtilityConvertsScreenPointsFlipsAndMeasures)
{
    // An overlay canvas's world is the screen: no camera.
    RectTransform* root = MakeRect("root");
    root->SetSizeDelta(Vector2(200, 100));
    root->SetLocalPosition(Vector3(150, 80, 0));
    Vector2 local;
    HX_CHECK(RectTransformUtility::ScreenPointToLocalPointInRectangle(root, Vector2(160, 90), nullptr, local) && Near(local, Vector2(10, 10)));
    HX_CHECK(RectTransformUtility::RectangleContainsScreenPoint(root, Vector2(240, 120)) && !RectTransformUtility::RectangleContainsScreenPoint(root, Vector2(260, 120)));
    // A padding of 50 on the right.
    HX_CHECK(!RectTransformUtility::RectangleContainsScreenPoint(root, Vector2(240, 120), nullptr, Vector4(0, 0, 50, 0)));
    HX_CHECK(RectTransformUtility::WorldToScreenPoint(nullptr, Vector3(3, 4, 5)) == Vector2(3, 4));
    Ray ray = RectTransformUtility::ScreenPointToRay(nullptr, Vector2(7, 8));
    HX_CHECK(Near(ray.origin, Vector3(7, 8, -100)) && Near(ray.direction, Vector3(0, 0, 1)));
    // Turned 90 degrees about y: the screen's ray runs along its plane and misses it.
    root->SetLocalRotation(Quaternion::Euler(0, 90, 0));
    Vector3 world;
    HX_CHECK(!RectTransformUtility::ScreenPointToWorldPointInRectangle(root, Vector2(150, 80), nullptr, world));
    root->SetLocalRotation(Quaternion::identity);
    // Bounds of a hierarchy in root's space.
    RectTransform* child = MakeRect("child", root);
    child->SetSizeDelta(Vector2(20, 20));
    child->SetAnchoredPosition(Vector2(120, 0));
    Bounds b = RectTransformUtility::CalculateRelativeRectTransformBounds(root);
    HX_CHECK(Near(b.min(), Vector3(-100, -50, 0)) && Near(b.max(), Vector3(130, 50, 0)));
    // Flipped on x: the pivot, anchors and anchored position mirrored.
    child->SetAnchorMin(Vector2(0, 0));
    child->SetAnchorMax(Vector2(0.25f, 1));
    child->SetPivot(Vector2(0.2f, 0.5f));
    child->SetAnchoredPosition(Vector2(15, 0));
    RectTransformUtility::FlipLayoutOnAxis(child, 0, false, false);
    HX_CHECK(Near(child->pivot(), Vector2(0.8f, 0.5f)) && Near(child->anchorMin(), Vector2(0.75f, 0)) && Near(child->anchorMax(), Vector2(1, 1)));
    HX_CHECK(Near(child->anchoredPosition(), Vector2(-15, 0)));
    // The axes swapped.
    child->SetSizeDelta(Vector2(10, 30));
    RectTransformUtility::FlipLayoutAxes(child, true, false);
    HX_CHECK(Near(child->sizeDelta(), Vector2(30, 10)) && Near(child->pivot(), Vector2(0.5f, 0.8f)));
    DestroyImmediate(root->gameObject());
    Cleanup();
}

namespace
{
    HelixEvent<Vector3>* gOwnerEvent = nullptr;
}

HX_TEST(HelixEvent, PersistentCallsReachMethodsAndSettersByName)
{
    GameObject* target = GameObject::Create("target");
    HelixEvent<Vector3> e;
    // A bound method with the call's own argument, a property setter with the event's.
    PersistentCall off;
    off.target = PPtr<Object>(target);
    off.methodName = String("SetActive");
    off.mode = i32(PersistentListenerMode::Bool);
    off.arguments.boolArgument = false;
    PersistentCall scale;
    scale.target = PPtr<Object>(target->transform());
    scale.methodName = String("set_localScale");
    scale.mode = i32(PersistentListenerMode::EventDefined);
    e.persistentCalls().calls.Push(off);
    e.persistentCalls().calls.Push(scale);
    Vector3 heard(0, 0, 0);
    e.listeners().Add([](void* u, Vector3 v) { *static_cast<Vector3*>(u) = v; }, &heard);
    e.Invoke(Vector3(2, 3, 4));
    HX_CHECK(!target->activeSelf() && Near(target->transform()->localScale(), Vector3(2, 3, 4)) && Near(heard, Vector3(2, 3, 4)));
    // A call turned off does not run.
    target->SetActive(true);
    e.persistentCalls().calls[0].callState = i32(HelixEventCallState::Off);
    e.Invoke(Vector3(1, 1, 1));
    HX_CHECK(target->activeSelf());
    // A component's events by index, for C#: their persistent calls and their raise.
    gOwnerEvent = &e;
    RegisterNativeEvent(ClassOf<GameObject>(), 7, {[](Object*) -> HelixEventStorage* { return gOwnerEvent; },
                                                   [](Object*, Span<const scripting::Variant> args) {
                                                       Vector3 v(0, 0, 0);
                                                       args[0].Get(v);
                                                       gOwnerEvent->Invoke(v);
                                                   }});
    HX_CHECK(HelixEventNative::PersistentCount(target, 7) == 2 && HelixEventNative::PersistentTarget(target, 7, 1) == target->transform());
    HX_CHECK(HelixEventNative::PersistentMethodName(target, 7, 0) == StringView("SetActive"));
    HX_CHECK(HelixEventNative::GetPersistentState(target, 7, 0) == HelixEventCallState::Off && HelixEventNative::PersistentCount(target, 8) == 0);
    HelixEventNative::SetPersistentState(target, 7, 0, HelixEventCallState::RuntimeOnly);
    HelixEventNative::RaiseVector2(target, 7, Vector2(5, 6));
    HX_CHECK(!target->activeSelf());
    DestroyImmediate(target);
    Cleanup();
}
