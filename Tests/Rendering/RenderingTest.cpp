#include <Support/Golden.h>
#include <Support/TestShaders.h>

#include <Helix/Core/Logging/Log.h>
#include <Helix/Engine/Rendering/Graphics.h>
#include <Helix/Engine/Rendering/Implementation/RenderCore.h>
#include <Helix/Engine/Rendering/Pipeline/RenderPipeline.h>
#include <Scripts/TestScripts.h>
#include <Helix/Engine/Script/ScriptRuntime.h>
#include <Helix/Scripting/Bindings/Interop.h>
#include <Helix/Engine/Rendering/Light.h>
#include <Helix/Engine/Rendering/Renderer.h>
#include <Helix/Engine/Rendering/ScreenCapture.h>
#include <Helix/Image/Codecs/Png.h>
#include <Helix/Engine/Transform/Hierarchy.h>
#include <Helix/Platform/Window/Window.h>
#include <Helix/Testing/Test.h>

#include <cmath>
#include <cstdio>

using namespace helix;
using namespace helix::engine;
using namespace helix::engine::testing;

namespace
{
    bool Near(f32 a, f32 b, f32 e = 1e-3f) { return std::fabs(a - b) < e; }
    bool Near(Vector3 a, Vector3 b, f32 e = 1e-3f) { return Near(a.x, b.x, e) && Near(a.y, b.y, e) && Near(a.z, b.z, e); }

    // A unit quad facing -z (toward a camera looking along +z), with uv and normals.
    Mesh* Quad()
    {
        Mesh* m = NewObject<Mesh>();
        m->SetName("Quad");
        const Vector3 v[] = {{-0.5f, -0.5f, 0}, {-0.5f, 0.5f, 0}, {0.5f, 0.5f, 0}, {0.5f, -0.5f, 0}};
        const Vector2 uv[] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
        const i32 t[] = {0, 1, 2, 0, 2, 3};
        m->SetVertices(Span<const Vector3>(v, 4));
        m->SetUVs(0, Span<const Vector2>(uv, 4));
        m->SetTriangles(Span<const i32>(t, 6));
        m->RecalculateNormals();
        return m;
    }

    GameObject* Thing(StringView name, Mesh* mesh, Material* material, Vector3 position, f32 scale = 1)
    {
        GameObject* g = GameObject::Create(name);
        g->transform()->SetPosition(position);
        g->transform()->SetLocalScale(Vector3(scale, scale, scale));
        g->AddComponent<MeshFilter>()->SetSharedMesh(mesh);
        g->AddComponent<MeshRenderer>()->SetSharedMaterial(material);
        return g;
    }

    Camera* MakeCamera(Vector3 position, f32 size)
    {
        GameObject* g = GameObject::Create("Camera");
        g->transform()->SetPosition(position);
        Camera* c = g->AddComponent<Camera>();
        c->SetOrthographic(true);
        c->SetOrthographicSize(size);
        c->SetClearFlags(CameraClearFlags::SolidColor);
        c->SetBackgroundColor(Color(0, 0, 0, 1));
        return c;
    }

    const char* BackendName(rhi::Backend b)
    {
        switch (b)
        {
        case rhi::Backend::D3D11: return "D3D11";
        case rhi::Backend::OpenGL: return "OpenGL";
        case rhi::Backend::OpenGLES: return "OpenGL ES";
        default: return "WebGL 2";
        }
    }

    // Runs f on every device: each D3D11 adapter, OpenGL, WebGL 2 where there is a window.
    template <class F>
    void ForEachDevice(F&& f)
    {
        for (rhi::Backend b : {rhi::Backend::D3D11, rhi::Backend::OpenGL, rhi::Backend::WebGL2})
        {
            if ((b == rhi::Backend::WebGL2 && !platform::Window::IsSupported()) || !rhi::Device::IsBackendAvailable(b))
                continue;
            char name[128];
            u32 adapters = 0;
            while (b == rhi::Backend::D3D11 && rhi::Device::AdapterName(b, adapters, name, sizeof(name)))
                adapters++;
            for (i32 adapter = adapters ? 0 : -1; adapter < i32(adapters); adapter++)
            {
                rhi::Device* device = rhi::Device::Create({.backend = b, .adapter = adapter, .vsync = false});
                if (!device)
                    continue;
                if (adapter >= 0)
                    rhi::Device::AdapterName(b, u32(adapter), name, sizeof(name));
                std::printf("    %s %s\n", BackendName(b), adapter >= 0 ? name : "");
                f(*device);
                Graphics::SetDevice(nullptr);
                rhi::Device::Destroy(device);
            }
        }
    }

    u32 Pixel(const Array<u32>& pixels, u32 size, u32 x, u32 y) { return pixels[y * size + x]; }

    bool NearColor(u32 a, u32 b, i32 tolerance = 3)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            i32 ca = i32((a >> shift) & 0xFF), cb = i32((b >> shift) & 0xFF);
            if (ca - cb > tolerance || cb - ca > tolerance)
                return false;
        }
        return true;
    }

    constexpr u32 Rgba(u32 r, u32 g, u32 b, u32 a = 255) { return r | (g << 8) | (b << 16) | (a << 24); }
}

