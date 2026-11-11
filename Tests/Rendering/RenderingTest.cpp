#include <Support/Golden.h>
#include <Support/TestShaders.h>

#include <Helix/Engine/Rendering/Graphics.h>
#include <Helix/Engine/Rendering/Light.h>
#include <Helix/Engine/Rendering/Renderer.h>
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

HX_TEST(Rendering, PlaceholderRendererDrawsOnEveryDevice)
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
        HX_CHECK(MatchGolden("PlaceholderScene", pixels.Data(), size, size));
        // A second frame makes nothing new.
        Graphics::RenderFrame();
        HX_CHECK(Graphics::LastStats().pipelinesCreated == 0 && Graphics::LastStats().uploads == 0);
        Graphics::ReleaseResources();
        device.Destroy(color);
        device.Destroy(depth);
    });
    std::printf("    %u devices\n", devices);
}
