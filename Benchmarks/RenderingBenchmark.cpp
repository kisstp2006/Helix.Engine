#include <Helix/Engine/Asset/Asset.h>
#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Engine/Rendering/Graphics.h>
#include <Helix/Engine/Rendering/Implementation/RenderCore.h>
#include <Helix/Engine/Rendering/Light.h>
#include <Helix/Engine/Rendering/Pipeline/RenderPipeline.h>
#include <Helix/Engine/Rendering/Pipeline/ScriptableRenderContext.h>
#include <Helix/Engine/Rendering/Renderer.h>
#include <Helix/Jobs/Jobs.h>
#include <Helix/Engine/Transform/Hierarchy.h>
#include <Helix/Platform/IO/FileSystem.h>
#include <Helix/Testing/Bench.h>

#include <TestShadersAssembly.h>

#include <algorithm>
#include <chrono>
#include <cstdio>

using namespace helix;
using namespace helix::engine;

namespace
{
    constexpr u32 kObjects = 10000;

    Shader* Unlit()
    {
        static Shader* unlit = [] {
            String root = platform::JoinPath(platform::TempDirectory(), "helix-rendering-bench");
            platform::CreateDirectories(root);
            platform::WriteFile(platform::JoinPath(root, "HelixEngine.TestShaders.dll"), Span<const u8>(kTestShadersAssembly, sizeof(kTestShadersAssembly)));
            static FolderAssetSource source(root);
            AddAssetSource(&source);
            LoadAsset(FindAssetAtPath("HelixEngine.TestShaders.dll")->guid);
            return Shader::Find("Tests/Unlit");
        }();
        return unlit;
    }

    // 10000 quads on a 100 x 100 grid, half of them in front of an orthographic camera, 16 materials.
    struct QuadField
    {
        Array<EntityId> objects;
        Camera* camera = nullptr;
        QuadField()
        {
            Mesh* quad = NewObject<Mesh>();
            const Vector3 v[] = {{-0.5f, -0.5f, 0}, {-0.5f, 0.5f, 0}, {0.5f, 0.5f, 0}, {0.5f, -0.5f, 0}};
            const i32 t[] = {0, 1, 2, 0, 2, 3};
            quad->SetVertices(Span<const Vector3>(v, 4));
            quad->SetTriangles(Span<const i32>(t, 6));
            Material* materials[16];
            for (u32 i = 0; i < 16; i++)
            {
                materials[i] = Material::Create(Unlit());
                materials[i]->SetColor("color", Color(f32(i) / 16, 0.5f, 1, 1));
            }
            for (u32 i = 0; i < kObjects; i++)
            {
                GameObject* g = GameObject::Create("Quad");
                g->transform()->SetPosition(Vector3(f32(i % 100), f32(i / 100), 0));
                g->AddComponent<MeshFilter>()->SetSharedMesh(quad);
                g->AddComponent<MeshRenderer>()->SetSharedMaterial(materials[i % 16]);
                objects.Push(g->GetEntityId());
            }
            GameObject* c = GameObject::Create("Camera");
            c->transform()->SetPosition(Vector3(25, 50, -10));
            camera = c->AddComponent<Camera>();
            objects.Push(c->GetEntityId());
            camera->SetOrthographic(true);
            camera->SetOrthographicSize(50);
            camera->SetAspect(0.5f);
            UpdateWorldMatrices();
            Graphics::RenderFrame();
        }
        ~QuadField()
        {
            for (EntityId id : objects)
                if (Object* o = EntityIdToObject(id))
                    DestroyImmediate(o, true);
        }
    };
}

