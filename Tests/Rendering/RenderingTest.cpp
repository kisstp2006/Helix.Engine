#include <Support/Devices.h>
#include <Support/Golden.h>
#include <Support/TestShaders.h>

#include <Helix/Core/Logging/Log.h>
#include <Helix/Engine/Rendering/Graphics.h>
#include <Helix/Engine/Rendering/GL.h>
#include <Helix/Engine/Rendering/Implementation/RenderCore.h>
#include <Helix/Engine/Rendering/Pipeline/RenderPipeline.h>
#include <Scripts/TestScripts.h>
#include <Helix/Engine/Script/ScriptRuntime.h>
#include <Helix/Scripting/Bindings/Interop.h>
#include <Helix/Engine/Rendering/CullingGroup.h>
#include <Helix/Engine/Rendering/AsyncGPUReadback.h>
#include <Helix/Engine/Rendering/LODGroup.h>
#include <Helix/Engine/Rendering/ReflectionProbe.h>
#include <Helix/Engine/Scene/SceneFile.h>
#include <Helix/Engine/Rendering/SystemInfo.h>
#include <Helix/Engine/Rendering/Light.h>
#include <Helix/Engine/Rendering/Implementation/RendererManager.h>
#include <Helix/Engine/Rendering/Pipeline/ScriptableRenderContext.h>
#include <Helix/Engine/Rendering/GraphicsBuffer.h>
#include <Helix/Engine/Rendering/Renderer.h>
#include <Helix/Engine/Rendering/RenderSettings.h>
#include <Helix/Engine/Rendering/ScreenCapture.h>
#include <Helix/Image/Codecs/Png.h>
#include <Helix/Engine/Transform/Hierarchy.h>
#include <Helix/Platform/Window/Window.h>
#include <Helix/Jobs/Jobs.h>
#include <Helix/Testing/Test.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
    // GL's: the same for the screen, upside down for a texture.
    Matrix4x4 screen = GL::GetGPUProjectionMatrix(c->projectionMatrix(), false), texture = GL::GetGPUProjectionMatrix(c->projectionMatrix(), true);
    Vector4 up = Vector4(0, 1, -5, 1);
    HX_CHECK(Near((screen * up).z, (c->GpuProjectionMatrix() * up).z) && Near((texture * up).y, -(screen * up).y) && (screen * up).y > 0);
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

