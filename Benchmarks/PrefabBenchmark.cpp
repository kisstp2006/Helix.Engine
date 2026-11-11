#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Engine/Pool/ObjectPool.h>
#include <Helix/Engine/Prefab/Prefab.h>
#include <Helix/Engine/Scene/SceneFile.h>
#include <Helix/Engine/Scene/SceneManager.h>
#include <Helix/Testing/Bench.h>

#include <chrono>
#include <cstdio>

using namespace helix;
using namespace helix::engine;

namespace
{
    class Turret final : public Behaviour
    {
        HX_ENGINE_CLASS(Turret, Behaviour, 1000300, ClassNone)

    public:
        f32 range = 10;
        PPtr<Transform> barrel;
        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("range", range);
            v.Field("barrel", barrel);
        }
    };

    constexpr u32 kCopies = 1000;

    // 100 objects: a root and 32 children (each a GameObject, its Transform and a Turret pointing at the root), plus
    // the root's three.
    GameObject* MakeSource()
    {
        GameObject* root = GameObject::Create("Fort");
        root->AddComponent<Turret>()->barrel = PPtr<Transform>(root->transform());
        for (u32 i = 0; i < 32; i++)
        {
            GameObject* c = GameObject::Create("Tower");
            c->transform()->SetParent(root->transform(), false);
            c->transform()->SetLocalPosition(Vector3(f32(i), 0, 0));
            c->AddComponent<Turret>()->barrel = PPtr<Transform>(root->transform());
        }
        return root;
    }

    // The source written as a file and loaded as a prefab asset.
    GameObject* MakePrefab()
    {
        Scene s = SceneManager::CreateScene("PrefabSource");
        Scene active = SceneManager::GetActiveScene();
        SceneManager::SetActiveScene(s);
        MakeSource();
        String text, error;
        WriteSceneYaml(s, text, error);
        SceneManager::SetActiveScene(active);
        SceneManager::UnloadSceneAsync(s);
        SceneManager::ProcessSceneLoads();
        Array<String> errors;
        return LoadPrefab(StringView(text), errors);
    }

    void DestroyAll(Array<GameObject*>& made)
    {
        for (GameObject* g : made)
            DestroyImmediate(g);
        made.Clear();
    }
}

// Items are objects: 1000 copies of a 100 object prefab asset (through its template), then destroyed.
HX_BENCH(PrefabInstantiateAndDestroy)
{
    GameObject* prefab = MakePrefab();
    Array<GameObject*> made;
    made.Reserve(kCopies);
    state.SetItemsPerIteration(u64(kCopies) * 100);
    while (state.KeepRunning())
    {
        for (u32 i = 0; i < kCopies; i++)
            made.Push(Instantiate(prefab));
        DestroyAll(made);
    }
    UnloadPrefab(prefab);
}

// The same from a scene object (no template: a map per copy), for comparison in the same run.
HX_BENCH(SceneObjectInstantiateAndDestroy)
{
    GameObject* source = MakeSource();
    Array<GameObject*> made;
    made.Reserve(kCopies);
    state.SetItemsPerIteration(u64(kCopies) * 100);
    while (state.KeepRunning())
    {
        for (u32 i = 0; i < kCopies; i++)
            made.Push(Instantiate(source));
        DestroyAll(made);
    }
    DestroyImmediate(source);
}

// Items are objects: making and destroying 1000 copies timed apart; prints the share of each.
HX_BENCH(PrefabMakeThenDestroySplit)
{
    GameObject* prefab = MakePrefab();
    Array<GameObject*> made;
    made.Reserve(kCopies);
    state.SetItemsPerIteration(u64(kCopies) * 100);
    double make = 0, destroy = 0;
    u64 rounds = 0;
    while (state.KeepRunning())
    {
        auto a = std::chrono::steady_clock::now();
        for (u32 i = 0; i < kCopies; i++)
            made.Push(Instantiate(prefab));
        auto b = std::chrono::steady_clock::now();
        DestroyAll(made);
        auto c = std::chrono::steady_clock::now();
        make += std::chrono::duration<double, std::nano>(b - a).count();
        destroy += std::chrono::duration<double, std::nano>(c - b).count();
        rounds++;
    }
    std::printf("per object: make %.0f ns, destroy %.0f ns\n", make / double(rounds * kCopies * 100), destroy / double(rounds * kCopies * 100));
    UnloadPrefab(prefab);
}

// Items are copies: InstantiateAsync of 1000 under a 2 ms budget, frame by frame; prints the worst frame.
HX_BENCH(PrefabInstantiateAsyncBudget)
{
    GameObject* prefab = MakePrefab();
    AsyncInstantiateOperation::SetIntegrationTimeMS(2.0f);
    state.SetItemsPerIteration(kCopies);
    double worst = 0;
    u32 frames = 0;
    Array<Object*> made;
    while (state.KeepRunning())
    {
        AsyncInstantiateOperation op = InstantiateAsync(prefab, kCopies);
        while (!op.isDone())
        {
            auto a = std::chrono::steady_clock::now();
            ProcessAsyncInstantiations();
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
            worst = ms > worst ? ms : worst;
            frames++;
        }
        op.Result(made);
        for (Object* o : made)
            DestroyImmediate(o);
    }
    std::printf("InstantiateAsync of %u copies of 100 objects, 2 ms budget: worst frame %.2f ms, %u frames\n", kCopies, worst, frames);
    UnloadPrefab(prefab);
}

// Items are Get + Release pairs of pooled copies (against PrefabInstantiateAndDestroy per copy).
HX_BENCH(PrefabPoolGetRelease)
{
    GameObject* prefab = MakePrefab();
    PoolCallbacks<GameObject*> cb;
    cb.user = prefab;
    cb.create = [](void* p) { return Instantiate(static_cast<GameObject*>(p)); };
    cb.onGet = [](void*, GameObject*& g) { g->SetActive(true); };
    cb.onRelease = [](void*, GameObject*& g) { g->SetActive(false); };
    cb.onDestroy = [](void*, GameObject*& g) { DestroyImmediate(g); };
    {
        ObjectPool<GameObject*> pool(cb, false, kCopies, kCopies);
        Array<GameObject*> out;
        out.Reserve(kCopies);
        state.SetItemsPerIteration(kCopies);
        while (state.KeepRunning())
        {
            for (u32 i = 0; i < kCopies; i++)
                out.Push(pool.Get());
            for (GameObject* g : out)
                pool.Release(g);
            out.Clear();
        }
    }
    UnloadPrefab(prefab);
}
