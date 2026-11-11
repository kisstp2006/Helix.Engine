#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Testing/Bench.h>

using namespace helix;
using namespace helix::engine;

namespace
{
    constexpr u32 kCount = 100000;

    class Mover final : public Behaviour
    {
        HX_ENGINE_CLASS(Mover, Behaviour, 1000100, ClassNone)
    };

    class Health final : public Component
    {
        HX_ENGINE_CLASS(Health, Component, 1000101, ClassNone)
    };
}

// Items are GameObjects (each with its Transform and two more components).
HX_BENCH(EngineCreateAndDestroyGameObjects)
{
    state.SetItemsPerIteration(kCount);
    Array<GameObject*> all;
    all.Reserve(kCount);
    while (state.KeepRunning())
    {
        all.Clear();
        for (u32 i = 0; i < kCount; i++)
        {
            GameObject* g = GameObject::Create("Enemy");
            g->AddComponent<Mover>();
            g->AddComponent<Health>();
            all.Push(g);
        }
        for (GameObject* g : all)
            DestroyImmediate(g);
    }
}

// Items are lookups.
HX_BENCH(EngineGetComponent)
{
    Array<GameObject*> all;
    for (u32 i = 0; i < 1000; i++)
    {
        GameObject* g = GameObject::Create("Enemy");
        g->AddComponent<Mover>();
        g->AddComponent<Health>();
        all.Push(g);
    }
    state.SetItemsPerIteration(u64(all.Size()) * 3);
    usize found = 0;
    while (state.KeepRunning())
        for (GameObject* g : all)
            found += (g->GetComponent<Health>() != nullptr) + (g->GetComponent<Behaviour>() != nullptr) + (g->GetComponent<Transform>() != nullptr);
    helix::testing::DoNotOptimize(found);
    for (GameObject* g : all)
        DestroyImmediate(g);
}

// Items are ids resolved.
HX_BENCH(EngineEntityIdToObject)
{
    Array<EntityId> ids;
    for (u32 i = 0; i < kCount; i++)
        ids.Push(GameObject::Create()->GetEntityId());
    state.SetItemsPerIteration(kCount);
    usize found = 0;
    while (state.KeepRunning())
        for (EntityId id : ids)
            found += EntityIdToObject(id) != nullptr;
    helix::testing::DoNotOptimize(found);
    for (EntityId id : ids)
        DestroyImmediate(EntityIdToObject(id));
}

// Items are objects returned.
HX_BENCH(EngineFindObjectsByType)
{
    Array<GameObject*> all;
    for (u32 i = 0; i < 10000; i++)
    {
        GameObject* g = GameObject::Create();
        g->AddComponent<Mover>();
        all.Push(g);
    }
    state.SetItemsPerIteration(10000);
    Array<Behaviour*> out;
    while (state.KeepRunning())
        FindObjectsByType(out);
    helix::testing::DoNotOptimize(out.Data());
    for (GameObject* g : all)
        DestroyImmediate(g);
}