// Every renderer moved: world matrices, then the manager's bounds and the copy for the render side.
HX_BENCH(RendererManagerAllMoved)
{
    QuadField scene;
    state.SetItemsPerIteration(kObjects);
    f32 z = 0;
    Array<Transform*> transforms;
    for (EntityId id : scene.objects)
        if (GameObject* g = EntityIdToObject<GameObject>(id))
            transforms.Push(g->transform());
    double set = 0, world = 0, manager = 0;
    u32 rounds = 0;
    while (state.KeepRunning())
    {
        z += 0.001f;
        auto a = std::chrono::steady_clock::now();
        for (Transform* t : transforms)
            t->SetLocalPosition(Vector3(t->localPosition().x, t->localPosition().y, z));
        auto b = std::chrono::steady_clock::now();
        UpdateWorldMatrices();
        auto c = std::chrono::steady_clock::now();
        Graphics::RenderFrame();
        auto d = std::chrono::steady_clock::now();
        set += std::chrono::duration<double, std::nano>(b - a).count();
        world += std::chrono::duration<double, std::nano>(c - b).count();
        manager += std::chrono::duration<double, std::nano>(d - c).count();
        rounds++;
    }
    double per = double(rounds) * kObjects;
    std::printf("per renderer: set %.0f ns, world matrices %.0f ns, renderer manager %.0f ns\n", set / per, world / per, manager / per);
}

// Nothing moved: what a frame costs the manager.
HX_BENCH(RendererManagerNothingMoved)
{
    QuadField scene;
    state.SetItemsPerIteration(kObjects);
    while (state.KeepRunning())
        Graphics::RenderFrame();
}