HX_TEST(Rendering, RenderStatesComeFromMaterialPropertiesOnEveryDevice)
{
    // One shader, two materials: the first culls back faces and overwrites (its property defaults), the second culls
    // nothing and blends by alpha; a quad facing the camera and one facing away from it.
    constexpr u32 size = 16;
    ForEachDevice([&](rhi::Device& device) {
        TestShaderFolder shaders;
        rhi::TextureHandle color =
            device.CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
        rhi::TextureHandle depth = device.CreateTexture({.format = rhi::Format::D32Float, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(&device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
        MakeCamera(Vector3(0, 0, -10), 2);
        Mesh* front = Quad();
        Mesh* back = Quad();
        const i32 reversed[] = {0, 2, 1, 0, 3, 2};
        back->SetTriangles(Span<const i32>(reversed, 6));
        Material* solid = Material::Create(Shader::Find("Tests/PropertyStates"));
        solid->SetColor("color", Color(1, 0, 0, 1));
        Material* blended = Material::Create(Shader::Find("Tests/PropertyStates"));
        blended->SetColor("color", Color(0, 1, 0, 0.5f));
        blended->SetFloat("_Cull", f32(CullMode::Off));
        blended->SetFloat("_ZWrite", 0);
        blended->SetFloat("_SrcBlend", f32(BlendMode::SrcAlpha));
        blended->SetFloat("_DstBlend", f32(BlendMode::OneMinusSrcAlpha));
        GameObject* a = Thing("Front", front, solid, Vector3(-1, 0, 0), 2);
        GameObject* b = Thing("Back", back, solid, Vector3(1, 0, 0), 2);
        Array<u32> pixels;
        pixels.Resize(size * size);
        Graphics::RenderFrame();
        HX_REQUIRE(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
        HX_CHECK(NearColor(Pixel(pixels, size, 4, 8), Rgba(255, 0, 0)));
        HX_CHECK(NearColor(Pixel(pixels, size, 12, 8), Rgba(0, 0, 0)));
        a->GetComponent<MeshRenderer>()->SetSharedMaterial(blended);
        b->GetComponent<MeshRenderer>()->SetSharedMaterial(blended);
        Graphics::RenderFrame();
        HX_REQUIRE(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
        // Alpha blends too (the two factor Blend takes the color factors for alpha): 0.5 * 0.5 + 0.5.
        HX_CHECK(NearColor(Pixel(pixels, size, 4, 8), Rgba(0, 128, 0, 191)));
        HX_CHECK(NearColor(Pixel(pixels, size, 12, 8), Rgba(0, 128, 0, 191)));
        // A changed property changes the state of the next frame.
        blended->SetFloat("_Cull", f32(CullMode::Back));
        Graphics::RenderFrame();
        HX_REQUIRE(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
        HX_CHECK(NearColor(Pixel(pixels, size, 4, 8), Rgba(0, 128, 0, 191)));
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

namespace
{
    // The names of the renderers in a list of render data indices, sorted.
    String NamesOf(Span<const u32> indices)
    {
        const RenderData& d = RendererManager::Get().Render();
        Array<String> names;
        for (u32 i : indices)
            if (Renderer* r = EntityIdToObject<Renderer>(d.renderer[i]))
                names.Push(String(r->gameObject()->Name()));
        std::sort(names.begin(), names.end(), [](const String& a, const String& b) { return StringView(a) < StringView(b); });
        String all;
        for (const String& n : names)
        {
            if (!all.Empty())
                all.Append(',');
            all.Append(n);
        }
        return all;
    }
}

HX_TEST(Culling, LODsLayerDistancesLightsAndShadowCasters)
{
    TestShaderFolder shaders;
    Mesh* quad = Quad();
    Material* m = Material::Create(Shader::Find("Tests/Unlit"));
    Array<GameObject*> made;
    // A perspective camera at the origin looking along +z.
    Camera* camera = MakeCamera(Vector3(0, 0, 0), 5);
    camera->SetOrthographic(false);
    camera->SetFieldOfView(60);
    made.Push(camera->gameObject());
    // A LOD group 10 units away: a detailed and a coarse quad (the coarse one also on the second level).
    GameObject* group = GameObject::Create("Group");
    group->transform()->SetPosition(Vector3(0, 0, 10));
    made.Push(group);
    GameObject* detailed = Thing("Detailed", quad, m, Vector3(0, 0, 10));
    GameObject* coarse = Thing("Coarse", quad, m, Vector3(0, 0, 10));
    made.Push(detailed), made.Push(coarse);
    LODGroup* lods = group->AddComponent<LODGroup>();
    lods->SetSize(2);
    Renderer* rd = detailed->GetComponent<Renderer>();
    Renderer* rc = coarse->GetComponent<Renderer>();
    // At 10 units with 60 degrees the group's 2 units are about 0.17 of the screen's height.
    LOD levels[2] = {LOD(0.3f, Span<Renderer* const>(&rd, 1)), LOD(0.05f, Span<Renderer* const>(&rc, 1))};
    lods->SetLODs(Span<const LOD>(levels, 2));
    // Layer 9 culled beyond 5 units; a far quad on it and a near one.
    GameObject* far = Thing("Far", quad, m, Vector3(1, 0, 20));
    far->SetLayer(9);
    GameObject* near = Thing("Near", quad, m, Vector3(-1, 0, 3));
    near->SetLayer(9);
    made.Push(far), made.Push(near);
    // A shadow caster behind the camera (toward the light), one that casts no shadows, one only casting shadows.
    GameObject* behind = Thing("Behind", quad, m, Vector3(0, 0, -5));
    GameObject* noShadow = Thing("NoShadow", quad, m, Vector3(2, 0, 8));
    noShadow->GetComponent<Renderer>()->SetShadowCastingMode(ShadowCastingMode::Off);
    GameObject* shadowOnly = Thing("ShadowOnly", quad, m, Vector3(-2, 0, 8));
    shadowOnly->GetComponent<Renderer>()->SetShadowCastingMode(ShadowCastingMode::ShadowsOnly);
    made.Push(behind), made.Push(noShadow), made.Push(shadowOnly);
    // A directional light shining along +z (casters behind the camera count) and two point lights.
    GameObject* sun = GameObject::Create("Sun");
    Light* sunLight = sun->AddComponent<Light>();
    sunLight->SetType(LightType::Directional);
    sunLight->SetShadows(LightShadows::Hard);
    GameObject* lamp = GameObject::Create("Lamp");
    lamp->transform()->SetPosition(Vector3(0, 0, 8));
    Light* lampLight = lamp->AddComponent<Light>();
    lampLight->SetRange(3);
    lampLight->SetShadows(LightShadows::Soft);
    GameObject* lamp2 = GameObject::Create("Lamp2");
    lamp2->transform()->SetPosition(Vector3(0, 0, -30));
    lamp2->AddComponent<Light>()->SetRange(1);
    made.Push(sun), made.Push(lamp), made.Push(lamp2);
    UpdateWorldMatrices();
    Graphics::RenderFrame();

    ScriptableRenderContext context;
    auto cull = [&](auto&& edit) -> CullingResults {
        ScriptableCullingParameters p;
        HX_CHECK(camera->TryGetCullingParameters(p));
        edit(p);
        return context.Cull(p);
    };
    CullingResults all = cull([](ScriptableCullingParameters&) {});
    HX_CHECK(NamesOf(Span<const u32>(all.data->renderers.Data(), all.data->renderers.Size())) == StringView("Coarse,Far,Near,NoShadow"));
    // Closer: the detailed level.
    group->transform()->SetPosition(Vector3(0, 0, 4));
    UpdateWorldMatrices();
    Graphics::RenderFrame();
    CullingResults closer = cull([](ScriptableCullingParameters&) {});
    String names = NamesOf(Span<const u32>(closer.data->renderers.Data(), closer.data->renderers.Size()));
    HX_CHECK(StringView(names).Contains("Detailed") && !StringView(names).Contains("Coarse"));
    // The maximum level skips the detailed one.
    QualitySettings::SetMaximumLODLevel(1);
    CullingResults coarseOnly = cull([](ScriptableCullingParameters&) {});
    names = NamesOf(Span<const u32>(coarseOnly.data->renderers.Data(), coarseOnly.data->renderers.Size()));
    HX_CHECK(StringView(names).Contains("Coarse") && !StringView(names).Contains("Detailed"));
    QualitySettings::SetMaximumLODLevel(0);
    // Layer distances, planar and spherical.
    CullingResults layered = cull([](ScriptableCullingParameters& p) { p.layerCullDistances[9] = 5; });
    names = NamesOf(Span<const u32>(layered.data->renderers.Data(), layered.data->renderers.Size()));
    HX_CHECK(StringView(names).Contains("Near") && !StringView(names).Contains("Far"));
    // Lights: the sun and the lamp in range of the view, at most two; then only one (the directional one first).
    HX_CHECK(all.visibleLights().Size() == 2);
    CullingResults oneLight = cull([](ScriptableCullingParameters& p) { p.maximumVisibleLights = 1; });
    HX_CHECK(oneLight.visibleLights().Size() == 1 && oneLight.visibleLights()[0].lightType == LightType::Directional);
    // Shadow casters: the sun's include the one behind the camera and the shadow only one; the lamp's are near it.
    String sunCasters, lampCasters;
    for (i32 i = 0; i < 2; i++)
    {
        String n = NamesOf(closer.ShadowCasters(i));
        (closer.visibleLights()[u32(i)].lightType == LightType::Directional ? sunCasters : lampCasters) = n;
    }
    std::printf("    sun: %s; lamp: %s\n", sunCasters.CStr(), lampCasters.CStr());
    HX_CHECK(StringView(sunCasters).Contains("Behind") && StringView(sunCasters).Contains("ShadowOnly") && !StringView(sunCasters).Contains("NoShadow"));
    HX_CHECK(StringView(lampCasters).Contains("ShadowOnly") && !StringView(lampCasters).Contains("Behind") && !StringView(lampCasters).Contains("Far"));
    // Without the option there are none.
    CullingResults noCasters = cull([](ScriptableCullingParameters& p) { p.cullingOptions = CullingOptions::None; });
    HX_CHECK(noCasters.ShadowCasters(0).Empty());

    for (GameObject* g : made)
        DestroyImmediate(g);
}

HX_TEST(Culling, ReflectionProbesBlendCullAndSort)
{
    // Two probes around a quad and one far away: the closest two by importance (the smaller box between equals),
    // weighted by how deep the quad's center is inside each; the camera sees two of the three, sorted by the culling's
    // criteria; their per object count; a probe in a scene file.
    TestShaderFolder shaders;
    Mesh* quad = Quad();
    Material* m = Material::Create(Shader::Find("Tests/Unlit"));
    Array<GameObject*> made;
    Camera* camera = MakeCamera(Vector3(0, 0, -10), 4);
    made.Push(camera->gameObject());
    auto probe = [&](StringView name, Vector3 position, f32 size, i32 importance) {
        GameObject* g = GameObject::Create(name);
        g->transform()->SetPosition(position);
        ReflectionProbe* p = g->AddComponent<ReflectionProbe>();
        p->SetSize(Vector3(size, size, size));
        p->SetImportance(importance);
        p->SetBlendDistance(1);
        made.Push(g);
        return p;
    };
    ReflectionProbe* big = probe("Big", Vector3(0, 0, 0), 4, 1);
    ReflectionProbe* small = probe("Small", Vector3(0, 0, 0), 2, 1);
    ReflectionProbe* far = probe("Far", Vector3(20, 0, 0), 2, 5);
    GameObject* thing = Thing("Reflecting", quad, m, Vector3(1.5f, 0, 0));
    made.Push(thing);
    Renderer* r = thing->GetComponent<Renderer>();
    UpdateWorldMatrices();
    Graphics::RenderFrame();
    Array<ReflectionProbeBlendInfo> found;
    auto closest = [&](ReflectionProbeUsage usage) {
        r->SetReflectionProbeUsage(usage);
        r->GetClosestReflectionProbes(found);
        String text;
        for (const ReflectionProbeBlendInfo& f : found)
            text += Format("{}:{} ", f.probe ? f.probe->gameObject()->Name() : StringView("-"), i32(std::lround(f.weight * 100)));
        return text;
    };
    // The center 0.5 outside the small box (blend 1: half) and inside the big one.
    String blend = closest(ReflectionProbeUsage::BlendProbes);
    big->SetImportance(2);
    String important = closest(ReflectionProbeUsage::BlendProbes);
    String simple = closest(ReflectionProbeUsage::Simple);
    String off = closest(ReflectionProbeUsage::Off);
    big->SetImportance(1);
    // 0.75 outside the big box only: a quarter, the rest to the skybox, or all of it when blending probes.
    thing->transform()->SetPosition(Vector3(2.75f, 0, 0));
    UpdateWorldMatrices();
    String skybox = closest(ReflectionProbeUsage::BlendProbesAndSkybox);
    String alone = closest(ReflectionProbeUsage::BlendProbes);
    thing->transform()->SetPosition(Vector3(1.5f, 0, 0));
    r->SetReflectionProbeUsage(ReflectionProbeUsage::BlendProbes);
    UpdateWorldMatrices();
    Graphics::RenderFrame();
    bool blends = blend == StringView("Small:50 Big:50 ") && important == StringView("Big:100 Small:0 ") && simple == StringView("Big:100 ") &&
                  off.Empty() && skybox == StringView("Big:25 ") && alone == StringView("Big:100 ");
    if (!blends)
        std::printf("    %s| %s| %s| %s| %s| %s\n", blend.CStr(), important.CStr(), simple.CStr(), off.CStr(), skybox.CStr(), alone.CStr());
    HX_CHECK(blends);

    Cubemap* cube = Cubemap::Create(4, TextureFormat::RGBA32, false);
    big->SetMode(ReflectionProbeMode::Custom);
    big->SetCustomBakedTexture(cube);
    big->SetIntensity(2);
    ScriptableRenderContext context;
    auto order = [&](ReflectionProbeSortingCriteria criteria, i32 mask = -1) {
        ScriptableCullingParameters p;
        HX_CHECK(camera->TryGetCullingParameters(p));
        p.reflectionProbeSortingCriteria = criteria;
        p.cullingMask = mask;
        CullingResults c = context.Cull(p);
        String text;
        for (const VisibleReflectionProbe& v : c.visibleReflectionProbes())
            text += Format("{} ", v.reflectionProbe() ? v.reflectionProbe()->gameObject()->Name() : StringView("-"));
        return text;
    };
    big->SetImportance(2);
    String byImportance = order(ReflectionProbeSortingCriteria::Importance);
    String bySize = order(ReflectionProbeSortingCriteria::Size);
    String both = order(ReflectionProbeSortingCriteria::ImportanceThenSize);
    String unsorted = order(ReflectionProbeSortingCriteria::None);
    small->gameObject()->SetLayer(9);
    String layered = order(ReflectionProbeSortingCriteria::None, ~(1 << 9));
    small->gameObject()->SetLayer(0);
    ScriptableCullingParameters p;
    HX_CHECK(camera->TryGetCullingParameters(p));
    CullingResults c = context.Cull(p);
    const VisibleReflectionProbe* bigSeen = nullptr;
    for (const VisibleReflectionProbe& v : c.visibleReflectionProbes())
        if (v.reflectionProbe() == big)
            bigSeen = &v;
    bool seen = byImportance == StringView("Big Small ") && bySize == StringView("Small Big ") && both == StringView("Big Small ") &&
                unsorted == StringView("Big Small ") && layered == StringView("Big ") && bigSeen && bigSeen->texture() == cube &&
                bigSeen->hdrData.x == 2 && bigSeen->bounds.size() == Vector3(4, 4, 4) && bigSeen->importance == 2 && bigSeen->blendDistance == 1 &&
                c.lightAndReflectionProbeIndexCount() == c.lightIndexCount() + 2;
    if (!seen)
        std::printf("    %s| %s| %s| %s| %s| %d\n", byImportance.CStr(), bySize.CStr(), both.CStr(), unsorted.CStr(), layered.CStr(),
                    c.lightAndReflectionProbeIndexCount());
    HX_CHECK(seen);
    // The indices themselves into a buffer: the probes' are indices among the visible ones.
    {
        i32 count = c.lightAndReflectionProbeIndexCount();
        GraphicsBuffer* indices = GraphicsBuffer::Create(GraphicsBufferTarget::Structured, 0, count, 4);
        GraphicsBuffer* small1 = GraphicsBuffer::Create(GraphicsBufferTarget::Structured, 0, 1, 4);
        HX_REQUIRE(indices && small1);
        HX_CHECK(c.FillLightAndReflectionProbeIndices(indices));
        Array<i32> got;
        got.Resize(usize(count));
        HX_CHECK(indices->GetData(got.Data(), u32(count) * 4, 0));
        bool probes = count >= 2 && got[usize(count) - 2] >= 0 && got[usize(count) - 2] < 2 && got[usize(count) - 1] >= 0 && got[usize(count) - 1] < 2 &&
                      got[usize(count) - 2] != got[usize(count) - 1];
        HX_CHECK(probes);
        u32 errors = 0;
        LogSink count1 = [](const LogMessage& m, void* user) {
            if (m.level >= LogLevel::Error)
                ++*static_cast<u32*>(user);
        };
        AddLogSink(count1, &errors);
        HX_CHECK((count <= 1 || !c.FillLightAndReflectionProbeIndices(small1)) && !CullingResults().FillLightAndReflectionProbeIndices(indices));
        RemoveLogSink(count1, &errors);
        HX_CHECK(errors == (count <= 1 ? 1u : 2u));
        GraphicsBuffer::Release(indices);
        GraphicsBuffer::Release(small1);
    }
    HX_CHECK(far->bounds().center == Vector3(20, 0, 0) && ReflectionProbe::minBakedCubemapResolution() == 16);

    // The file form: a probe written into a scene and read back.
    Scene start = SceneManager::GetActiveScene();
    Scene written = SceneManager::CreateScene("ProbeWritten");
    SceneManager::SetActiveScene(written);
    GameObject* saved = GameObject::Create("Saved");
    ReflectionProbe* sp = saved->AddComponent<ReflectionProbe>();
    sp->SetSize(Vector3(3, 4, 5));
    sp->SetCenter(Vector3(1, 0, 0));
    sp->SetBoxProjection(true);
    sp->SetMode(ReflectionProbeMode::Realtime);
    sp->SetImportance(7);
    sp->SetClearFlags(ReflectionProbeClearFlags::SolidColor);
    String yaml, error;
    HX_REQUIRE(WriteSceneYaml(written, yaml, error));
    Scene read = SceneManager::CreateScene("ProbeRead");
    Array<String> errors;
    HX_REQUIRE(ReadSceneFile(StringView(yaml), read, errors));
    Array<GameObject*> roots = read.GetRootGameObjects();
    ReflectionProbe* back = roots.Size() == 1 ? roots[0]->GetComponent<ReflectionProbe>() : nullptr;
    HX_CHECK(StringView(yaml).Contains("ReflectionProbe:") && StringView(yaml).Contains("m_BoxSize"));
    HX_CHECK(back && back->size() == Vector3(3, 4, 5) && back->center() == Vector3(1, 0, 0) && back->boxProjection() &&
             back->mode() == ReflectionProbeMode::Realtime && back->importance() == 7 && back->clearFlags() == ReflectionProbeClearFlags::SolidColor);
    SceneManager::SetActiveScene(start);
    SceneManager::UnloadSceneAsync(written);
    SceneManager::UnloadSceneAsync(read);
    SceneManager::ProcessSceneLoads();

    for (GameObject* g : made)
        DestroyImmediate(g);
    DestroyImmediate(cube);
    DestroyImmediate(m);
    Graphics::ReleaseResources();
}

HX_TEST(Culling, CullingGroupsReportStateChanges)
{
    constexpr u32 size = 16;
    ForEachDevice([&](rhi::Device& device) {
        TestShaderFolder shaders;
        rhi::TextureHandle color =
            device.CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
        rhi::TextureHandle depth = device.CreateTexture({.format = rhi::Format::D32Float, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(&device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
        // A camera at the origin looking along +z (orthographic, 5 units up and down); spheres in and out of view.
        Camera* camera = MakeCamera(Vector3(0, 0, 0), 5);
        BoundingSphere spheres[4] = {BoundingSphere(Vector3(0, 0, 3), 1), BoundingSphere(Vector3(0, 0, 30), 1), BoundingSphere(Vector3(50, 0, 3), 1),
                                     BoundingSphere(Vector3(0, 0, -20), 1)};
        struct Seen
        {
            Array<CullingGroupEvent> events;
        } seen;
        CullingGroup group;
        group.SetStateChanged([](void* user, const CullingGroupEvent& e) { static_cast<Seen*>(user)->events.Push(e); }, &seen);
        group.SetTargetCamera(camera);
        group.SetBoundingSpheres(spheres, 4);
        group.SetBoundingSphereCount(3);
        const f32 bands[] = {10, 50};
        group.SetBoundingDistances(Span<const f32>(bands, 2));
        group.SetDistanceReferencePoint(Vector3(0, 0, 0));
        UpdateWorldMatrices();
        Graphics::RenderFrame();
        // Visible: the near and the far one in front; bands 0, 1 and 2 (the side one, 49 away, in band 1).
        HX_CHECK(group.IsVisible(0) && group.IsVisible(1) && !group.IsVisible(2));
        HX_CHECK(group.GetDistance(0) == 0 && group.GetDistance(1) == 1 && group.GetDistance(2) == 1);
        // Changes only: the side one stayed invisible but moved to band 1, so three events.
        HX_CHECK(seen.events.Size() == 3 && seen.events[0].hasBecomeVisible() && seen.events[1].currentDistance() == 1 &&
                 !seen.events[2].isVisible() && seen.events[2].currentDistance() == 1);
        i32 indices[4];
        HX_CHECK(group.QueryIndices(true, Span<i32>(indices, 4), 0) == 2 && indices[0] == 0 && indices[1] == 1);
        HX_CHECK(group.QueryIndices(1, Span<i32>(indices, 4), 0) == 2 && indices[0] == 1 && indices[1] == 2);
        // Nothing changed: no events; a sphere moved out of view: one.
        seen.events.Clear();
        Graphics::RenderFrame();
        HX_CHECK(seen.events.Empty());
        spheres[0].position = Vector3(0, 0, -3);
        Graphics::RenderFrame();
        HX_CHECK(seen.events.Size() == 1 && seen.events[0].index == 0 && seen.events[0].hasBecomeInvisible());
        // Erasing swaps the last state in; a disabled group hears nothing.
        group.EraseSwapBack(0);
        HX_CHECK(!group.IsVisible(0) && group.GetDistance(0) == 1);
        group.SetEnabled(false);
        seen.events.Clear();
        spheres[1].position = Vector3(0, 0, 3);
        Graphics::RenderFrame();
        HX_CHECK(seen.events.Empty());
        DestroyImmediate(camera->gameObject());
        Graphics::ReleaseResources();
        device.Destroy(color);
        device.Destroy(depth);
    });
}

HX_TEST(Culling, DrawListsSortStablyByTheirKeys)
{
    // Small lists on this thread, large ones on the jobs: the same order as a stable comparison sort.
    if (!jobs::IsInitialized())
        jobs::Initialize();
    for (u32 n : {1000u, 100000u})
    {
        Array<render::SortEntry> entries, scratch;
        u64 seed = 12345;
        auto next = [&] {
            seed = seed * 6364136223846793005ull + 1442695040888963407ull;
            return seed >> 11;
        };
        for (u32 i = 0; i < n; i++)
            entries.Push({next() % 7 << 48 | next() % 3, (next() % 50 << 32 | (next() % 2) << 24) | i});
        Array<render::SortEntry> expected = entries;
        std::stable_sort(expected.begin(), expected.end(),
                         [](const render::SortEntry& a, const render::SortEntry& b) { return a.hi != b.hi ? a.hi < b.hi : (a.lo >> 24) < (b.lo >> 24); });
        render::RadixSort(entries, scratch);
        bool same = true;
        for (u32 i = 0; i < n && same; i++)
            same = entries[i].item() == expected[i].item();
        HX_CHECK(same);
    }
}

HX_TEST(Rendering, PersistentBlocksDrawWhatTransientOnesDraw)
{
    constexpr u32 size = 32;
    ForEachDevice([&](rhi::Device& device) {
        TestShaderFolder shaders;
        rhi::TextureHandle color =
            device.CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
        rhi::TextureHandle depth = device.CreateTexture({.format = rhi::Format::D32Float, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(&device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
        Mesh* quad = Quad();
        Material* red = Material::Create(Shader::Find("Tests/Unlit"));
        red->SetColor("color", Color(1, 0, 0, 1));
        Material* blue = Material::Create(Shader::Find("Tests/Unlit"));
        blue->SetColor("color", Color(0, 0, 1, 1));
        GameObject* a = Thing("A", quad, red, Vector3(-1, 0, 0), 1.5f);
        GameObject* b = Thing("B", quad, blue, Vector3(1, 0, 0), 1.5f);
        Camera* camera = MakeCamera(Vector3(0, 0, -10), 2);
        auto frame = [&](bool batching) {
            GraphicsSettings::SetUseScriptableRenderPipelineBatching(batching);
            UpdateWorldMatrices();
            Graphics::RenderFrame();
            Array<u32> pixels;
            pixels.Resize(size * size);
            HX_CHECK(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
            return pixels;
        };
        auto same = [](const Array<u32>& x, const Array<u32>& y) {
            for (usize i = 0; i < x.Size(); i++)
                if (x[i] != y[i])
                    return false;
            return true;
        };
        bool can = device.Caps().uniformBufferOffsets && device.Caps().uniformBufferPartialUpdates;
        HX_CHECK(SystemInfo::renderingThreadingMode() == (device.Threaded() ? RenderingThreadingMode::MultiThreaded : RenderingThreadingMode::Direct));
        Array<u32> transient = frame(false);
        Array<u32> persistent = frame(true);
        HX_CHECK(same(transient, persistent));
        HX_CHECK(Graphics::LastStats().persistentDraws == (can ? 2u : 0u));
        // A material's value and an object's place change: written into fresh parts, the same image as without.
        red->SetColor("color", Color(0, 1, 0, 1));
        b->transform()->SetPosition(Vector3(1, 1, 0));
        Array<u32> on = frame(true);
        Array<u32> again = frame(true);
        Array<u32> off = frame(false);
        HX_CHECK(same(on, off) && same(again, off));
        HX_CHECK(NearColor(Pixel(off, size, 8, 16), Rgba(0, 255, 0)));
        GraphicsSettings::SetUseScriptableRenderPipelineBatching(false);
        DestroyImmediate(a);
        DestroyImmediate(b);
        DestroyImmediate(camera->gameObject());
        Graphics::ReleaseResources();
        device.Destroy(color);
        device.Destroy(depth);
    });
}

HX_TEST(Rendering, InstancedDrawsDrawWhatSeparateOnesDraw)
{
    constexpr u32 size = 32;
    ForEachDevice([&](rhi::Device& device) {
        TestShaderFolder shaders;
        rhi::TextureHandle color =
            device.CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
        rhi::TextureHandle depth = device.CreateTexture({.format = rhi::Format::D32Float, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(&device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
        Mesh* quad = Quad();
        Material* m = Material::Create(Shader::Find("Tests/InstancedUnlit"));
        m->SetColor("color", Color(1, 0.5f, 0, 1));
        Material* plain = Material::Create(Shader::Find("Tests/Unlit"));
        plain->SetColor("color", Color(0, 0, 1, 1));
        Array<GameObject*> made;
        // Eight small quads in two rows with the instancing material, one with a material that does not take part.
        for (u32 i = 0; i < 8; i++)
            made.Push(Thing("Q", quad, m, Vector3(-3.5f + f32(i % 4) * 2, i < 4 ? 1.5f : -1.5f, 0), 1));
        made.Push(Thing("Plain", quad, plain, Vector3(0, 0, 0), 0.5f));
        Camera* camera = MakeCamera(Vector3(0, 0, -10), 4);
        made.Push(camera->gameObject());
        auto frame = [&](bool instancing) {
            m->SetEnableInstancing(instancing);
            UpdateWorldMatrices();
            Graphics::RenderFrame();
            Array<u32> pixels;
            pixels.Resize(size * size);
            HX_CHECK(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
            return pixels;
        };
        Array<u32> separate = frame(false);
        HX_CHECK(Graphics::LastStats().drawCalls == 9 && Graphics::LastStats().instancedBatches == 0);
        Array<u32> instanced = frame(true);
        const RenderStats& stats = Graphics::LastStats();
        std::printf("    draws %u, instanced %u of %u instances\n", stats.drawCalls, stats.instancedBatches, stats.instances);
        HX_CHECK(stats.drawCalls == 2 && stats.instancedBatches == 1 && stats.instances == 8);
        bool same = true;
        for (u32 i = 0; i < size * size; i++)
            same = same && separate[i] == instanced[i];
        HX_CHECK(same);
        HX_CHECK(NearColor(Pixel(instanced, size, 2, 10), Rgba(255, 128, 0)) && NearColor(Pixel(instanced, size, 16, 16), Rgba(0, 0, 255)));
        // The same eight from Graphics.DrawMeshInstanced (renderers of the material hidden), and from a command buffer.
        Array<Matrix4x4> matrices;
        for (u32 i = 0; i < 8; i++)
        {
            matrices.Push(made[i]->transform()->localToWorldMatrix());
            made[i]->SetActive(false);
        }
        Graphics::DrawMeshInstanced(quad, 0, m, matrices, nullptr, 0, nullptr);
        Array<u32> queued = frame(true);
        HX_CHECK(Graphics::LastStats().instancedBatches == 1 && Graphics::LastStats().instances == 8);
        same = true;
        for (u32 i = 0; i < size * size; i++)
            same = same && separate[i] == queued[i];
        HX_CHECK(same);
        UpdateWorldMatrices();
        Graphics::RenderFrame();
        CommandBuffer buffer;
        buffer.ClearRenderTarget(true, true, Color(0, 0, 0, 1));
        buffer.SetViewProjectionMatrices(camera->worldToCameraMatrix(), camera->projectionMatrix());
        buffer.DrawMeshInstanced(quad, 0, m, 0, matrices);
        Graphics::ExecuteCommandBuffer(buffer);
        Array<u32> commanded;
        commanded.Resize(size * size);
        HX_REQUIRE(device.ReadTexture(color, 0, commanded.Data(), commanded.Size() * 4));
        // The eight quads where the renderers drew them; nothing where the plain one was.
        same = true;
        for (u32 i = 0; i < size * size; i++)
            if (!NearColor(separate[i], Rgba(0, 0, 255)))
                same = same && NearColor(separate[i], commanded[i]);
        HX_CHECK(same && NearColor(Pixel(commanded, size, 16, 16), Rgba(0, 0, 0)));
        for (GameObject* g : made)
            DestroyImmediate(g);
        Graphics::ReleaseResources();
        device.Destroy(color);
        device.Destroy(depth);
    });
}

HX_TEST(Rendering, StateBlocksSetStencilOnTheDevices)
{
    constexpr u32 size = 32;
    ForEachDevice([&](rhi::Device& device) {
        TestShaderFolder shaders;
        rhi::TextureHandle color =
            device.CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
        rhi::TextureHandle depth = device.CreateTexture({.format = rhi::Format::D24UnormS8Uint, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(&device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D24UnormS8Uint});
        Mesh* quad = Quad();
        Material* red = Material::Create(Shader::Find("Tests/Unlit"));
        red->SetColor("color", Color(1, 0, 0, 1));
        Material* green = Material::Create(Shader::Find("Tests/Unlit"));
        green->SetColor("color", Color(0, 1, 0, 1));
        // A mask quad on the left half (layer 8), a full screen quad (layer 9) in front of it.
        GameObject* mask = Thing("Mask", quad, red, Vector3(-2, 0, 1), 4);
        mask->SetLayer(8);
        GameObject* full = Thing("Full", quad, green, Vector3(0, 0, 0), 8);
        full->SetLayer(9);
        Camera* camera = MakeCamera(Vector3(0, 0, -10), 4);
        UpdateWorldMatrices();
        Graphics::RenderFrame();
        {
            ScriptableRenderContext context;
            context.SetupCameraProperties(camera);
            CommandBuffer clear;
            clear.ClearRenderTarget(true, true, Color(0, 0, 0, 1));
            context.ExecuteCommandBuffer(clear);
            ScriptableCullingParameters p;
            HX_REQUIRE(camera->TryGetCullingParameters(p));
            CullingResults culling = context.Cull(p);
            SortingSettings sorting(camera);
            DrawingSettings drawing(ShaderTagId("SRPDefaultUnlit"), sorting);
            // The mask writes 1 into the stencil and no color; the full quad draws where the stencil is 1.
            RenderStateBlock write(RenderStateMask(i32(RenderStateMask::Stencil) | i32(RenderStateMask::Blend)));
            write.blendState.targets[0].writeMask = ColorWriteMask(0);
            write.stencilState.enabled = true;
            write.stencilState.passOperationFront = write.stencilState.passOperationBack = StencilOp::Replace;
            write.stencilReference = 1;
            FilteringSettings maskOnly(RenderQueueRange::all(), 1 << 8);
            context.DrawRenderers(culling, drawing, maskOnly, write);
            RenderStateBlock test(RenderStateMask::Stencil);
            test.stencilState.enabled = true;
            test.stencilState.compareFunctionFront = test.stencilState.compareFunctionBack = CompareFunction::Equal;
            test.stencilState.writeMask = 0;
            test.stencilReference = 1;
            FilteringSettings fullOnly(RenderQueueRange::all(), 1 << 9);
            context.DrawRenderers(culling, drawing, fullOnly, test);
            context.Submit();
        }
        Array<u32> pixels;
        pixels.Resize(size * size);
        HX_REQUIRE(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
        std::printf("    left %08x right %08x\n", Pixel(pixels, size, 8, 16), Pixel(pixels, size, 24, 16));
        HX_CHECK(NearColor(Pixel(pixels, size, 8, 16), Rgba(0, 255, 0)) && NearColor(Pixel(pixels, size, 24, 16), Rgba(0, 0, 0)));
        DestroyImmediate(mask);
        DestroyImmediate(full);
        DestroyImmediate(camera->gameObject());
        Graphics::ReleaseResources();
        device.Destroy(color);
        device.Destroy(depth);
    });
}

HX_TEST(Rendering, InvertedCullingAndGlobalArraysFromCommandBuffers)
{
    constexpr u32 size = 16;
    ForEachDevice([&](rhi::Device& device) {
        TestShaderFolder shaders;
        rhi::TextureHandle color =
            device.CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
        rhi::TextureHandle depth = device.CreateTexture({.format = rhi::Format::D32Float, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(&device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
        Mesh* quad = Quad();
        Material* red = Material::Create(Shader::Find("Tests/Unlit"));
        red->SetColor("color", Color(1, 0, 0, 1));
        Camera* camera = MakeCamera(Vector3(0, 0, -10), 2);
        auto center = [&](bool invert) {
            CommandBuffer b;
            b.ClearRenderTarget(true, true, Color(0, 0, 0, 1));
            b.SetViewProjectionMatrices(camera->worldToCameraMatrix(), camera->projectionMatrix());
            b.SetInvertCulling(invert);
            b.DrawMesh(quad, Matrix4x4::TRS(Vector3(), Quaternion::identity, Vector3(2, 2, 2)), red);
            Graphics::ExecuteCommandBuffer(b);
            Array<u32> pixels;
            pixels.Resize(size * size);
            HX_CHECK(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
            return Pixel(pixels, size, 8, 8);
        };
        // The quad faces the camera: drawn; with culling inverted its front faces count as back faces.
        HX_CHECK(NearColor(center(false), Rgba(255, 0, 0)) && NearColor(center(true), Rgba(0, 0, 0)));
        // Global arrays set by a command buffer.
        const f32 floats[] = {1, 2, 3};
        const Vector4 vectors[] = {Vector4(1, 2, 3, 4), Vector4(5, 6, 7, 8)};
        CommandBuffer b;
        b.SetGlobalFloatArray(PropertyToID("_TestFloats"), Span<const f32>(floats, 3));
        b.SetGlobalVectorArray(PropertyToID("_TestVectors"), Span<const Vector4>(vectors, 2));
        Graphics::ExecuteCommandBuffer(b);
        Array<f32> f;
        Array<Vector4> v;
        Shader::Globals().GetFloatArray(PropertyToID("_TestFloats"), f);
        Shader::Globals().GetVectorArray(PropertyToID("_TestVectors"), v);
        HX_CHECK(f.Size() == 3 && f[2] == 3 && v.Size() == 2 && v[1].w == 8);
        DestroyImmediate(camera->gameObject());
        Graphics::ReleaseResources();
        device.Destroy(color);
        device.Destroy(depth);
    });
}

HX_TEST(Rendering, RendererListsRenderPassesProceduralDrawsAndReadbacks)
{
    constexpr u32 size = 32;
    ForEachDevice([&](rhi::Device& device) {
        TestShaderFolder shaders;
        rhi::TextureHandle color =
            device.CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
        rhi::TextureHandle depth = device.CreateTexture({.format = rhi::Format::D32Float, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(&device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
        Mesh* quad = Quad();
        Material* red = Material::Create(Shader::Find("Tests/Unlit"));
        red->SetColor("color", Color(1, 0, 0, 1));
        GameObject* left = Thing("Left", quad, red, Vector3(-1, 0, 0), 1.5f);
        Camera* camera = MakeCamera(Vector3(0, 0, -10), 2);
        UpdateWorldMatrices();
        Graphics::RenderFrame();
        auto read = [&] {
            Array<u32> pixels;
            pixels.Resize(size * size);
            HX_CHECK(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
            return pixels;
        };
        // A renderer list draws what DrawRenderers draws; a state for the pass tag's value turns its color writes off.
        for (u32 round = 0; round < 2; round++)
        {
            ScriptableRenderContext context;
            context.SetupCameraProperties(camera);
            CommandBuffer clear;
            clear.ClearRenderTarget(true, true, Color(0, 0, 0, 1));
            context.ExecuteCommandBuffer(clear);
            ScriptableCullingParameters p;
            HX_REQUIRE(camera->TryGetCullingParameters(p));
            CullingResults culling = context.Cull(p);
            RendererUtils::RendererListDesc desc(ShaderTagId("SRPDefaultUnlit"), culling, camera);
            desc.sortingCriteria = SortingCriteria::CommonOpaque;
            RendererListParams params = desc.ConvertToParameters();
            if (round == 1)
            {
                RenderStateBlock noColor(RenderStateMask::Blend);
                noColor.blendState.targets[0].writeMask = ColorWriteMask(0);
                params.isPassTagName = true;
                params.taggedStates.Push({ShaderTagId("SRPDefaultUnlit"), noColor});
            }
            RendererUtils::RendererList list = context.CreateRendererList(params);
            HX_CHECK(list.isValid());
            CommandBuffer draw;
            draw.DrawRendererList(list);
            context.ExecuteCommandBuffer(draw);
            context.Submit();
            Array<u32> pixels = read();
            HX_CHECK(NearColor(Pixel(pixels, size, 8, 16), round == 0 ? Rgba(255, 0, 0) : Rgba(0, 0, 0)) && NearColor(Pixel(pixels, size, 24, 16), Rgba(0, 0, 0)));
        }
        // A native render pass: one subpass into the camera target, cleared green by its load action.
        {
            ScriptableRenderContext context;
            context.SetupCameraProperties(camera);
            AttachmentDescriptor target(GraphicsFormat::R8G8B8A8_UNorm);
            target.ConfigureTarget(RenderTargetIdentifier(BuiltinRenderTextureType::CameraTarget), false, true);
            target.ConfigureClear(Color(0, 1, 0, 1));
            AttachmentDescriptor scratch(GraphicsFormat::R8G8B8A8_UNorm);
            AttachmentDescriptor attachments[2] = {target, scratch};
            context.BeginRenderPass(i32(size), i32(size), 1, Span<const AttachmentDescriptor>(attachments, 2));
            const i32 first[] = {1};
            context.BeginSubPass(Span<const i32>(first, 1));
            context.EndSubPass();
            const i32 second[] = {0};
            context.BeginSubPass(Span<const i32>(second, 1));
            context.EndSubPass();
            context.EndRenderPass();
            context.Submit();
            HX_CHECK(NearColor(Pixel(read(), size, 16, 16), Rgba(0, 255, 0)));
        }
        // DrawProcedural: three vertices of Hidden/BlitCopy (a full screen triangle from the vertex id) with white.
        {
            Material* blit = Material::Create(Shader::Find("Hidden/BlitCopy"));
            CommandBuffer b;
            b.ClearRenderTarget(true, true, Color(0, 0, 0, 1));
            b.DrawProcedural(Matrix4x4::identity, blit, 0, MeshTopology::Triangles, 3);
            Graphics::ExecuteCommandBuffer(b);
            HX_CHECK(NearColor(Pixel(read(), size, 16, 16), Rgba(255, 255, 255)));
            DestroyImmediate(blit);
        }
        // BlitMultiTap's offsets as globals.
        {
            RenderTexture* a = RenderTexture::New(8, 8, 0);
            RenderTexture* b = RenderTexture::New(8, 8, 0);
            const Vector2 taps[] = {Vector2(1, 0), Vector2(0, 1), Vector2(-1, 0)};
            Graphics::BlitMultiTap(a, b, nullptr, Span<const Vector2>(taps, 3));
            Array<Vector4> offsets;
            Shader::Globals().GetVectorArray(PropertyToID("_BlitMultiTapOffsets"), offsets);
            HX_CHECK(offsets.Size() == 3 && offsets[1].y == 1);
            DestroyImmediate(a);
            DestroyImmediate(b);
        }
        // Readbacks: read when the frame's commands are submitted, delivered at the next frame's start.
        {
            RenderTexture* rt = RenderTexture::New(16, 16, 24);
            camera->SetTargetTexture(rt);
            struct Got
            {
                u32 calls = 0;
                u32 texel = 0;
            } got;
            AsyncGPUReadbackRequest r = AsyncGPUReadback::Request(rt, 0, [](void* user, const AsyncGPUReadbackRequest& q) {
                auto* g = static_cast<Got*>(user);
                g->calls++;
                Span<const u8> data = q.GetData();
                if (data.Size() >= 4)
                    std::memcpy(&g->texel, data.Data() + (8 * 16 + 4) * 4, 4);
            }, &got);
            HX_CHECK(!r.done());
            Graphics::RenderFrame();
            HX_CHECK(r.done() && !r.hasError() && r.width() == 16 && got.calls == 0);
            Graphics::RenderFrame();
            HX_CHECK(got.calls == 1 && NearColor(got.texel, Rgba(255, 0, 0)));
            // A command buffer's request waits for its command.
            CommandBuffer b;
            AsyncGPUReadbackRequest r2 = b.RequestAsyncReadback(rt);
            HX_CHECK(!r2.done());
            Graphics::ExecuteCommandBuffer(b);
            HX_CHECK(r2.done() && r2.GetData().Size() == 16 * 16 * 4);
            HX_CHECK(AsyncGPUReadback::Request(Texture2D::whiteTexture()).hasError());
            camera->SetTargetTexture(nullptr);
            DestroyImmediate(rt);
        }
        DestroyImmediate(left);
        DestroyImmediate(camera->gameObject());
        Graphics::ReleaseResources();
        device.Destroy(color);
        device.Destroy(depth);
    });
}

HX_TEST(Rendering, TheCameraBlockSaysWhenTheTargetIsDrawnFlipped)
{
    constexpr u32 size = 16;
    ForEachDevice([&](rhi::Device& device) {
        TestShaderFolder shaders;
        rhi::TextureHandle color =
            device.CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
        rhi::TextureHandle depth = device.CreateTexture({.format = rhi::Format::D32Float, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(&device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
        Material* params = Material::Create(Shader::Find("Tests/ProjectionParams"));
        GameObject* quad = Thing("Params", Quad(), params, Vector3(0, 0, 0), 3);
        Camera* camera = MakeCamera(Vector3(0, 0, -10), 2);
        camera->SetNearClipPlane(0.5f);
        UpdateWorldMatrices();
        Array<u32> pixels;
        pixels.Resize(size * size);
        // The screen: not flipped.
        Graphics::RenderFrame();
        HX_REQUIRE(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
        HX_CHECK(NearColor(Pixel(pixels, size, 8, 8), Rgba(0, 255, 128)));
        // A render texture: flipped.
        RenderTexture* rt = RenderTexture::New(i32(size), i32(size), 24);
        camera->SetTargetTexture(rt);
        Graphics::RenderFrame();
        const render::GpuRenderTarget* g = render::RenderTargetOf(*rt);
        HX_REQUIRE(g != nullptr);
        HX_REQUIRE(device.ReadTexture(g->color, 0, pixels.Data(), pixels.Size() * 4));
        HX_CHECK(NearColor(Pixel(pixels, size, 8, 8), Rgba(255, 0, 128)));
        camera->SetTargetTexture(nullptr);
        DestroyImmediate(rt);
        DestroyImmediate(quad);
        DestroyImmediate(camera->gameObject());
        DestroyImmediate(params);
        Graphics::ReleaseResources();
        device.Destroy(color);
        device.Destroy(depth);
    });
}

HX_TEST(Rendering, ValuesWithoutPropertiesComeByName)
{
    constexpr u32 size = 16;
    ForEachDevice([&](rhi::Device& device) {
        TestShaderFolder shaders;
        rhi::TextureHandle color =
            device.CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
        rhi::TextureHandle depth = device.CreateTexture({.format = rhi::Format::D32Float, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(&device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
        Material* named = Material::Create(Shader::Find("Tests/NamedValues"));
        GameObject* quad = Thing("Named", Quad(), named, Vector3(0, 0, 0), 3);
        Camera* camera = MakeCamera(Vector3(0, 0, -10), 2);
        UpdateWorldMatrices();
        Array<u32> pixels;
        pixels.Resize(size * size);
        auto center = [&]() -> u32 {
            Graphics::RenderFrame();
            HX_CHECK(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
            return Pixel(pixels, size, 8, 8);
        };
        // The global color; the texture is white while nothing sets it.
        i32 namedColor = PropertyToID("namedColor"), namedTexture = PropertyToID("namedTexture");
        Shader::SetGlobalVector(namedColor, Vector4(0, 1, 0, 1));
        HX_CHECK(NearColor(center(), Rgba(0, 255, 0)));
        // A renderer's property block wins over the globals.
        MaterialPropertyBlock block;
        block.SetVector(namedColor, Vector4(1, 0, 0, 1));
        quad->GetComponent<MeshRenderer>()->SetPropertyBlock(&block);
        HX_CHECK(NearColor(center(), Rgba(255, 0, 0)));
        // A global texture (black is transparent).
        Shader::SetGlobalTexture(namedTexture, Texture2D::blackTexture());
        HX_CHECK(NearColor(center(), Rgba(0, 0, 0, 0)));
        quad->GetComponent<MeshRenderer>()->SetPropertyBlock(nullptr);
        Shader::Globals().Clear();
        DestroyImmediate(quad);
        DestroyImmediate(camera->gameObject());
        DestroyImmediate(named);
        Graphics::ReleaseResources();
        device.Destroy(color);
        device.Destroy(depth);
    });
}

HX_TEST(Rendering, CubemapsHoldSixFacesAndSampleAsCubes)
{
    // The pixel API per face and mip; on every device the six directions read the six faces.
    TestShaderFolder shaders;
    const Color colors[6] = {Color(1, 0, 0, 1), Color(0, 1, 0, 1), Color(0, 0, 1, 1), Color(1, 1, 0, 1), Color(0, 1, 1, 1), Color(1, 0, 1, 1)};
    Cubemap* cube = Cubemap::Create(4, TextureFormat::RGBA32, true, true);
    HX_REQUIRE(cube != nullptr);
    HX_CHECK(cube->width() == 4 && cube->height() == 4 && cube->mipmapCount() == 3 && cube->dimension() == TextureDimension::Cube);
    for (u32 f = 0; f < 6; f++)
    {
        Array<Color> face;
        face.Resize(16);
        for (Color& c : face)
            c = colors[f];
        HX_CHECK(cube->SetPixels(Span<const Color>(face.Data(), face.Size()), CubemapFace(f)));
    }
    cube->Apply(true);
    HX_CHECK(cube->GetPixel(CubemapFace::NegativeY, 1, 2) == colors[3] && cube->GetPixel(CubemapFace::PositiveZ, 0, 0, 2) == colors[4]);
    HX_CHECK(cube->GetPixels(CubemapFace::NegativeZ, 1).Size() == 4 && cube->GetPixelData<u32>(0, CubemapFace::PositiveX).Size() == 16);
    Cubemap* copy = Cubemap::Create(4, TextureFormat::RGBA32, true, true);
    HX_CHECK(copy->CopyPixels(cube, 2, 0, 5, 0) && copy->GetPixel(CubemapFace::NegativeZ, 3, 3) == colors[2]);
    HX_CHECK(!copy->SetPixel(CubemapFace::Unknown, 0, 0, Color(1, 1, 1, 1)));
    copy->SmoothEdges(1);
    // Only -Z has pixels in the copy: its edge texels blend toward the black faces next to it, its inner ones stay blue.
    Color edge = copy->GetPixel(CubemapFace::NegativeZ, 0, 0);
    HX_CHECK(!(edge == colors[2]) && copy->GetPixel(CubemapFace::NegativeZ, 1, 1) == colors[2]);
    DestroyImmediate(copy);

    constexpr u32 size = 36;
    ForEachDevice([&](rhi::Device& device) {
        rhi::TextureHandle color =
            device.CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
        rhi::TextureHandle depth = device.CreateTexture({.format = rhi::Format::D32Float, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(&device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
        Camera* main = MakeCamera(Vector3(0, 0, -10), 1);
        Material* faces = Material::Create(Shader::Find("Tests/CubeFaces"));
        faces->SetTexture("cube", cube);
        Thing("Faces", Quad(), faces, Vector3(0, 0, 0), 2);
        Graphics::RenderFrame();
        Array<u32> pixels;
        pixels.Resize(size * size);
        HX_REQUIRE(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
        // Rows top first: the top row of the screen is the quad's top row (-Y, +Z, -Z).
        const u32 want[6] = {Rgba(255, 0, 0), Rgba(0, 255, 0), Rgba(0, 0, 255), Rgba(255, 255, 0), Rgba(0, 255, 255), Rgba(255, 0, 255)};
        bool ok = true;
        for (u32 f = 0; f < 6; f++)
        {
            u32 x = 6 + (f % 3) * 12, y = f < 3 ? 27 : 9;
            if (!NearColor(Pixel(pixels, size, x, y), want[f]))
            {
                std::printf("    face %u at %u,%u: %08x\n", f, x, y, Pixel(pixels, size, x, y));
                ok = false;
            }
        }
        HX_CHECK(ok);
        DestroyImmediate(faces);
        DestroyImmediate(main->gameObject());
        if (GameObject* g = GameObject::Find("Faces"))
            DestroyImmediate(g);
        Graphics::ReleaseResources();
        device.Destroy(color);
        device.Destroy(depth);
    });
    DestroyImmediate(cube);
}

HX_TEST(Rendering, ShadowMapsSampleTheirDepthWithAComparison)
{
    // A depth only render texture in the Shadowmap format, its left half covered by a quad at the middle depth, read
    // with a comparison: a reference in front of the quad is lit everywhere, one behind it only on the right.
    TestShaderFolder shaders;
    constexpr u32 size = 32;
    ForEachDevice([&](rhi::Device& device) {
        rhi::TextureHandle color =
            device.CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
        rhi::TextureHandle depth = device.CreateTexture({.format = rhi::Format::D32Float, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(&device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
        Camera* main = MakeCamera(Vector3(0, 0, -10), 1);
        main->SetNearClipPlane(9);
        main->SetFarClipPlane(11);
        RenderTexture* shadow = RenderTexture::New(16, 16, 24, RenderTextureFormat::Shadowmap);
        HX_CHECK(shadow->descriptor().shadowSamplingMode == ShadowSamplingMode::CompareDepths && shadow->descriptor().graphicsFormat == GraphicsFormat::None);
        Mesh* quad = Quad();
        Material* caster = Material::Create(Shader::Find("Tests/Unlit"));
        CommandBuffer b;
        b.SetRenderTarget(RenderTargetIdentifier(shadow));
        b.ClearRenderTarget(true, false, Color(0, 0, 0, 0));
        b.SetViewProjectionMatrices(main->worldToCameraMatrix(), main->projectionMatrix());
        b.DrawMesh(quad, Matrix4x4::TRS(Vector3(-0.5f, 0, 0), Quaternion::identity, Vector3(1, 2, 1)), caster);
        Graphics::ExecuteCommandBuffer(b);
        Material* compare = Material::Create(Shader::Find("Tests/ShadowCompare"));
        compare->SetTexture("shadowMap", shadow);
        Thing("Receiver", quad, compare, Vector3(0, 0, 0), 2);
        Array<u32> pixels;
        pixels.Resize(size * size);
        for (f32 reference : {0.25f, 0.75f})
        {
            compare->SetFloat("reference", reference);
            Graphics::RenderFrame();
            HX_REQUIRE(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
            u32 left = Pixel(pixels, size, 6, 16), right = Pixel(pixels, size, 26, 16);
            u32 wantLeft = reference < 0.5f ? Rgba(255, 255, 255) : Rgba(0, 0, 0);
            if (!NearColor(left, wantLeft) || !NearColor(right, Rgba(255, 255, 255)))
                std::printf("    reference %.2f: left %08x right %08x\n", reference, left, right);
            HX_CHECK(NearColor(left, wantLeft) && NearColor(right, Rgba(255, 255, 255)));
        }
        DestroyImmediate(compare);
        DestroyImmediate(caster);
        DestroyImmediate(shadow);
        DestroyImmediate(main->gameObject());
        if (GameObject* g = GameObject::Find("Receiver"))
            DestroyImmediate(g);
        Graphics::ReleaseResources();
        device.Destroy(color);
        device.Destroy(depth);
    });
}

HX_TEST(Rendering, TheSkyboxListDrawsRenderSettingsSkybox)
{
    // A skybox of six colored faces around a perspective camera: ahead +Z, up +Y; nothing without a skybox material.
    constexpr u32 size = 16;
    bool registered = false;
    const Color colors[6] = {Color(1, 0, 0, 1), Color(0, 1, 0, 1), Color(0, 0, 1, 1), Color(1, 1, 0, 1), Color(0, 1, 1, 1), Color(1, 0, 1, 1)};
    ForEachDevice([&](rhi::Device& device) {
        TestShaderFolder shaders;
        rhi::TextureHandle color =
            device.CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
        rhi::TextureHandle depth = device.CreateTexture({.format = rhi::Format::D32Float, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(&device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
        Cubemap* cube = Cubemap::Create(1, TextureFormat::RGBA32, false, true);
        cube->SetFilterMode(FilterMode::Point);
        for (u32 f = 0; f < 6; f++)
            cube->SetPixel(CubemapFace(f), 0, 0, colors[f]);
        cube->Apply(false);
        Material* sky = Material::Create(Shader::Find("Tests/SkyboxCube"));
        sky->SetTexture("cube", cube);
        ScriptableObject* asset = MakePipelineAsset(registered);
        HX_REQUIRE(asset != nullptr);
        GraphicsSettings::SetDefaultRenderPipeline(asset);
        TestPipeline::skybox = true;
        GameObject* eye = GameObject::Create("SkyEye");
        Camera* camera = eye->AddComponent<Camera>();
        camera->SetFieldOfView(60);
        camera->SetClearFlags(CameraClearFlags::SolidColor);
        camera->SetBackgroundColor(Color(0, 0, 0, 1));
        Array<u32> pixels;
        pixels.Resize(size * size);
        auto centre = [&]() {
            Graphics::RenderFrame();
            if (!device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4))
                return 0xDEADBEEFu;
            return Pixel(pixels, size, size / 2, size / 2);
        };
        RenderSettings::SetSkybox(nullptr);
        u32 none = centre();
        RenderSettings::SetSkybox(sky);
        u32 ahead = centre();
        eye->transform()->SetRotation(Quaternion::Euler(-90, 0, 0));
        u32 up = centre();
        bool ok = NearColor(none, Rgba(0, 0, 0)) && NearColor(ahead, Rgba(0, 255, 255)) && NearColor(up, Rgba(0, 0, 255)) && RenderSettings::skybox() == sky;
        if (!ok)
            std::printf("    none %08x ahead %08x up %08x\n", none, ahead, up);
        HX_CHECK(ok);
        TestPipeline::skybox = false;
        RenderSettings::SetSkybox(nullptr);
        GraphicsSettings::SetDefaultRenderPipeline(nullptr);
        DestroyImmediate(asset);
        DestroyImmediate(eye);
        DestroyImmediate(sky);
        DestroyImmediate(cube);
        Graphics::ReleaseResources();
        device.Destroy(color);
        device.Destroy(depth);
    });
}

HX_TEST(Rendering, TheBuiltInSkyboxShadersOnEveryDevice)
{
    // Skybox/Cubemap with a cube of six colors: ahead +Z, right +X, up +Y; turned by 90 degrees (ahead shows +X), its
    // exposure and tint (doubled). Skybox/6 Sided with six textures whose right half is red and top half green, blue
    // telling the side: from inside each side looks upright, its right half to the right. Both drawn by
    // ScriptableRenderContext.DrawSkybox too; goldens of a view toward a corner where three sides meet.
    constexpr u32 size = 32;
    bool registered = false;
    const Color colors[6] = {Color(1, 0, 0, 1), Color(0, 1, 0, 1), Color(0, 0, 1, 1), Color(1, 1, 0, 1), Color(0, 1, 1, 1), Color(1, 0, 1, 1)};
    ForEachDevice([&](rhi::Device& device) {
        TestShaderFolder shaders;
        rhi::TextureHandle color =
            device.CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
        rhi::TextureHandle depth = device.CreateTexture({.format = rhi::Format::D32Float, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(&device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
        Cubemap* cube = Cubemap::Create(1, TextureFormat::RGBA32, false, true);
        cube->SetFilterMode(FilterMode::Point);
        for (u32 f = 0; f < 6; f++)
            cube->SetPixel(CubemapFace(f), 0, 0, colors[f]);
        cube->Apply(false);
        Material* cubemap = Material::Create(Shader::Find("Skybox/Cubemap"));
        HX_REQUIRE(cubemap != nullptr && cubemap->shader()->Name() == StringView("Skybox/Cubemap"));
        cubemap->SetTexture("_Tex", cube);
        // The sides: red on the right half, green on the top half, blue the side's number / 5 (64 texels a side: devices
        // round a coordinate to a texel differently only within 1/256 of a texel, far less than a pixel here).
        const char* sides[6] = {"_FrontTex", "_BackTex", "_LeftTex", "_RightTex", "_UpTex", "_DownTex"};
        Material* sixSided = Material::Create(Shader::Find("Skybox/6 Sided"));
        HX_REQUIRE(sixSided != nullptr && sixSided->shader()->Name() == StringView("Skybox/6 Sided"));
        Texture2D* faces[6] = {};
        for (u32 f = 0; f < 6; f++)
        {
            faces[f] = Texture2D::Create(64, 64, TextureFormat::RGBA32, false, true);
            faces[f]->SetFilterMode(FilterMode::Point);
            faces[f]->SetWrapMode(TextureWrapMode::Clamp);
            for (i32 y = 0; y < 64; y++)
                for (i32 x = 0; x < 64; x++)
                    faces[f]->SetPixel(x, y, Color(x >= 32 ? 1.0f : 0.0f, y >= 32 ? 1.0f : 0.0f, f32(f) / 5.0f, 1));
            faces[f]->Apply(false);
            sixSided->SetTexture(sides[f], faces[f]);
        }
        ScriptableObject* asset = MakePipelineAsset(registered);
        HX_REQUIRE(asset != nullptr);
        GraphicsSettings::SetDefaultRenderPipeline(asset);
        TestPipeline::skybox = true;
        GameObject* eye = GameObject::Create("SkyEye");
        Camera* camera = eye->AddComponent<Camera>();
        camera->SetFieldOfView(90);
        camera->SetClearFlags(CameraClearFlags::SolidColor);
        camera->SetBackgroundColor(Color(0, 0, 0, 1));
        Array<u32> pixels;
        pixels.Resize(size * size);
        auto render = [&](Vector3 angles) {
            eye->transform()->SetRotation(Quaternion::Euler(angles.x, angles.y, angles.z));
            UpdateWorldMatrices();
            Graphics::RenderFrame();
            return device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4);
        };
        auto centre = [&](Vector3 angles) { return render(angles) ? Pixel(pixels, size, size / 2, size / 2) : 0xDEADBEEFu; };

        // The cube.
        RenderSettings::SetSkybox(cubemap);
        u32 ahead = centre(Vector3(0, 0, 0)), right = centre(Vector3(0, 90, 0)), up = centre(Vector3(-90, 0, 0));
        cubemap->SetFloat("_Rotation", 90);
        u32 turned = centre(Vector3(0, 0, 0));
        cubemap->SetFloat("_Rotation", 0);
        cubemap->SetFloat("_Exposure", 0.5f);
        u32 exposed = centre(Vector3(0, 0, 0));
        cubemap->SetFloat("_Exposure", 1);
        cubemap->SetColor("_Tint", Color(0.5f, 0.5f, 0.25f, 0.5f));
        u32 tinted = centre(Vector3(0, 0, 0));
        cubemap->SetColor("_Tint", Color(0.5f, 0.5f, 0.5f, 0.5f));
        bool cubeOk = NearColor(ahead, Rgba(0, 255, 255)) && NearColor(right, Rgba(255, 0, 0)) && NearColor(up, Rgba(0, 0, 255)) &&
                      NearColor(turned, Rgba(255, 0, 0)) && NearColor(exposed, Rgba(0, 128, 128), 4) && NearColor(tinted, Rgba(0, 255, 128), 4);
        if (!cubeOk)
            std::printf("    cube: ahead %08x right %08x up %08x turned %08x exposed %08x tinted %08x\n", ahead, right, up, turned, exposed, tinted);
        HX_CHECK(cubeOk);
        // The goldens' view: no horizon or side edge through pixel centres (the horizon at 30 degrees up is between rows).
        HX_REQUIRE(render(Vector3(-30, 33, 0)));
        HX_CHECK(MatchGolden("SkyboxCubemap", pixels.Data(), size, size));

        // The six sides, each seen upright from inside: the quarters of the view.
        RenderSettings::SetSkybox(sixSided);
        const Vector3 views[6] = {Vector3(0, 0, 0), Vector3(0, 180, 0), Vector3(0, 90, 0), Vector3(0, -90, 0), Vector3(-90, 0, 0), Vector3(90, 0, 0)};
        bool sidesOk = true;
        for (u32 f = 0; f < 6; f++)
        {
            HX_REQUIRE(render(views[f]));
            u32 b = u32(std::lround(f32(f) / 5.0f * 255.0f));
            // Rows are read top first: y 24 is low on the screen, 8 high.
            u32 lowLeft = Pixel(pixels, size, 8, 24), lowRight = Pixel(pixels, size, 24, 24), highLeft = Pixel(pixels, size, 8, 8),
                highRight = Pixel(pixels, size, 24, 8);
            bool ok = NearColor(lowLeft, Rgba(0, 0, b)) && NearColor(lowRight, Rgba(255, 0, b)) && NearColor(highLeft, Rgba(0, 255, b)) &&
                      NearColor(highRight, Rgba(255, 255, b));
            if (!ok)
                std::printf("    side %s: %08x %08x %08x %08x\n", sides[f], lowLeft, lowRight, highLeft, highRight);
            sidesOk = sidesOk && ok;
        }
        HX_CHECK(sidesOk);
        HX_REQUIRE(render(Vector3(-30, 33, 0)));
        HX_CHECK(MatchGolden("SkyboxSixSided", pixels.Data(), size, size));
        // ScriptableRenderContext.DrawSkybox draws the same.
        Array<u32> listed = pixels;
        TestPipeline::skybox = false;
        TestPipeline::drawSkybox = true;
        HX_REQUIRE(render(Vector3(-30, 33, 0)));
        HX_CHECK(std::memcmp(listed.Data(), pixels.Data(), pixels.Size() * 4) == 0);

        TestPipeline::drawSkybox = false;
        RenderSettings::SetSkybox(nullptr);
        GraphicsSettings::SetDefaultRenderPipeline(nullptr);
        DestroyImmediate(asset);
        DestroyImmediate(eye);
        DestroyImmediate(cubemap);
        DestroyImmediate(sixSided);
        DestroyImmediate(cube);
        for (Texture2D* t : faces)
            DestroyImmediate(t);
        Graphics::ReleaseResources();
        device.Destroy(color);
        device.Destroy(depth);
    });
}

HX_TEST(Rendering, SphericalHarmonicsOfAmbientLight)
{
    // A flat color everywhere; a directional light about its color toward it and nothing away from it; the shaders'
    // constants evaluate as Evaluate does; the scene's Flat, Trilight and Custom modes.
    SphericalHarmonicsL2 flat;
    flat.AddAmbientLight(Color(0.2f, 0.4f, 0.6f, 1));
    Color any = flat.Evaluate(Vector3(0.3f, -0.8f, 0.52f).normalized());
    HX_CHECK(std::fabs(any.r - 0.2f) < 1e-4f && std::fabs(any.g - 0.4f) < 1e-4f && std::fabs(any.b - 0.6f) < 1e-4f);
    SphericalHarmonicsL2 sun;
    sun.AddDirectionalLight(Vector3(0, 1, 0), Color(1, 1, 1, 1), 2);
    Color toward = sun.Evaluate(Vector3(0, 1, 0)), away = sun.Evaluate(Vector3(0, -1, 0)), side = sun.Evaluate(Vector3(1, 0, 0));
    HX_CHECK(std::fabs(toward.r - 2.125f) < 1e-3f && std::fabs(away.r) < 0.15f && side.r > 0.1f && side.r < 0.3f);
    SphericalHarmonicsL2 both = flat + sun * 0.5f;
    Vector4 k[7];
    both.ShaderConstants(k);
    bool same = true;
    for (Vector3 n : {Vector3(1, 0, 0), Vector3(0, -1, 0), Vector3(0.6f, 0.48f, 0.64f), Vector3(-0.36f, 0.48f, -0.8f)})
    {
        Vector4 vB(n.x * n.y, n.y * n.z, n.z * n.z, n.z * n.x);
        f32 c2 = n.x * n.x - n.y * n.y;
        Color e = both.Evaluate(n);
        f32 rgb[3] = {e.r, e.g, e.b};
        f32 kc[3] = {k[6].x, k[6].y, k[6].z};
        for (i32 c = 0; c < 3; c++)
            same = same && std::fabs(Vector4::Dot(k[c], Vector4(n.x, n.y, n.z, 1)) + Vector4::Dot(k[3 + c], vB) + kc[c] * c2 - rgb[c]) < 1e-4f;
    }
    HX_CHECK(same);

    RenderSettings::SetAmbientMode(AmbientMode::Flat);
    RenderSettings::SetAmbientLight(Color(0.1f, 0.2f, 0.3f, 1));
    Color flatUp = RenderSettings::ambientProbe().Evaluate(Vector3(0, 1, 0));
    HX_CHECK(std::fabs(flatUp.g - 0.2f) < 1e-4f && RenderSettings::ambientSkyColor().b == 0.3f);
    RenderSettings::SetAmbientMode(AmbientMode::Trilight);
    RenderSettings::SetAmbientSkyColor(Color(0, 0, 1, 1));
    RenderSettings::SetAmbientEquatorColor(Color(0, 1, 0, 1));
    RenderSettings::SetAmbientGroundColor(Color(1, 0, 0, 1));
    SphericalHarmonicsL2 tri = RenderSettings::ambientProbe();
    Color up = tri.Evaluate(Vector3(0, 1, 0)), down = tri.Evaluate(Vector3(0, -1, 0)), side2 = tri.Evaluate(Vector3(1, 0, 0));
    HX_CHECK(up.b > up.r && up.b > 0.5f && down.r > down.b && down.r > 0.5f && side2.g > side2.r && side2.g > side2.b);
    RenderSettings::SetAmbientMode(AmbientMode::Custom);
    RenderSettings::SetAmbientProbe(flat);
    Array<f32> coefficients = RenderSettings::ambientProbeCoefficients();
    HX_CHECK(coefficients.Size() == 27 && RenderSettings::ambientProbe() == flat && coefficients[9] == flat(1, 0));
    RenderSettings::Apply(RenderSettingsData());
}

HX_TEST(Rendering, DrawsGetTheirReflectionProbeAndTheAmbientLight)
{
    // A red probe's box around the left quad, nothing around the right one: the right one reflects the scene's custom
    // reflection (green), at RenderSettings.reflectionIntensity; both get the flat ambient light (blue). Without
    // PerObjectData.ReflectionProbes both reflect the scene's.
    constexpr u32 size = 32;
    bool registered = false;
    ForEachDevice([&](rhi::Device& device) {
        TestShaderFolder shaders;
        rhi::TextureHandle color =
            device.CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
        rhi::TextureHandle depth = device.CreateTexture({.format = rhi::Format::D32Float, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(&device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
        ScriptableObject* asset = MakePipelineAsset(registered);
        HX_REQUIRE(asset != nullptr);
        GraphicsSettings::SetDefaultRenderPipeline(asset);
        MakeCamera(Vector3(0, 0, -10), 4);
        auto cubeOf = [](Color c) {
            Cubemap* cube = Cubemap::Create(1, TextureFormat::RGBA32, false, true);
            for (i32 f = 0; f < 6; f++)
                cube->SetPixel(CubemapFace(f), 0, 0, c);
            cube->Apply();
            return cube;
        };
        Cubemap* red = cubeOf(Color(1, 0, 0, 1));
        Cubemap* green = cubeOf(Color(0, 1, 0, 1));
        GameObject* holder = GameObject::Create("Probe");
        holder->transform()->SetPosition(Vector3(-2, 0, 0));
        ReflectionProbe* probe = holder->AddComponent<ReflectionProbe>();
        probe->SetSize(Vector3(3, 3, 3));
        probe->SetMode(ReflectionProbeMode::Custom);
        probe->SetCustomBakedTexture(red);
        probe->SetIntensity(0.5f);
        RenderSettings::SetDefaultReflectionMode(DefaultReflectionMode::Custom);
        RenderSettings::SetCustomReflectionTexture(green);
        RenderSettings::SetReflectionIntensity(1);
        RenderSettings::SetAmbientMode(AmbientMode::Flat);
        RenderSettings::SetAmbientLight(Color(0, 0, 0.5f, 1));
        Mesh* quad = Quad();
        Material* m = Material::Create(Shader::Find("Tests/ProbeAndAmbient"));
        GameObject* left = Thing("Left", quad, m, Vector3(-2, 0, 0));
        GameObject* right = Thing("Right", quad, m, Vector3(2, 0, 0));
        Array<u32> pixels;
        pixels.Resize(size * size);
        bool before = GraphicsSettings::useScriptableRenderPipelineBatching();
        for (bool persistent : {false, true})
        {
            GraphicsSettings::SetUseScriptableRenderPipelineBatching(persistent);
            TestPipeline::perObjectData = PerObjectData::ReflectionProbes;
            Graphics::RenderFrame();
            HX_REQUIRE(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
            u32 l = Pixel(pixels, size, 8, 16), r = Pixel(pixels, size, 24, 16);
            bool ok = NearColor(l, Rgba(128, 0, 128)) && NearColor(r, Rgba(0, 255, 128));
            TestPipeline::perObjectData = PerObjectData::None;
            Graphics::RenderFrame();
            HX_REQUIRE(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
            u32 l2 = Pixel(pixels, size, 8, 16);
            ok = ok && NearColor(l2, Rgba(0, 255, 128));
            if (!ok)
                std::printf("    persistent %d: left %08x right %08x, without probes %08x\n", int(persistent), l, r, l2);
            HX_CHECK(ok);
        }
        GraphicsSettings::SetUseScriptableRenderPipelineBatching(before);
        TestPipeline::perObjectData = PerObjectData::None;
        RenderSettings::Apply(RenderSettingsData());
        GraphicsSettings::SetDefaultRenderPipeline(nullptr);
        for (Object* o : {static_cast<Object*>(left), static_cast<Object*>(right), static_cast<Object*>(holder), static_cast<Object*>(m),
                          static_cast<Object*>(quad), static_cast<Object*>(red), static_cast<Object*>(green), static_cast<Object*>(asset)})
            DestroyImmediate(o);
        Graphics::ReleaseResources();
        device.Destroy(color);
        device.Destroy(depth);
    });
}

HX_TEST(Rendering, SceneFilesKeepTheirRenderSettings)
{
    // The active scene's settings written as its RenderSettings document and read back into another scene, which
    // applies them when it becomes the active one.
    Scene start = SceneManager::GetActiveScene();
    Scene written = SceneManager::CreateScene("SettingsWritten");
    SceneManager::SetActiveScene(written);
    RenderSettings::SetAmbientMode(AmbientMode::Trilight);
    RenderSettings::SetAmbientGroundColor(Color(0.5f, 0.25f, 0, 1));
    RenderSettings::SetReflectionIntensity(0.75f);
    RenderSettings::SetReflectionBounces(3);
    String yaml, error;
    HX_REQUIRE(WriteSceneYaml(written, yaml, error));
    bool named = yaml.View().Contains("--- !h!104 &2\nRenderSettings:") && yaml.View().Contains("m_AmbientMode: 1") &&
                 yaml.View().Contains("m_ReflectionIntensity: 0.75") && yaml.View().Contains("m_ReflectionBounces: 3");
    if (!named)
        std::printf("%s\n", yaml.CStr());
    HX_CHECK(named);
    SceneManager::SetActiveScene(start);
    RenderSettings::Apply(RenderSettingsData());
    Scene read = SceneManager::CreateScene("SettingsRead");
    Array<String> errors;
    HX_REQUIRE(ReadSceneFile(StringView(yaml), read, errors));
    HX_CHECK(errors.Empty() && RenderSettings::ambientMode() == AmbientMode::Skybox);
    SceneManager::SetActiveScene(read);
    HX_CHECK(RenderSettings::ambientMode() == AmbientMode::Trilight && RenderSettings::ambientGroundColor().r == 0.5f &&
             RenderSettings::reflectionIntensity() == 0.75f && RenderSettings::reflectionBounces() == 3);
    SceneManager::SetActiveScene(start);
    SceneManager::UnloadSceneAsync(written);
    SceneManager::UnloadSceneAsync(read);
    SceneManager::ProcessSceneLoads();
    RenderSettings::Apply(RenderSettingsData());
}

HX_TEST(Rendering, DrawsGetTheLightsOfTheirObjects)
{
    // Four quads, a directional light and three point lights: per object the directional light, then the point lights
    // reaching the quad by importance; the two quads on the right share theirs and draw instanced. Then without the
    // directional light (the light index map leaves it out). With the object blocks of each draw and the persistent ones.
    constexpr u32 size = 64;
    bool registered = false;
    ForEachDevice([&](rhi::Device& device) {
        TestShaderFolder shaders;
        rhi::TextureHandle color =
            device.CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
        rhi::TextureHandle depth = device.CreateTexture({.format = rhi::Format::D32Float, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(&device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
        ScriptableObject* asset = MakePipelineAsset(registered);
        HX_REQUIRE(asset != nullptr);
        GraphicsSettings::SetDefaultRenderPipeline(asset);
        Camera* main = MakeCamera(Vector3(0, 0, -10), 4);
        Array<GameObject*> made;
        auto light = [&](StringView name, LightType type, Vector3 position, f32 range, f32 intensity) {
            GameObject* g = GameObject::Create(name);
            Light* l = g->AddComponent<Light>();
            l->SetType(type);
            l->SetRange(range);
            l->SetIntensity(intensity);
            g->transform()->SetPosition(position);
            made.Push(g);
            return EntityId::ToULong(l->GetEntityId());
        };
        u64 sun = light("Sun", LightType::Directional, Vector3(0, 0, 0), 10, 1);
        u64 a = light("A", LightType::Point, Vector3(-3, 0, -1), 2, 1);
        u64 b = light("B", LightType::Point, Vector3(0, 0, -1), 1.4f, 2);
        u64 c = light("C", LightType::Point, Vector3(3, 0, -1), 3, 1);
        Mesh* quad = Quad();
        Material* lights = Material::Create(Shader::Find("Tests/ObjectLights"));
        lights->SetEnableInstancing(true);
        for (f32 x : {-3.0f, 0.0f, 1.5f, 2.5f})
            made.Push(Thing("Lit", quad, lights, Vector3(x, 0, 0)));
        TestPipeline::perObjectData = PerObjectData::LightData | PerObjectData::LightIndices;
        bool before = GraphicsSettings::useScriptableRenderPipelineBatching();
        Array<u32> pixels;
        pixels.Resize(size * size);
        for (bool persistent : {false, true})
            for (bool drop : {false, true})
            {
                GraphicsSettings::SetUseScriptableRenderPipelineBatching(persistent);
                TestPipeline::dropFirstLight = drop;
                Graphics::RenderFrame();
                HX_REQUIRE(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
                const Array<u64>& ids = TestPipeline::lightIds;
                auto index = [&](u64 id) {
                    for (u32 i = 0; i < ids.Size(); i++)
                        if (ids[i] == id)
                            return i32(i) - (drop ? 1 : 0);
                    return -100;
                };
                auto encode = [](i32 count, i32 first, i32 second) {
                    auto byte = [](i32 v) { return u32(std::lround(f32(v) / 8.0f * 255.0f)); };
                    return Rgba(byte(count), byte(first), byte(second));
                };
                // Left: the sun and A; middle: the sun, B, then C; right: the sun and C.
                u32 want[4];
                if (!drop)
                {
                    want[0] = encode(2, index(sun), index(a));
                    want[1] = encode(3, index(sun), index(b));
                    want[2] = want[3] = encode(2, index(sun), index(c));
                }
                else
                {
                    want[0] = encode(1, index(a), 0);
                    want[1] = encode(2, index(b), index(c));
                    want[2] = want[3] = encode(1, index(c), 0);
                }
                const u32 xs[4] = {8, 32, 44, 52};
                bool ok = ids.Size() == 4 && TestPipeline::lightIndexMap.Size() == 4 && TestPipeline::lightIndexMap[3] == 3 &&
                          TestPipeline::lightIndexCount == (drop ? 5 : 9);
                for (u32 k = 0; k < 4; k++)
                    if (!NearColor(Pixel(pixels, size, xs[k], 32), want[k]))
                    {
                        std::printf("    persistent %d drop %d quad %u: %08x, want %08x\n", int(persistent), int(drop), k, Pixel(pixels, size, xs[k], 32), want[k]);
                        ok = false;
                    }
                const RenderStats& stats = Graphics::LastStats();
                if (stats.instancedBatches != 1 || stats.instances != 2)
                {
                    std::printf("    persistent %d drop %d: %u instanced batches of %u instances, %u lights, %d indices\n", int(persistent), int(drop),
                                stats.instancedBatches, stats.instances, u32(ids.Size()), TestPipeline::lightIndexCount);
                    ok = false;
                }
                HX_CHECK(ok);
            }
        GraphicsSettings::SetUseScriptableRenderPipelineBatching(before);
        TestPipeline::perObjectData = PerObjectData::None;
        TestPipeline::dropFirstLight = false;
        GraphicsSettings::SetDefaultRenderPipeline(nullptr);
        DestroyImmediate(asset);
        for (GameObject* g : made)
            DestroyImmediate(g);
        DestroyImmediate(main->gameObject());
        DestroyImmediate(lights);
        Graphics::ReleaseResources();
        device.Destroy(color);
        device.Destroy(depth);
    });
}

namespace
{
    // Where a world point lands in a light's clip space (x, y, z over w).
    Vector3 ClipOf(const Matrix4x4& view, const Matrix4x4& projection, Vector3 p)
    {
        Vector4 c = projection * (view * Vector4(p.x, p.y, p.z, 1));
        return Vector3(c.x / c.w, c.y / c.w, c.z / c.w);
    }

    struct SpotPointResults
    {
        bool spot = false, spotCenter = false, spotEdge = false, spotPlanes = false, wrongKind = false;
        bool faces = true, widened = false;
        f32 sphere = 0;
    };
    SpotPointResults gSpotPoint;
}

HX_TEST(Rendering, SpotAndPointShadowsHaveTheirMatrices)
{
    // A spot light's view and cone, a point light's six faces (their axes and the cube map's up vectors) and fovBias;
    // the cascades' conservative sphere is smaller than the stable one; SetGlobalDepthBias pushes the draws after it.
    constexpr u32 size = 16;
    bool registered = false;
    ForEachDevice([&](rhi::Device& device) {
        TestShaderFolder shaders;
        rhi::TextureHandle color =
            device.CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
        rhi::TextureHandle depth = device.CreateTexture({.format = rhi::Format::D32Float, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(&device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
        ScriptableObject* asset = MakePipelineAsset(registered);
        HX_REQUIRE(asset != nullptr);
        GraphicsSettings::SetDefaultRenderPipeline(asset);
        GameObject* eye = GameObject::Create("ShadowEye");
        Camera* camera = eye->AddComponent<Camera>();
        camera->SetFieldOfView(60);
        camera->SetFarClipPlane(40);
        eye->transform()->SetPosition(Vector3(0, 0, -10));
        GameObject* spotObject = GameObject::Create("Spot");
        Light* spot = spotObject->AddComponent<Light>();
        spot->SetType(LightType::Spot);
        spot->SetSpotAngle(60);
        spot->SetRange(10);
        spotObject->transform()->SetPosition(Vector3(0, 0, -5));
        GameObject* pointObject = GameObject::Create("Point");
        Light* point = pointObject->AddComponent<Light>();
        point->SetType(LightType::Point);
        point->SetRange(5);
        GameObject* sunObject = GameObject::Create("Sun");
        Light* sun = sunObject->AddComponent<Light>();
        sun->SetType(LightType::Directional);
        sunObject->transform()->SetRotation(Quaternion::Euler(50, 0, 0));
        Material* surface = Material::Create(Shader::Find("Tests/Surface"));
        GameObject* caster = Thing("Caster", Quad(), surface, Vector3(0, 0, 0), 2);
        gSpotPoint = SpotPointResults();
        TestPipeline::afterCull = [](ScriptableRenderContext&, CullingResults culling) {
            Span<const VisibleLight> lights = culling.visibleLights();
            i32 spotIndex = -1, pointIndex = -1, sunIndex = -1;
            for (u32 i = 0; i < lights.Size(); i++)
            {
                if (lights[i].lightType == LightType::Directional)
                    sunIndex = i32(i);
                if (lights[i].lightType == LightType::Spot)
                    spotIndex = i32(i);
                if (lights[i].lightType == LightType::Point)
                    pointIndex = i32(i);
            }
            SpotPointResults& r = gSpotPoint;
            Matrix4x4 view, projection;
            ShadowSplitData split;
            if (sunIndex >= 0 && culling.ComputeDirectionalShadowMatricesAndCullingPrimitives(sunIndex, 0, 2, Vector3(0.5f, 0, 0), 256, 0.5f, view, projection, split))
                r.sphere = split.cullingSphere.w;
            r.spot = spotIndex >= 0 && culling.ComputeSpotShadowMatricesAndCullingPrimitives(spotIndex, view, projection, split);
            if (r.spot)
            {
                // The axis lands in the middle; the cone's rim at 5 along the axis is at the edge (60 degrees, tan 30).
                Vector3 center = ClipOf(view, projection, Vector3(0, 0, 0));
                Vector3 rim = ClipOf(view, projection, Vector3(5 * std::tan(30 * mathf::Deg2Rad), 0, 0));
                r.spotCenter = std::abs(center.x) < 1e-4f && std::abs(center.y) < 1e-4f && std::abs(center.z) < 1;
                r.spotEdge = std::abs(std::abs(rim.x) - 1) < 1e-3f;
                r.spotPlanes = split.cullingPlaneCount == 5 && split.cullingSphere.w > 5 && split.GetCullingPlane(0).GetDistanceToPoint(Vector3(0, 0, 0)) > 0;
            }
            r.wrongKind = pointIndex >= 0 && !culling.ComputeSpotShadowMatricesAndCullingPrimitives(pointIndex, view, projection, split);
            // Each face: its axis in the middle, the cube map's up on the screen's up.
            const Vector3 axes[6] = {Vector3(1, 0, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0), Vector3(0, -1, 0), Vector3(0, 0, 1), Vector3(0, 0, -1)};
            const Vector3 ups[6] = {Vector3(0, -1, 0), Vector3(0, -1, 0), Vector3(0, 0, 1), Vector3(0, 0, -1), Vector3(0, -1, 0), Vector3(0, -1, 0)};
            for (i32 f = 0; f < 6 && pointIndex >= 0; f++)
            {
                bool ok = culling.ComputePointShadowMatricesAndCullingPrimitives(pointIndex, CubemapFace(f), 0, view, projection, split);
                Vector3 middle = ClipOf(view, projection, axes[f] * 2.0f);
                Vector3 above = ClipOf(view, projection, axes[f] * 2.0f + ups[f]);
                bool good = ok && std::abs(middle.x) < 1e-4f && std::abs(middle.y) < 1e-4f && std::abs(above.x) < 1e-4f && std::abs(above.y - 0.5f) < 1e-3f &&
                            split.cullingSphere.w == 5;
                if (!good)
                    std::printf("    face %d: middle %.3f %.3f above %.3f %.3f\n", f, middle.x, middle.y, above.x, above.y);
                r.faces = r.faces && good;
            }
            r.widened = pointIndex >= 0 && culling.ComputePointShadowMatricesAndCullingPrimitives(pointIndex, CubemapFace::PositiveX, 10, view, projection, split) &&
                        std::abs(projection(1, 1) - 1.0f / std::tan(50 * mathf::Deg2Rad)) < 1e-4f &&
                        !culling.ComputePointShadowMatricesAndCullingPrimitives(pointIndex, CubemapFace::Unknown, 0, view, projection, split);
        };
        Graphics::RenderFrame();
        f32 stable = gSpotPoint.sphere;
        TestPipeline::conservativeSphere = true;
        Graphics::RenderFrame();
        f32 tight = gSpotPoint.sphere;
        TestPipeline::conservativeSphere = false;
        TestPipeline::afterCull = nullptr;
        const SpotPointResults& r = gSpotPoint;
        bool ok = r.spot && r.spotCenter && r.spotEdge && r.spotPlanes && r.wrongKind && r.faces && r.widened && tight > 0 && tight < stable;
        if (!ok)
            std::printf("    spot %d %d %d %d kind %d faces %d widened %d spheres %.3f %.3f\n", int(r.spot), int(r.spotCenter), int(r.spotEdge),
                        int(r.spotPlanes), int(r.wrongKind), int(r.faces), int(r.widened), stable, tight);
        HX_CHECK(ok);
        GraphicsSettings::SetDefaultRenderPipeline(nullptr);

        // Depth bias: a green quad over a red one at the same depth wins (LEqual) unless pushed back by the bias.
        Camera* front = MakeCamera(Vector3(0, 0, -10), 1);
        Material* red = Material::Create(Shader::Find("Tests/Unlit"));
        red->SetColor("color", Color(1, 0, 0, 1));
        Material* green = Material::Create(Shader::Find("Tests/Unlit"));
        green->SetColor("color", Color(0, 1, 0, 1));
        Mesh* quad = Quad();
        Matrix4x4 world = Matrix4x4::TRS(Vector3(0, 0, 0), Quaternion::identity, Vector3(4, 4, 1));
        Array<u32> pixels;
        pixels.Resize(size * size);
        u32 seen[3] = {};
        for (i32 k = 0; k < 3; k++)
        {
            CommandBuffer b;
            b.ClearRenderTarget(true, true, Color(0, 0, 0, 1));
            b.SetViewProjectionMatrices(front->worldToCameraMatrix(), front->projectionMatrix());
            b.DrawMesh(quad, world, red);
            if (k == 1)
                b.SetGlobalDepthBias(1000, 4);
            if (k == 2)
            {
                b.SetGlobalDepthBias(1000, 4);
                b.SetGlobalDepthBias(0, 0);
            }
            b.DrawMesh(quad, world, green);
            b.SetGlobalDepthBias(0, 0);
            Graphics::ExecuteCommandBuffer(b);
            HX_REQUIRE(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
            seen[k] = Pixel(pixels, size, 8, 8);
        }
        bool biased = NearColor(seen[0], Rgba(0, 255, 0)) && NearColor(seen[1], Rgba(255, 0, 0)) && NearColor(seen[2], Rgba(0, 255, 0));
        if (!biased)
            std::printf("    depth bias: %08x %08x %08x\n", seen[0], seen[1], seen[2]);
        HX_CHECK(biased);
        DestroyImmediate(red);
        DestroyImmediate(green);
        DestroyImmediate(front->gameObject());
        DestroyImmediate(asset);
        for (GameObject* g : {caster, sunObject, pointObject, spotObject, eye})
            DestroyImmediate(g);
        DestroyImmediate(surface);
        Graphics::ReleaseResources();
        device.Destroy(color);
        device.Destroy(depth);
    });
}

HX_TEST(Rendering, DirectionalShadowCascadesCullAndDrawTheirCasters)
{
    // A directional light with shadows over a caster: its bounds, two cascades fitting the split of the view, each
    // split's shadow list drawing the caster; a split whose planes leave the caster out draws nothing.
    constexpr u32 size = 16;
    bool registered = false;
    ForEachDevice([&](rhi::Device& device) {
        TestShaderFolder shaders;
        rhi::TextureHandle color =
            device.CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
        rhi::TextureHandle depth = device.CreateTexture({.format = rhi::Format::D32Float, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(&device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
        ScriptableObject* asset = MakePipelineAsset(registered);
        HX_REQUIRE(asset != nullptr);
        GraphicsSettings::SetDefaultRenderPipeline(asset);
        GameObject* eye = GameObject::Create("ShadowEye");
        Camera* camera = eye->AddComponent<Camera>();
        camera->SetFieldOfView(60);
        camera->SetFarClipPlane(40);
        eye->transform()->SetPosition(Vector3(0, 0, -10));
        GameObject* sun = GameObject::Create("ShadowSun");
        Light* light = sun->AddComponent<Light>();
        light->SetType(LightType::Directional);
        light->SetShadows(LightShadows::Hard);
        sun->transform()->SetRotation(Quaternion::Euler(50, 0, 0));
        Material* surface = Material::Create(Shader::Find("Tests/Surface"));
        GameObject* caster = Thing("Caster", Quad(), surface, Vector3(0, 0, 0), 2);
        TestPipeline::shadowSplits = 2;
        TestPipeline::awayFromCasters = false;
        // The caster's ShadowCaster pass links (its fragment stage takes the varyings it does not read).
        u32 errors = 0;
        LogSink count = [](const LogMessage& m, void* user) {
            if (m.level >= LogLevel::Error)
                ++*static_cast<u32*>(user);
        };
        AddLogSink(count, &errors);
        Graphics::RenderFrame();
        RemoveLogSink(count, &errors);
        HX_CHECK(errors == 0);
        u32 bothDraws = Graphics::LastStats().drawCalls;
        Bounds b = TestPipeline::casterBounds;
        bool bounds = TestPipeline::shadowBounds && Near(b.center.x, 0) && Near(b.center.y, 0) && Near(b.size().x, 2) && Near(b.size().y, 2);
        // The first split ends at a quarter of the 40 of the camera's far plane: its sphere holds the split's corners.
        const ShadowSplitData& first = TestPipeline::splits[0];
        bool sphere = first.cullingSphere.w > 0 && first.cullingPlaneCount == 5 && TestPipeline::splits[1].cullingSphere.w > first.cullingSphere.w;
        // The caster's center in the first split's clip space is inside the box.
        Vector4 clip = TestPipeline::shadowProjections[0] * (TestPipeline::shadowViews[0] * Vector4(0, 0, 0, 1));
        bool inside = std::abs(clip.x / clip.w) <= 1 && std::abs(clip.y / clip.w) <= 1;
        TestPipeline::awayFromCasters = true;
        Graphics::RenderFrame();
        u32 oneDraw = Graphics::LastStats().drawCalls;
        // The camera's own pass draws the caster once more than the shadow lists.
        bool ok = bounds && sphere && inside && bothDraws == oneDraw + 1;
        if (!ok)
            std::printf("    bounds %d sphere %d inside %d draws %u %u\n", int(bounds), int(sphere), int(inside), bothDraws, oneDraw);
        HX_CHECK(ok);
        TestPipeline::shadowSplits = 0;
        TestPipeline::awayFromCasters = false;
        GraphicsSettings::SetDefaultRenderPipeline(nullptr);
        DestroyImmediate(asset);
        DestroyImmediate(caster);
        DestroyImmediate(sun);
        DestroyImmediate(eye);
        DestroyImmediate(surface);
        Graphics::ReleaseResources();
        device.Destroy(color);
        device.Destroy(depth);
    });
}