HX_TEST(Rendering, CameraMatricesAndConversions)
{
    GameObject* g = GameObject::Create("Camera");
    g->transform()->SetPosition(Vector3(1, 2, -10));
    Camera* c = g->AddComponent<Camera>();
    Camera::SetDisplaySize(200, 100);
    HX_CHECK(Near(c->aspect(), 2.0f) && c->pixelWidth() == 200 && c->pixelHeight() == 100 && c->fieldOfView() == 60.0f);
    // The point in front of the camera's center is the screen's center, at its distance.
    Vector3 s = c->WorldToScreenPoint(Vector3(1, 2, 0));
    HX_CHECK(Near(s, Vector3(100, 50, 10)));
    Vector3 back = c->ScreenToWorldPoint(Vector3(30, 80, 7));
    HX_CHECK(Near(c->WorldToScreenPoint(back), Vector3(30, 80, 7)));
    // A point above the center is above it on screen (bottom left origin), right is right.
    HX_CHECK(c->WorldToViewportPoint(Vector3(1, 3, 0)).y > 0.5f && c->WorldToViewportPoint(Vector3(2, 2, 0)).x > 0.5f);
    Ray r = c->ViewportPointToRay(Vector3(0.5f, 0.5f, 0));
    HX_CHECK(Near(r.direction, Vector3(0, 0, 1)) && Near(r.origin, Vector3(1, 2, -10 + 0.3f)));
    Vector3 corners[4];
    c->CalculateFrustumCorners(Rect{0, 0, 1, 1}, 1, corners);
    HX_CHECK(Near(corners[0].z, -1) && corners[0].x < 0 && corners[0].y < 0 && corners[2].x > 0 && corners[2].y > 0);
    // Camera space looks along -z; the GPU projection takes depth 0 at the near plane and 1 at the far plane.
    HX_CHECK(Near(c->worldToCameraMatrix().MultiplyPoint3x4(Vector3(1, 2, 0)), Vector3(0, 0, -10)));
    Vector4 nearClip = c->GpuProjectionMatrix() * Vector4(0, 0, -c->nearClipPlane(), 1);
    Vector4 farClip = c->GpuProjectionMatrix() * Vector4(0, 0, -c->farClipPlane(), 1);
    HX_CHECK(Near(nearClip.z / nearClip.w, 0) && Near(farClip.z / farClip.w, 1, 1e-2f));
    // Orthographic: viewport positions are linear.
    c->SetOrthographic(true);
    c->SetOrthographicSize(5);
    HX_CHECK(Near(c->WorldToViewportPoint(Vector3(11, 2, 0)).x, 1.0f) && Near(c->ViewportToWorldPoint(Vector3(0, 0, 3)), Vector3(-9, -3, -7)));
    // The camera list and main.
    HX_CHECK(Camera::allCamerasCount() == 1 && Camera::main() == nullptr);
    g->SetTag("MainCamera");
    HX_CHECK(Camera::main() == c);
    c->SetEnabled(false);
    HX_CHECK(Camera::allCamerasCount() == 0 && Camera::main() == nullptr);
    Plane planes[6];
    c->SetEnabled(true);
    GeometryUtility::CalculateFrustumPlanes(*c, planes);
    HX_CHECK(GeometryUtility::TestPlanesAABB(planes, Bounds(Vector3(1, 2, 0), Vector3(1, 1, 1))) &&
             !GeometryUtility::TestPlanesAABB(planes, Bounds(Vector3(1, 2, -20), Vector3(1, 1, 1))));
    Camera::SetDisplaySize(1, 1);
    Cleanup();
}

HX_TEST(Rendering, RenderersFollowTheirTransforms)
{
    TestShaderFolder shaders;
    Mesh* quad = Quad();
    Material* m = Material::Create(Shader::Find("Tests/Unlit"));
    GameObject* g = Thing("A", quad, m, Vector3(5, 0, 0), 2);
    MeshRenderer* r = g->GetComponent<MeshRenderer>();
    HX_REQUIRE(r && r->ManagerIndex() != ~0u);
    HX_CHECK(Near(r->bounds().center, Vector3(5, 0, 0)) && Near(r->bounds().extents, Vector3(1, 1, 0)) && Near(r->localBounds().extents, Vector3(0.5f, 0.5f, 0)));
    g->transform()->SetPosition(Vector3(0, 3, 0));
    UpdateWorldMatrices();
    Graphics::RenderFrame();
    HX_CHECK(Near(r->bounds().center, Vector3(0, 3, 0)));
    // Material copies on first access; the shared one stays as it was.
    Material* own = r->material();
    HX_CHECK(own != m && r->sharedMaterial() == own && r->material() == own && own->Name().EndsWith("(Instance)"));
    own->SetColor("color", Color(1, 0, 0, 1));
    HX_CHECK(m->GetColor("color") == Color(1, 1, 1, 1));
    // Inactive GameObjects and disabled renderers leave the manager or stop drawing.
    g->SetActive(false);
    HX_CHECK(r->ManagerIndex() == ~0u);
    g->SetActive(true);
    HX_CHECK(r->ManagerIndex() != ~0u);
    // A bounds override stays until reset.
    r->SetBounds(Bounds(Vector3(), Vector3(10, 10, 10)));
    Graphics::RenderFrame();
    HX_CHECK(Near(r->bounds().extents, Vector3(5, 5, 5)));
    r->ResetBounds();
    Graphics::RenderFrame();
    HX_CHECK(Near(r->bounds().extents, Vector3(1, 1, 0)));
    // Property blocks.
    MaterialPropertyBlock block;
    block.SetColor("color", Color(0, 0, 1, 1));
    r->SetPropertyBlock(&block);
    MaterialPropertyBlock read;
    r->GetPropertyBlock(read);
    HX_CHECK(r->HasPropertyBlock() && read.GetColor("color") == Color(0, 0, 1, 1));
    // The copy goes with its renderer.
    EntityId ownId = own->GetEntityId();
    DestroyImmediate(g, true);
    HX_CHECK(EntityIdToObject(ownId) == nullptr);
}