// Culling, sorting and recording the draws of 5000 visible quads on a device (CPU time of RenderFrame).
HX_BENCH(PlaceholderFrame10000Renderers)
{
    rhi::Device* device = nullptr;
    for (rhi::Backend b : {rhi::Backend::D3D11, rhi::Backend::OpenGL})
        if (!device && rhi::Device::IsBackendAvailable(b))
            device = rhi::Device::Create({.backend = b, .vsync = false, .transientBufferSize = 32u << 20});
    if (!device)
    {
        std::printf("no device, skipped\n");
        while (state.KeepRunning())
        {
        }
        return;
    }
    constexpr u32 size = 256;
    rhi::TextureHandle color = device->CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget});
    rhi::TextureHandle depth = device->CreateTexture({.format = rhi::Format::D32Float, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
    Graphics::SetDevice(device);
    Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
    {
        QuadField scene;
        Graphics::RenderFrame();
        state.SetItemsPerIteration(kObjects);
        while (state.KeepRunning())
        {
            Graphics::RenderFrame();
            device->WaitIdle();
        }
        std::printf("visible %u, draw calls %u\n", Graphics::LastStats().visible, Graphics::LastStats().drawCalls);
    }
    Graphics::SetDevice(nullptr);
    device->Destroy(color);
    device->Destroy(depth);
    rhi::Device::Destroy(device);
}

namespace
{
    // Quads on a grid, 16 materials; the camera sees about half of them; a directional light with shadows.
    struct Field
    {
        Array<EntityId> objects;
        Camera* camera = nullptr;
        Field(u32 count, u32 columns)
        {
            Mesh* quad = NewObject<Mesh>();
            const Vector3 v[] = {{-0.5f, -0.5f, 0}, {-0.5f, 0.5f, 0}, {0.5f, 0.5f, 0}, {0.5f, -0.5f, 0}};
            const i32 t[] = {0, 1, 2, 0, 2, 3};
            quad->SetVertices(Span<const Vector3>(v, 4));
            quad->SetTriangles(Span<const i32>(t, 6));
            Material* materials[16];
            for (u32 i = 0; i < 16; i++)
            {
                materials[i] = Material::Create(Unlit());
                materials[i]->SetColor("color", Color(f32(i) / 16, 0.5f, 1, 1));
            }
            for (u32 i = 0; i < count; i++)
            {
                GameObject* g = GameObject::Create("Quad");
                g->transform()->SetPosition(Vector3(f32(i % columns), f32(i / columns), 0));
                g->AddComponent<MeshFilter>()->SetSharedMesh(quad);
                g->AddComponent<MeshRenderer>()->SetSharedMaterial(materials[i % 16]);
                objects.Push(g->GetEntityId());
            }
            f32 rows = f32((count + columns - 1) / columns);
            GameObject* c = GameObject::Create("Camera");
            c->transform()->SetPosition(Vector3(f32(columns) * 0.25f - 0.5f, rows * 0.5f - 0.5f, -10));
            camera = c->AddComponent<Camera>();
            objects.Push(c->GetEntityId());
            camera->SetOrthographic(true);
            camera->SetOrthographicSize(rows * 0.5f);
            camera->SetAspect(f32(columns) * 0.5f / rows);
            GameObject* sun = GameObject::Create("Sun");
            sun->transform()->SetRotation(Quaternion::Euler(50, 30, 0));
            Light* light = sun->AddComponent<Light>();
            light->SetType(LightType::Directional);
            light->SetShadows(LightShadows::Hard);
            objects.Push(sun->GetEntityId());
            UpdateWorldMatrices();
            Graphics::RenderFrame();
        }
        ~Field()
        {
            for (EntityId id : objects)
                if (Object* o = EntityIdToObject(id))
                    DestroyImmediate(o, true);
        }
    };

    // Frames of a field on a D3D11 device with the persistent blocks: the draw list and submit times per frame.
    void MeasureFrames(testing::BenchState& state, u32 count, const char* budget)
    {
        if (!jobs::IsInitialized())
            jobs::Initialize();
        rhi::Device* device = rhi::Device::IsBackendAvailable(rhi::Backend::D3D11)
                                  ? rhi::Device::Create({.backend = rhi::Backend::D3D11, .vsync = false, .transientBufferSize = 32u << 20})
                                  : nullptr;
        if (!device)
        {
            std::printf("no D3D11 device, skipped\n");
            while (state.KeepRunning())
            {
            }
            return;
        }
        constexpr u32 size = 256;
        rhi::TextureHandle color = device->CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget});
        rhi::TextureHandle depth = device->CreateTexture({.format = rhi::Format::D32Float, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
        GraphicsSettings::SetUseScriptableRenderPipelineBatching(true);
        {
            Field field(count, 200);
            Graphics::RenderFrame();
            // Each iteration draws 8 frames; the median and the best frame are printed beside the mean (the laptop's
            // clock and other work make the mean swing).
            Array<f64> lists, submits;
            state.SetItemsPerIteration(count / 2 * 8);
            while (state.KeepRunning())
                for (u32 f = 0; f < 8; f++)
                {
                    Graphics::RenderFrame();
                    device->WaitIdle();
                    lists.Push(Graphics::LastStats().drawListTime);
                    submits.Push(Graphics::LastStats().submitTime);
                }
            auto summary = [](Array<f64>& v, f64& mean, f64& median, f64& best) {
                f64 sum = 0;
                for (f64 x : v)
                    sum += x;
                std::sort(v.Data(), v.Data() + v.Size());
                mean = v.Empty() ? 0 : sum / f64(v.Size());
                median = v.Empty() ? 0 : v[v.Size() / 2];
                best = v.Empty() ? 0 : v[0];
            };
            f64 listMean, listMedian, listBest, submitMean, submitMedian, submitBest;
            summary(lists, listMean, listMedian, listBest);
            summary(submits, submitMean, submitMedian, submitBest);
            std::printf("%u draws (%u persistent), %u frames: draw list mean %.3f median %.3f best %.3f ms, submit mean %.3f median %.3f best %.3f ms "
                        "(budget %s)\n",
                        Graphics::LastStats().drawCalls, Graphics::LastStats().persistentDraws, u32(lists.Size()), listMean, listMedian, listBest,
                        submitMean, submitMedian, submitBest, budget);
        }
        GraphicsSettings::SetUseScriptableRenderPipelineBatching(false);
        Graphics::SetDevice(nullptr);
        device->Destroy(color);
        device->Destroy(depth);
        rhi::Device::Destroy(device);
    }
}

// Budget (13): culling 100k renderers with a shadowed directional light, 0.8 ms on the jobs.
HX_BENCH(BudgetCull100kRenderersAndShadowCasters)
{
    if (!jobs::IsInitialized())
        jobs::Initialize();
    Field field(100000, 400);
    ScriptableCullingParameters p;
    field.camera->TryGetCullingParameters(p);
    state.SetItemsPerIteration(100000);
    u32 visible = 0, casters = 0;
    while (state.KeepRunning())
    {
        ScriptableRenderContext context;
        CullingResults r = context.Cull(p);
        visible = u32(r.data->renderers.Size());
        casters = u32(r.ShadowCasters(0).Size());
    }
    std::printf("visible %u, shadow casters %u (budget 0.8 ms)\n", visible, casters);
}

// Budget (13): culling 100k renderers with 4 cascades, 0.8 ms on the jobs: the cull, then the shadow casters of the sun
// in each of four cascades as a pipeline culls them (the splits' matrices and planes, CullShadowCasters).
HX_BENCH(BudgetCull100kRenderersAnd4Cascades)
{
    if (!jobs::IsInitialized())
        jobs::Initialize();
    Field field(100000, 400);
    ScriptableCullingParameters p;
    field.camera->TryGetCullingParameters(p);
    p.shadowDistance = 400;
    state.SetItemsPerIteration(100000);
    u32 visible = 0, inSplits[4] = {};
    f64 cullTime = 0, splitTime = 0;
    u32 rounds = 0;
    while (state.KeepRunning())
    {
        ScriptableRenderContext context;
        auto start = std::chrono::steady_clock::now();
        CullingResults r = context.Cull(p);
        auto culled = std::chrono::steady_clock::now();
        visible = u32(r.data->renderers.Size());
        ShadowSplitData splits[4];
        for (i32 i = 0; i < 4; i++)
        {
            Matrix4x4 view, projection;
            r.ComputeDirectionalShadowMatricesAndCullingPrimitives(0, i, 4, Vector3(0.067f, 0.2f, 0.467f), 2048, 0, view, projection, splits[i]);
        }
        LightShadowCasterCullingInfo info;
        info.splitStart = 0;
        info.splitLength = 4;
        info.projectionType = BatchCullingProjectionType::Orthographic;
        context.CullShadowCasters(r, ShadowCastersCullingInfos{Span<const LightShadowCasterCullingInfo>(&info, 1), Span<const ShadowSplitData>(splits, 4)});
        auto done = std::chrono::steady_clock::now();
        cullTime += std::chrono::duration<f64, std::milli>(culled - start).count();
        splitTime += std::chrono::duration<f64, std::milli>(done - culled).count();
        rounds++;
        for (u32 i = 0; i < 4; i++)
            inSplits[i] = u32(r.data->splitCasters[i].Size());
    }
    std::printf("visible %u, casters per cascade %u %u %u %u: cull %.3f ms, cascades %.3f ms (budget 0.8 ms)\n", visible, inSplits[0], inSplits[1],
                inSplits[2], inSplits[3], cullTime / Max(rounds, 1u), splitTime / Max(rounds, 1u));
}

// Budget (13): draw list building and sorting of 20k draws, 0.6 ms.
HX_BENCH(BudgetDrawList20kDraws) { MeasureFrames(state, 40000, "20k draw list 0.6 ms"); }

// Budget (13): the render thread's submit of 5k draws on D3D11 with the SRP-batcher style path, 2.0 ms.
HX_BENCH(BudgetSubmit5kDraws) { MeasureFrames(state, 10000, "5k submit 2.0 ms"); }

// The sort of a 20k draw list alone (keys like DrawRenderers' with 16 materials and quantized distances).
HX_BENCH(DrawListSort20k)
{
    if (!jobs::IsInitialized())
        jobs::Initialize();
    Array<render::SortEntry> source, entries, scratch;
    u64 seed = 7;
    for (u32 i = 0; i < 20000; i++)
    {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        source.Push({u64(2000) << 48 | (seed >> 40 & 15) << 8, (seed >> 16 & 0xFFFFFFFF) << 32 | i});
    }
    state.SetItemsPerIteration(20000);
    while (state.KeepRunning())
    {
        entries = source;
        render::RadixSort(entries, scratch);
    }
}
