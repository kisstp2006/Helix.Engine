#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Jobs/Jobs.h>
#include <Helix/Testing/Bench.h>

using namespace helix;
using namespace helix::engine;

namespace
{
    // 2000 hierarchies of 50: a root, 7 children, 6 grandchildren each.
    constexpr u32 kRoots = 2000;
    constexpr u32 kPerRoot = 50;

    struct Forest
    {
        Array<Transform*> roots;
        Array<Transform*> all;

        Forest()
        {
            roots.Reserve(kRoots);
            all.Reserve(kRoots * kPerRoot);
            for (u32 r = 0; r < kRoots; r++)
            {
                Transform* root = GameObject::Create("root")->transform();
                root->SetHierarchyCapacity(kPerRoot);
                roots.Push(root);
                all.Push(root);
                for (u32 c = 0; c < 7; c++)
                {
                    Transform* child = GameObject::Create("child")->transform();
                    child->SetParent(root, false);
                    child->SetLocalPosition(Vector3(f32(c), 0, 0));
                    all.Push(child);
                    for (u32 g = 0; g < 6; g++)
                    {
                        Transform* leaf = GameObject::Create("leaf")->transform();
                        leaf->SetParent(child, false);
                        leaf->SetLocalPosition(Vector3(0, f32(g), 0));
                        all.Push(leaf);
                    }
                }
            }
            UpdateWorldMatrices();
        }

        ~Forest()
        {
            for (Transform* r : roots)
                DestroyImmediate(r->gameObject());
        }
    };
}

// Items are transforms: every local position set, then every world matrix brought up to date (in parallel).
HX_BENCH(TransformSetLocalAndUpdateWorld)
{
    jobs::Initialize();
    {
        Forest s;
        state.SetItemsPerIteration(s.all.Size());
        f32 x = 0;
        while (state.KeepRunning())
        {
            x += 0.001f;
            for (Transform* t : s.all)
                t->SetLocalPosition(Vector3(x, 1, 2));
            UpdateWorldMatrices();
        }
        helix::testing::DoNotOptimize(s.all[7]->position().x);
    }
    jobs::Shutdown();
}

// Items are transforms: a leaf moved and its world position read at once (the lazy path, up to the clean parent).
HX_BENCH(TransformSetPositionAndRead)
{
    Forest s;
    state.SetItemsPerIteration(s.all.Size());
    f32 sum = 0, x = 0;
    while (state.KeepRunning())
    {
        x += 0.001f;
        for (Transform* t : s.all)
        {
            t->SetLocalPosition(Vector3(x, 0, 0));
            sum += t->position().y;
        }
    }
    helix::testing::DoNotOptimize(sum);
}

// Items are moves: a subtree of 10 between two hierarchies of 1000.
HX_BENCH(TransformSetParentBetweenHierarchies)
{
    Transform* a = GameObject::Create("a")->transform();
    Transform* b = GameObject::Create("b")->transform();
    for (u32 i = 0; i < 999; i++)
    {
        GameObject::Create("x")->transform()->SetParent(a, false);
        GameObject::Create("y")->transform()->SetParent(b, false);
    }
    Transform* moving = GameObject::Create("moving")->transform();
    for (u32 i = 0; i < 9; i++)
        GameObject::Create("part")->transform()->SetParent(moving, false);
    moving->SetParent(a, false);
    state.SetItemsPerIteration(2);
    while (state.KeepRunning())
    {
        moving->SetParent(b, true);
        moving->SetParent(a, true);
    }
    DestroyImmediate(a->gameObject());
    DestroyImmediate(b->gameObject());
}

// Items are changed transforms visited: every root moved, then one system's changes dispatched over Helix.Jobs.
HX_BENCH(TransformDispatchChanges)
{
    jobs::Initialize();
    {
        Forest s;
        u32 system = RegisterTransformSystem("BenchRenderer");
        for (Transform* t : s.all)
            SetTransformInterest(t, system, true);
        DispatchTransformChanges(system, [](TransformHierarchy&, u32) {}, true);
        state.SetItemsPerIteration(s.all.Size());
        f32 x = 0;
        while (state.KeepRunning())
        {
            x += 0.001f;
            for (Transform* r : s.roots)
                r->SetLocalPosition(Vector3(x, 0, 0));
            DispatchTransformChanges(system, [](TransformHierarchy& h, u32 i) { helix::testing::DoNotOptimize(h.world[i].m03); }, true);
        }
    }
    jobs::Shutdown();
}

// Items are points.
HX_BENCH(TransformPoints)
{
    Transform* t = GameObject::Create("t")->transform();
    t->SetPositionAndRotation(Vector3(1, 2, 3), Quaternion::Euler(10, 20, 30));
    t->SetLocalScale(Vector3(2, 2, 2));
    Array<Vector3> points;
    points.Resize(100000, Vector3(1, 2, 3));
    Array<Vector3> out;
    out.Resize(points.Size());
    state.SetItemsPerIteration(points.Size());
    while (state.KeepRunning())
        t->TransformPoints(Span<const Vector3>(points.Data(), points.Size()), Span<Vector3>(out.Data(), out.Size()));
    helix::testing::DoNotOptimize(out.Data());
    DestroyImmediate(t->gameObject());
}
