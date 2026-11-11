#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Engine/Scene/SceneFile.h>
#include <Helix/Engine/Scene/SceneManager.h>
#include <Helix/Testing/Bench.h>

using namespace helix;
using namespace helix::engine;

namespace
{
    class Spinner final : public Behaviour
    {
        HX_ENGINE_CLASS(Spinner, Behaviour, 1000200, ClassNone)

    public:
        f32 speed = 90;
        PPtr<Transform> pivot;
        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("speed", speed);
            v.Field("pivot", pivot);
        }
    };

    // 10000 GameObjects: 1000 roots with 9 children each, every one with a Transform and a Spinner pointing at its root.
    constexpr u32 kGameObjects = 10000;

    Scene MakeScene(StringView name)
    {
        Scene s = SceneManager::CreateScene(name);
        Scene active = SceneManager::GetActiveScene();
        SceneManager::SetActiveScene(s);
        for (u32 r = 0; r < kGameObjects / 10; r++)
        {
            GameObject* root = GameObject::Create("root");
            root->AddComponent<Spinner>()->pivot = PPtr<Transform>(root->transform());
            for (u32 c = 0; c < 9; c++)
            {
                GameObject* child = GameObject::Create("child");
                child->transform()->SetParent(root->transform(), false);
                child->transform()->SetLocalPosition(Vector3(f32(c), 0.5f, -1));
                child->AddComponent<Spinner>()->pivot = PPtr<Transform>(root->transform());
            }
        }
        SceneManager::SetActiveScene(active);
        return s;
    }

    void Empty(Scene s)
    {
        for (GameObject* g : s.GetRootGameObjects())
            DestroyImmediate(g);
    }

    void Drop(Scene s)
    {
        SceneManager::UnloadSceneAsync(s);
        SceneManager::ProcessSceneLoads();
    }
}

// Items are GameObjects (each with its Transform and a component).
HX_BENCH(SceneWriteYaml)
{
    Scene s = MakeScene("BenchWrite");
    String text, error;
    state.SetItemsPerIteration(kGameObjects);
    while (state.KeepRunning())
        WriteSceneYaml(s, text, error);
    helix::testing::DoNotOptimize(text.Data());
    Drop(s);
}

// Items are GameObjects: the file parsed, its objects made, read, linked and enabled, then destroyed again.
HX_BENCH(SceneReadYaml)
{
    Scene s = MakeScene("BenchReadYaml");
    String text, error;
    WriteSceneYaml(s, text, error);
    Empty(s);
    state.SetItemsPerIteration(kGameObjects);
    Array<String> errors;
    while (state.KeepRunning())
    {
        ReadSceneFile(StringView(text), s, errors);
        Empty(s);
    }
    Drop(s);
}

HX_BENCH(SceneWriteBinary)
{
    Scene s = MakeScene("BenchWriteBinary");
    Array<u8> bytes;
    String error;
    state.SetItemsPerIteration(kGameObjects);
    while (state.KeepRunning())
        WriteSceneBinary(s, bytes, error);
    helix::testing::DoNotOptimize(bytes.Data());
    Drop(s);
}

HX_BENCH(SceneReadBinary)
{
    Scene s = MakeScene("BenchReadBinary");
    Array<u8> bytes;
    String error;
    WriteSceneBinary(s, bytes, error);
    Empty(s);
    state.SetItemsPerIteration(kGameObjects);
    Array<String> errors;
    while (state.KeepRunning())
    {
        ReadSceneFile(Span<const u8>(bytes.Data(), bytes.Size()), s, errors);
        Empty(s);
    }
    Drop(s);
}

// Items are GameObjects: the cost of making and destroying them alone, to tell the reading apart.
HX_BENCH(SceneMakeAndDestroyOnly)
{
    state.SetItemsPerIteration(kGameObjects);
    while (state.KeepRunning())
    {
        Scene s = MakeScene("BenchMake");
        Drop(s);
    }
}