namespace
{
    struct GoldenScene
    {
        Mesh* quad;
        Material* red;
    };

    // Two cameras and six quads of the golden image (32 pixels square).
    GoldenScene MakeGoldenScene()
    {
        // An orthographic camera 8 units wide at z -10 looking along +z: one unit is 4 pixels.
        MakeCamera(Vector3(0, 0, -10), 4);
        Mesh* quad = Quad();
        Material* red = Material::Create(Shader::Find("Tests/Unlit"));
        red->SetColor("color", Color(1, 0, 0, 1));
        Thing("Red", quad, red, Vector3(-2, 2, 0), 2);
        // A texture of four colors (rows bottom first), drawn with point filtering.
        Texture2D* checker = Texture2D::Create(2, 2, TextureFormat::RGBA32, false, true);
        const Color32 texels[] = {{255, 255, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 255, 255}};
        checker->SetPixels32(Span<const Color32>(texels, 4));
        checker->SetFilterMode(FilterMode::Point);
        checker->Apply(false);
        Material* textured = Material::Create(Shader::Find("Tests/Textured"));
        textured->SetMainTexture(checker);
        Thing("Textured", quad, textured, Vector3(2, 2, 0), 2);
        // Lit by a directional light shining along +z (toward the quad's front): fully lit plus ambient.
        GameObject* sun = GameObject::Create("Sun");
        Light* light = sun->AddComponent<Light>();
        light->SetType(LightType::Directional);
        light->SetIntensity(0.5f);
        Material* lambert = Material::Create(Shader::Find("Tests/Lambert"));
        Thing("Lit", quad, lambert, Vector3(-2, -2, 0), 2);
        // A broken shader draws magenta; a transparent quad blends over the red one.
        Thing("Broken", quad, Material::Create(Shader::Find("Tests/Broken")), Vector3(2, -2, 0), 2);
        Material* glass = Material::Create(Shader::Find("Tests/Transparent"));
        glass->SetColor("color", Color(0, 0, 1, 0.5f));
        Thing("Glass", quad, glass, Vector3(-1.5f, 2, -1), 1);
        // Culled: behind the camera, and on a layer the camera does not draw.
        Thing("Behind", quad, red, Vector3(0, 0, -20), 2);
        GameObject* hidden = Thing("Hidden", quad, red, Vector3(0, 0, 0), 1);
        hidden->SetLayer(8);
        Camera::main();
        Array<Camera*> cameras;
        Camera::GetAllCameras(cameras);
        cameras[0]->SetCullingMask(~(1u << 8));
        // A second camera on top (depth 1) in the top right corner: only layer 8, on green.
        Camera* corner = MakeCamera(Vector3(0, 0, -10), 1);
        corner->SetDepth(1);
        corner->SetRect(Rect{0.75f, 0.75f, 0.25f, 0.25f});
        corner->SetCullingMask(1u << 8);
        corner->SetBackgroundColor(Color(0, 1, 0, 1));
        return {quad, red};
    }
}

