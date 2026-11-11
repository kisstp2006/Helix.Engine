#include <Helix/Engine/Asset/Asset.h>
#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Engine/Rendering/Graphics.h>
#include <Helix/Engine/Rendering/Renderer.h>
#include <Helix/Engine/Transform/Hierarchy.h>
#include <Helix/Platform/IO/FileSystem.h>
#include <Helix/Testing/Bench.h>

#include <TestShadersAssembly.h>

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