HX_TEST(Rendering, FallbackPipelineDrawsOnEveryDevice)
{
    constexpr u32 size = 32;
    u32 devices = 0;
    ForEachDevice([&](rhi::Device& device) {
        devices++;
        // The scene and the shaders go with each device (the folder's end destroys every object).
        TestShaderFolder shaders;
        HX_REQUIRE(Shader::Find("Tests/Unlit") && Shader::Find("Tests/Lambert"));
        rhi::TextureHandle color =
            device.CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
        rhi::TextureHandle depth = device.CreateTexture({.format = rhi::Format::D32Float, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(&device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
        Graphics::SetAmbientLight(Color(0.25f, 0.25f, 0.25f, 1));
        MakeGoldenScene();

        Graphics::RenderFrame();
        const RenderStats& stats = Graphics::LastStats();
        HX_CHECK(stats.cameras == 2 && stats.drawCalls == 6);
        Array<u32> pixels;
        pixels.Resize(size * size);
        HX_REQUIRE(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
        // Rows top first, 4 pixels a unit: the red quad top left, the textured one top right (its bottom texels yellow
        // and green, uv 0 at the bottom), lit bottom left (0.5 light + 0.25 ambient), magenta bottom right.
        HX_CHECK(NearColor(Pixel(pixels, size, 5, 5), Rgba(255, 0, 0)));
        HX_CHECK(NearColor(Pixel(pixels, size, 2, 2), Rgba(0, 0, 0)) && NearColor(Pixel(pixels, size, 11, 11), Rgba(255, 0, 0)));
        HX_CHECK(NearColor(Pixel(pixels, size, 20, 5), Rgba(0, 0, 255)) && NearColor(Pixel(pixels, size, 20, 10), Rgba(255, 255, 0)));
        HX_CHECK(NearColor(Pixel(pixels, size, 6, 26), Rgba(191, 191, 191)));
        HX_CHECK(NearColor(Pixel(pixels, size, 26, 26), Rgba(255, 0, 255)));
        // Half blue over red where the glass is (alpha blends too: 0.5 * 0.5 + 1 * 0.5).
        HX_CHECK(NearColor(Pixel(pixels, size, 10, 8), Rgba(128, 0, 128, 191)));
        // The corner camera: green with the layer 8 quad (the center) in it.
        HX_CHECK(NearColor(Pixel(pixels, size, 31, 0), Rgba(0, 255, 0)) && NearColor(Pixel(pixels, size, 28, 4), Rgba(255, 0, 0)));
        HX_CHECK(NearColor(Pixel(pixels, size, 16, 16), Rgba(0, 0, 0)));
        HX_CHECK(MatchGolden("FallbackScene", pixels.Data(), size, size));
        // A second frame makes nothing new.
        Graphics::RenderFrame();
        HX_CHECK(Graphics::LastStats().pipelinesCreated == 0 && Graphics::LastStats().uploads == 0);
        // Screenshots: the frame drawn again into a texture, at the screen's size and twice it.
        String shot = platform::JoinPath(platform::TempDirectory(), Format("helix-shot-{}.png", Guid::Generate().ToString()));
        String big = platform::JoinPath(platform::TempDirectory(), Format("helix-shot-{}.png", Guid::Generate().ToString()));
        ScreenCapture::CaptureScreenshot(shot);
        ScreenCapture::CaptureScreenshot(big, 2);
        HX_CHECK(ScreenCapture::PendingCaptures() == 2);
        Graphics::RenderFrame();
        HX_CHECK(ScreenCapture::PendingCaptures() == 0 && Graphics::LastStats().cameras == 2);
        Array<u8> png;
        image::Image img, bigImg;
        HX_REQUIRE(platform::ReadFile(shot, png) && image::DecodePng(Span<const u8>(png.Data(), png.Size()), img));
        HX_CHECK(img.width == size && img.height == size);
        bool same = true;
        for (u32 i = 0; i < size * size; i++)
        {
            u32 c = (pixels[i] & 0x00ffffffu) | 0xff000000u;
            u32 got;
            std::memcpy(&got, img.pixels.Data() + i * 4, 4);
            same = same && NearColor(got, c);
        }
        HX_CHECK(same);
        HX_REQUIRE(platform::ReadFile(big, png) && image::DecodePng(Span<const u8>(png.Data(), png.Size()), bigImg));
        HX_CHECK(bigImg.width == size * 2 && bigImg.height == size * 2);
        u32 bigRed;
        std::memcpy(&bigRed, bigImg.pixels.Data() + (10 * size * 2 + 10) * 4, 4);
        HX_CHECK(NearColor(bigRed, Rgba(255, 0, 0)));
        platform::RemoveFile(shot);
        platform::RemoveFile(big);
        Graphics::ReleaseResources();
        device.Destroy(color);
        device.Destroy(depth);
    });
    std::printf("    %u devices\n", devices);
}

HX_TEST(Rendering, BackFacesAreCulledOnEveryDevice)
{
    constexpr u32 size = 16;
    ForEachDevice([&](rhi::Device& device) {
        TestShaderFolder shaders;
        rhi::TextureHandle color =
            device.CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
        rhi::TextureHandle depth = device.CreateTexture({.format = rhi::Format::D32Float, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(&device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
        // One unit is 4 pixels. The quad is clockwise seen from the camera (its front); the copy with the triangles
        // reversed shows the camera its back and is not drawn.
        MakeCamera(Vector3(0, 0, -10), 2);
        Mesh* front = Quad();
        Mesh* back = Quad();
        const i32 reversed[] = {0, 2, 1, 0, 3, 2};
        back->SetTriangles(Span<const i32>(reversed, 6));
        back->RecalculateNormals();
        HX_CHECK(front->normals()[0].z < -0.9f && back->normals()[0].z > 0.9f);
        Material* red = Material::Create(Shader::Find("Tests/Unlit"));
        red->SetColor("color", Color(1, 0, 0, 1));
        Thing("Front", front, red, Vector3(-1, 0, 0), 2);
        Thing("Back", back, red, Vector3(1, 0, 0), 2);
        // A mesh scripts may not read (as model files make them) is drawn without an error in the log.
        front->UploadMeshData(true);
        u32 errors = 0;
        LogSink count = [](const LogMessage& m, void* user) {
            if (m.level >= LogLevel::Error)
                ++*static_cast<u32*>(user);
        };
        AddLogSink(count, &errors);
        Graphics::RenderFrame();
        RemoveLogSink(count, &errors);
        HX_CHECK(errors == 0 && !front->isReadable());
        Array<u32> pixels;
        pixels.Resize(size * size);
        HX_REQUIRE(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
        HX_CHECK(NearColor(Pixel(pixels, size, 4, 8), Rgba(255, 0, 0)));
        HX_CHECK(NearColor(Pixel(pixels, size, 12, 8), Rgba(0, 0, 0)));
        Graphics::ReleaseResources();
        device.Destroy(color);
        device.Destroy(depth);
    });
}

HX_BINDINGS_EXPORT void HxBindings_EngineTests_Publish();

namespace
{
    // The C++ pipeline asset of the test module (registered as a module linked into a player is, unloaded after).
    ScriptableObject* MakePipelineAsset(bool& registered)
    {
        if (!registered)
        {
            HxBindings_EngineTests_Publish();
            String error;
            registered = RegisterLinkedGameModule("EngineTests", error);
            if (!registered)
                std::printf("    %.*s\n", int(error.Size()), error.Data());
        }
        return ScriptableObject::CreateInstanceByName("TestPipelineAsset");
    }

    struct EventLog
    {
        Array<String> lines;
        u32 tokens[6] = {};

        EventLog()
        {
            tokens[0] = RenderPipelineManager::beginContextRendering().Add(
                [](void* u, ScriptableRenderContext&, Span<Camera* const> c) { static_cast<EventLog*>(u)->lines.Push(Format("beginContext:{}", c.Size())); }, this);
            tokens[1] = RenderPipelineManager::endContextRendering().Add(
                [](void* u, ScriptableRenderContext&, Span<Camera* const> c) { static_cast<EventLog*>(u)->lines.Push(Format("endContext:{}", c.Size())); }, this);
            tokens[2] = RenderPipelineManager::beginCameraRendering().Add(
                [](void* u, ScriptableRenderContext&, Camera* c) { static_cast<EventLog*>(u)->lines.Push(Format("beginCamera:{}", i32(c->depth()))); }, this);
            tokens[3] = RenderPipelineManager::endCameraRendering().Add(
                [](void* u, ScriptableRenderContext&, Camera* c) { static_cast<EventLog*>(u)->lines.Push(Format("endCamera:{}", i32(c->depth()))); }, this);
            tokens[4] = RenderPipelineManager::activeRenderPipelineAssetChanged().Add(
                [](void* u, ScriptableObject* from, ScriptableObject* to) {
                    static_cast<EventLog*>(u)->lines.Push(Format("asset:{}>{}", from ? "asset" : "none", to ? "asset" : "none"));
                },
                this);
            tokens[5] = RenderPipelineManager::activeRenderPipelineTypeChanged().Add([](void* u) { static_cast<EventLog*>(u)->lines.Push("type"); }, this);
        }

        ~EventLog()
        {
            RenderPipelineManager::beginContextRendering().Remove(tokens[0]);
            RenderPipelineManager::endContextRendering().Remove(tokens[1]);
            RenderPipelineManager::beginCameraRendering().Remove(tokens[2]);
            RenderPipelineManager::endCameraRendering().Remove(tokens[3]);
            RenderPipelineManager::activeRenderPipelineAssetChanged().Remove(tokens[4]);
            RenderPipelineManager::activeRenderPipelineTypeChanged().Remove(tokens[5]);
        }

        bool Take(std::initializer_list<const char*> expected)
        {
            bool same = lines.Size() == expected.size();
            usize i = 0;
            for (const char* e : expected)
                same = same && i < lines.Size() && lines[i++] == StringView(e);
            if (!same)
                for (const String& l : lines)
                    std::printf("    %.*s\n", int(l.Size()), l.Data());
            lines.Clear();
            return same;
        }
    };
}

HX_TEST(Rendering, ACppScriptablePipelineDrawsTheGoldenSceneOnEveryDevice)
{
    constexpr u32 size = 32;
    u32 devices = 0;
    bool registered = false;
    ForEachDevice([&](rhi::Device& device) {
        devices++;
        TestShaderFolder shaders;
        rhi::TextureHandle color =
            device.CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
        rhi::TextureHandle depth = device.CreateTexture({.format = rhi::Format::D32Float, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(&device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
        Graphics::SetAmbientLight(Color(0.25f, 0.25f, 0.25f, 1));
        GoldenScene scene = MakeGoldenScene();
        ScriptableObject* asset = MakePipelineAsset(registered);
        HX_REQUIRE(asset != nullptr);
        TestPipeline::made = TestPipeline::disposed = 0;
        EventLog events;

        // The asset's pipeline draws what the fallback drew: the golden image of the fallback test.
        GraphicsSettings::SetDefaultRenderPipeline(asset);
        HX_CHECK(GraphicsSettings::currentRenderPipeline() == asset && QualitySettings::GetRenderPipelineAssetAt(0) == nullptr);
        Graphics::RenderFrame();
        HX_CHECK(TestPipeline::made == 1 && RenderPipelineManager::currentAsset() == asset);
        HX_CHECK(events.Take({"asset:none>asset", "type", "beginContext:2", "beginCamera:0", "endCamera:0", "beginCamera:1", "endCamera:1", "endContext:2"}));
        HX_CHECK(Graphics::LastStats().cameras == 2 && Graphics::LastStats().drawCalls == 6);
        Array<u32> pixels;
        pixels.Resize(size * size);
        HX_REQUIRE(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
        HX_CHECK(MatchGolden("FallbackScene", pixels.Data(), size, size));
        // The sun is visible to the main camera (its layer 0 is not in the corner camera's mask): white at half intensity.
        HX_CHECK(TestPipeline::visibleLights == 1 && Near(TestPipeline::lightColor.r, 0.5f) && Near(TestPipeline::lightColor.b, 0.5f));

        // The same pipeline the next frame; results of the frame before are no longer valid.
        Graphics::RenderFrame();
        HX_CHECK(TestPipeline::made == 1 && !TestPipeline::lastWasValid);
        events.lines.Clear();

        // An override material: every quad red; a state block culling front faces: nothing.
        TestPipeline::overrideMaterial = scene.red;
        Graphics::RenderFrame();
        HX_REQUIRE(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
        HX_CHECK(NearColor(Pixel(pixels, size, 20, 10), Rgba(255, 0, 0)) && NearColor(Pixel(pixels, size, 26, 26), Rgba(255, 0, 0)));
        TestPipeline::overrideMaterial = nullptr;
        TestPipeline::state = RenderStateBlock(RenderStateMask::Raster);
        TestPipeline::state.rasterState.cullingMode = CullMode::Front;
        Graphics::RenderFrame();
        HX_REQUIRE(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
        HX_CHECK(NearColor(Pixel(pixels, size, 5, 5), Rgba(0, 0, 0)) && Graphics::LastStats().drawCalls == 6);
        TestPipeline::state = RenderStateBlock();
        // A layer mask without the default layer: only the corner's quad of layer 8.
        TestPipeline::layerMask = 1 << 8;
        Graphics::RenderFrame();
        HX_CHECK(Graphics::LastStats().drawCalls == 1);
        TestPipeline::layerMask = -1;

        // Commands run where the context runs them: globals and keywords change at Submit, meshes draw over the scene.
        CommandBuffer extra;
        extra.SetName("Extra");
        i32 value = PropertyToID("_PipelineTestValue");
        extra.SetGlobalFloat(value, 3);
        extra.EnableShaderKeyword("PIPELINE_TEST");
        extra.SetViewport(Rect{0, 0, f32(size), f32(size)});
        extra.DrawMesh(scene.quad, Matrix4x4::TRS(Vector3(2, -2, 0), Quaternion::identity, Vector3(2, 2, 2)), scene.red, 0, 0);
        extra.BeginSample("Extra");
        extra.EndSample("Extra");
        HX_CHECK(extra.name() == StringView("Extra") && extra.sizeInBytes() > 0);
        Shader::SetGlobalFloat(value, 0);
        Shader::DisableKeyword("PIPELINE_TEST");
        TestPipeline::extra = &extra;
        Graphics::RenderFrame();
        TestPipeline::extra = nullptr;
        HX_CHECK(Shader::GetGlobalFloat(value) == 3 && Shader::IsKeywordEnabled("PIPELINE_TEST"));
        HX_REQUIRE(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
        HX_CHECK(NearColor(Pixel(pixels, size, 26, 26), Rgba(255, 0, 0)));
        Shader::DisableKeyword("PIPELINE_TEST");
        extra.Clear();
        HX_CHECK(extra.sizeInBytes() == 0);

        // Graphics.ExecuteCommandBuffer runs at once over the whole target.
        CommandBuffer now;
        now.ClearRenderTarget(true, true, Color(0, 0, 1, 1));
        Graphics::ExecuteCommandBuffer(now);
        HX_REQUIRE(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
        HX_CHECK(NearColor(Pixel(pixels, size, 5, 5), Rgba(0, 0, 255)) && NearColor(Pixel(pixels, size, 31, 0), Rgba(0, 0, 255)));

        events.lines.Clear();
        // No asset: the fallback renders again (the old pipeline disposed), the asset and the type changed.
        GraphicsSettings::SetDefaultRenderPipeline(nullptr);
        Graphics::RenderFrame();
        HX_CHECK(TestPipeline::disposed == 1 && RenderPipelineManager::currentAsset() == nullptr);
        HX_CHECK(events.Take({"asset:asset>none", "type"}));
        HX_REQUIRE(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
        HX_CHECK(MatchGolden("FallbackScene", pixels.Data(), size, size));
        // The quality level's asset wins over the default.
        QualitySettings::SetRenderPipeline(asset);
        HX_CHECK(GraphicsSettings::currentRenderPipeline() == asset && QualitySettings::GetRenderPipelineAssetAt(0) == asset);
        Graphics::RenderFrame();
        HX_CHECK(TestPipeline::made == 2);
        QualitySettings::SetRenderPipeline(nullptr);
        Graphics::RenderFrame();
        HX_CHECK(TestPipeline::disposed == 2);
        events.lines.Clear();

        Object::DestroyImmediate(asset);
        Graphics::ReleaseResources();
        device.Destroy(color);
        device.Destroy(depth);
    });
    if (registered)
        UnloadGameModule("EngineTests");
    std::printf("    %u devices\n", devices);
}

HX_TEST(Rendering, CullingAndCommandBuffersCheckTheirArguments)
{
    // Without a device nothing runs, the values still hold.
    ScriptableRenderContext context;
    ScriptableCullingParameters p;
    p.cullingPlaneCount = 11;
    u32 errors = 0;
    LogSink count = [](const LogMessage& m, void* user) {
        if (m.level >= LogLevel::Error)
            ++*static_cast<u32*>(user);
    };
    AddLogSink(count, &errors);
    CullingResults bad = context.Cull(p);
    HX_CHECK(!bad.IsValid() && errors == 1);
    context.DrawRenderers(bad, DrawingSettings(), FilteringSettings());
    HX_CHECK(errors == 2);
    p.cullingPlaneCount = 0;
    CullingResults all = context.Cull(p);
    HX_CHECK(all.IsValid());
    CommandBuffer b;
    b.DrawMesh(nullptr, Matrix4x4::identity, nullptr);
    b.EndSample("none");
    HX_CHECK(errors == 3 && b.sizeInBytes() > 0);
    RemoveLogSink(count, &errors);
    HX_CHECK(CommandBuffer::Find(b.Id()) == &b && ScriptableRenderContext::Find(context.Id()) == &context);
    // Defaults as the reference has them.
    DrawingSettings d(ShaderTagId("SRPDefaultUnlit"), SortingSettings());
    HX_CHECK(d.GetShaderPassName(0) == ShaderTagId("SRPDefaultUnlit") && d.GetShaderPassName(1).id == 0 && d.enableInstancing && d.mainLightIndex == -1);
    FilteringSettings f = FilteringSettings::defaultValue();
    HX_CHECK(f.renderQueueRange.lowerBound == 0 && f.renderQueueRange.upperBound == 5000 && f.layerMask == -1 && f.renderingLayerMask == ~0u);
    HX_CHECK(RenderQueueRange::opaque().upperBound == 2500 && RenderQueueRange::transparent().lowerBound == 2501);
    RenderStateBlock state(RenderStateMask::Depth);
    HX_CHECK(state.depthState.writeEnabled && state.depthState.compareFunction == CompareFunction::Less && state.rasterState.cullingMode == CullMode::Back);
}

namespace
{
    // A render texture's sampled color as stored (rows bottom first): one texel.
    u32 RenderTextureTexel(rhi::Device& device, RenderTexture* rt, u32 mip, u32 x, u32 y)
    {
        const render::GpuRenderTarget* g = render::RenderTargetOf(*rt);
        if (!g)
            return 0xDEADBEEFu;
        u32 w = Max(1u, g->width >> mip), h = Max(1u, g->height >> mip);
        Array<u32> texels;
        texels.Resize(usize(w) * h);
        if (!device.ReadTexture(g->color, mip, texels.Data(), texels.Size() * 4))
            return 0xDEADBEEFu;
        return texels[usize(y) * w + x];
    }
}

HX_TEST(Rendering, RenderTexturesAsTargetsAndTextures)
{
    constexpr u32 size = 32;
    ForEachDevice([&](rhi::Device& device) {
        TestShaderFolder shaders;
        rhi::TextureHandle color =
            device.CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
        rhi::TextureHandle depth = device.CreateTexture({.format = rhi::Format::D32Float, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(&device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
        // A camera into a 16 x 16 render texture (four pixels a unit): green with a red quad in its top half's middle.
        Mesh* quad = Quad();
        Material* red = Material::Create(Shader::Find("Tests/Unlit"));
        red->SetColor("color", Color(1, 0, 0, 1));
        GameObject* top = Thing("Top", quad, red, Vector3(0, 1, 0), 2);
        top->SetLayer(8);
        Camera* into = MakeCamera(Vector3(0, 0, -10), 2);
        into->SetDepth(-1);
        into->SetCullingMask(1u << 8);
        into->SetBackgroundColor(Color(0, 1, 0, 1));
        RenderTexture* rt = RenderTexture::New(16, 16, 24);
        rt->SetFilterMode(FilterMode::Point);
        into->SetTargetTexture(rt);
        // The main camera draws a quad of side 4 textured with it at the screen's middle.
        Camera* main = MakeCamera(Vector3(0, 0, -10), 4);
        main->SetCullingMask(~(1u << 8));
        Material* textured = Material::Create(Shader::Find("Tests/Textured"));
        textured->SetMainTexture(rt);
        Thing("Screen", quad, textured, Vector3(0, 0, 0), 4);
        HX_CHECK(into->pixelWidth() == 16 && into->pixelHeight() == 16);

        auto check = [&](const char* what) {
            Graphics::RenderFrame();
            Array<u32> pixels;
            pixels.Resize(size * size);
            HX_REQUIRE(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
            // Rows top first: the texture's top half (red in the middle) is the quad's top half.
            bool ok = NearColor(Pixel(pixels, size, 16, 10), Rgba(255, 0, 0)) && NearColor(Pixel(pixels, size, 16, 20), Rgba(0, 255, 0)) &&
                      NearColor(Pixel(pixels, size, 9, 10), Rgba(0, 255, 0)) && NearColor(Pixel(pixels, size, 2, 2), Rgba(0, 0, 0));
            if (!ok)
                std::printf("    %s: %08x %08x %08x %08x\n", what, Pixel(pixels, size, 16, 10), Pixel(pixels, size, 16, 20), Pixel(pixels, size, 9, 10),
                            Pixel(pixels, size, 2, 2));
            HX_CHECK(ok);
        };
        check("render texture");
        // Its rows are kept bottom first, as a texture's: the red quad in the top rows.
        HX_CHECK(NearColor(RenderTextureTexel(device, rt, 0, 8, 12), Rgba(255, 0, 0)) && NearColor(RenderTextureTexel(device, rt, 0, 8, 3), Rgba(0, 255, 0)));

        // Multisampled: resolved into the sampled texture once drawn.
        RenderTexture* msaa = RenderTexture::New(16, 16, 24);
        msaa->SetAntiAliasing(4);
        msaa->SetFilterMode(FilterMode::Point);
        into->SetTargetTexture(msaa);
        textured->SetMainTexture(msaa);
        check("multisampled");

        // With mips made after drawing.
        RenderTexture* mipped = RenderTexture::New(16, 16, 24);
        mipped->SetUseMipMap(true);
        mipped->SetFilterMode(FilterMode::Point);
        into->SetTargetTexture(mipped);
        textured->SetMainTexture(mipped);
        check("mipped");
        HX_CHECK(mipped->mipmapCount() == 5);
        HX_CHECK(NearColor(RenderTextureTexel(device, mipped, 1, 4, 6), Rgba(255, 0, 0)) && NearColor(RenderTextureTexel(device, mipped, 1, 4, 1), Rgba(0, 255, 0)));

        // Blit and CopyTexture: the same texels.
        RenderTexture* blitted = RenderTexture::New(16, 16, 0);
        RenderTexture* copied = RenderTexture::New(16, 16, 0);
        Graphics::Blit(rt, blitted);
        Graphics::CopyTexture(rt, copied);
        for (u32 y : {3u, 12u})
            for (u32 x : {1u, 8u})
            {
                u32 want = RenderTextureTexel(device, rt, 0, x, y);
                u32 gotBlit = RenderTextureTexel(device, blitted, 0, x, y), gotCopy = RenderTextureTexel(device, copied, 0, x, y);
                if (!NearColor(gotBlit, want) || gotCopy != want)
                    std::printf("    %u,%u: want %08x blit %08x copy %08x\n", x, y, want, gotBlit, gotCopy);
                HX_CHECK(NearColor(gotBlit, want) && gotCopy == want);
            }

        // ConvertTexture: into half the size; into a slice of an array and back.
        RenderTexture* half = RenderTexture::New(8, 8, 0);
        HX_CHECK(Graphics::ConvertTexture(rt, half));
        HX_CHECK(NearColor(RenderTextureTexel(device, half, 0, 4, 6), Rgba(255, 0, 0)) && NearColor(RenderTextureTexel(device, half, 0, 4, 1), Rgba(0, 255, 0)));
        RenderTexture* slices = RenderTexture::New(16, 16, 0);
        slices->SetDimension(TextureDimension::Tex2DArray);
        slices->SetVolumeDepth(2);
        RenderTexture* back = RenderTexture::New(16, 16, 0);
        HX_CHECK(Graphics::ConvertTexture(rt, 0, slices, 1) && Graphics::ConvertTexture(slices, 1, back, 0));
        HX_CHECK(NearColor(RenderTextureTexel(device, back, 0, 8, 12), Rgba(255, 0, 0)) && NearColor(RenderTextureTexel(device, back, 0, 8, 3), Rgba(0, 255, 0)));
        HX_CHECK(!Graphics::ConvertTexture(rt, Texture2D::Create(4, 4, TextureFormat::RGBA32, false)));

        // A temporary array: its third slice cleared and copied out.
        {
            RenderTexture* slice = RenderTexture::New(8, 8, 0);
            const i32 id = Shader::PropertyToID("_Slices");
            CommandBuffer b;
            b.GetTemporaryRTArray(id, 8, 8, 3);
            b.SetRenderTarget(RenderTargetIdentifier(id), 0, CubemapFace::Unknown, 2);
            b.ClearRenderTarget(false, true, Color(0, 0, 1, 1));
            b.CopyTexture(RenderTargetIdentifier(id), 2, 0, RenderTargetIdentifier(slice), 0, 0);
            b.ReleaseTemporaryRT(id);
            Graphics::ExecuteCommandBuffer(b);
            HX_CHECK(NearColor(RenderTextureTexel(device, slice, 0, 3, 3), Rgba(0, 0, 255)));
        }

        // A standard request: the camera rendered into another texture by the fallback pipeline.
        RenderTexture* requested = RenderTexture::New(16, 16, 24);
        StandardRequest request{requested};
        HX_CHECK(RenderPipeline::SupportsRenderRequest(into, request));
        RenderPipeline::SubmitRenderRequest(into, request);
        HX_CHECK(NearColor(RenderTextureTexel(device, requested, 0, 8, 12), Rgba(255, 0, 0)) && NearColor(RenderTextureTexel(device, requested, 0, 8, 3), Rgba(0, 255, 0)));
        HX_CHECK(into->targetTexture() == mipped);

        // Temporaries: a released one is taken again; unused ones go after some frames.
        u32 before = RenderTexture::TemporaryCount();
        RenderTexture* a = RenderTexture::GetTemporary(8, 8);
        RenderTexture::ReleaseTemporary(a);
        RenderTexture* b = RenderTexture::GetTemporary(8, 8);
        HX_CHECK(a == b && RenderTexture::TemporaryCount() == before + 1);
        RenderTexture::ReleaseTemporary(b);
        for (u32 f = 0; f < 20; f++)
            Graphics::RenderFrame();
        HX_CHECK(RenderTexture::TemporaryCount() == 0);

        // A created texture keeps its size; released it takes a new one.
        u32 errors = 0;
        LogSink count = [](const LogMessage& m, void* user) {
            if (m.level >= LogLevel::Error)
                ++*static_cast<u32*>(user);
        };
        AddLogSink(count, &errors);
        rt->SetWidth(32);
        RemoveLogSink(count, &errors);
        HX_CHECK(errors == 1 && rt->width() == 16 && rt->IsCreated());
        rt->Release();
        rt->SetWidth(32);
        HX_CHECK(!rt->IsCreated() && rt->width() == 32);

        into->SetTargetTexture(nullptr);
        Graphics::ReleaseResources();
        device.Destroy(color);
        device.Destroy(depth);
    });
}
