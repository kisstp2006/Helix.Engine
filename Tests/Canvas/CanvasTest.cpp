#include <Support/Devices.h>
#include <Support/Golden.h>
#include <Support/TestClasses.h>
#include <Support/TestShaders.h>

#include <Helix/Engine/Canvas/Canvas.h>
#include <Helix/Engine/Canvas/CanvasRenderer.h>
#include <Helix/Engine/Material/Material.h>
#include <Helix/Engine/Mesh/Mesh.h>
#include <Helix/Engine/Rendering/Camera.h>
#include <Helix/Engine/Rendering/Graphics.h>
#include <Helix/Engine/Shader/Shader.h>
#include <Helix/Engine/Texture/Texture.h>
#include <Helix/Engine/Transform/RectTransform.h>
#include <Helix/Engine/Transform/RectTransformUtility.h>
#include <Helix/Testing/Test.h>

#include <cmath>
#include <cstdio>

using namespace helix;
using namespace helix::engine;
using namespace helix::engine::testing;

namespace
{
    bool Near(f32 a, f32 b, f32 eps = 1e-3f) { return std::fabs(a - b) <= eps; }
    bool Near(Vector2 a, Vector2 b, f32 eps = 1e-3f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps); }
    bool Near(Vector3 a, Vector3 b, f32 eps = 1e-3f) { return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps); }

    RectTransform* Rt(GameObject* g) { return static_cast<RectTransform*>(g->transform()); }

    Canvas* MakeCanvas(StringView name, RenderMode mode = RenderMode::ScreenSpaceOverlay)
    {
        GameObject* g = GameObject::Create(name);
        Canvas* c = g->AddComponent<Canvas>();
        c->SetRenderMode(mode);
        return c;
    }

    // An element: a RectTransform of the size at the anchored position (anchors and pivot centered) under the parent.
    GameObject* Element(StringView name, Transform* parent, Vector2 position, Vector2 size)
    {
        GameObject* g = GameObject::Create(name);
        auto* r = static_cast<RectTransform*>(g->AddComponent(ClassOf<RectTransform>()));
        r->SetParent(parent, false);
        r->SetSizeDelta(size);
        r->SetAnchoredPosition(position);
        return g;
    }

    // A colored quad over its rectangle (bottom left, top left, top right, bottom right) with the default material.
    CanvasRenderer* Quad(GameObject* g, Color32 color, Material* material = nullptr)
    {
        CanvasRenderer* r = g->AddComponent<CanvasRenderer>();
        Rect rect = Rt(g)->rect();
        UIVertex v[4];
        const Vector2 corners[4] = {{rect.xMin(), rect.yMin()}, {rect.xMin(), rect.yMax()}, {rect.xMax(), rect.yMax()}, {rect.xMax(), rect.yMin()}};
        const Vector2 uvs[4] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
        for (u32 i = 0; i < 4; i++)
        {
            v[i].position = Vector3(corners[i].x, corners[i].y, 0);
            v[i].color = color;
            v[i].uv0 = Vector4(uvs[i].x, uvs[i].y, 0, 0);
        }
        r->SetVertices(Span<const UIVertex>(v, 4), 4);
        r->SetMaterial(material ? material : Canvas::GetDefaultCanvasMaterial(), Texture2D::whiteTexture());
        return r;
    }

    u32 Pixel(const Array<u32>& pixels, u32 size, u32 x, u32 y) { return pixels[y * size + x]; }

    bool NearColor(u32 a, u32 b, i32 tolerance = 4)
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

HX_TEST(Canvas, ScreenSpaceCanvasesAreTheScreenOrTheCamerasView)
{
    Camera::SetDisplaySize(200, 100);
    Canvas* overlay = MakeCanvas("Overlay");
    // A canvas replaces the Transform with a RectTransform, which it drives.
    HX_REQUIRE(overlay->transform()->Is<RectTransform>());
    canvas::Batch(false);
    RectTransform* r = Rt(overlay->gameObject());
    HX_CHECK(Near(r->rect().width, 200) && Near(r->rect().height, 100) && Near(r->position(), Vector3(100, 50, 0)));
    HX_CHECK(r->drivenByObject() == overlay && overlay->isRootCanvas() && overlay->rootCanvas() == overlay);
    HX_CHECK(Near(overlay->renderingDisplaySize(), Vector2(200, 100)) && Near(overlay->pixelRect().width, 200));
    // Scaled by 2: half the units, twice the scale.
    overlay->SetScaleFactor(2);
    canvas::Batch(false);
    HX_CHECK(Near(r->rect().width, 100) && Near(r->localScale(), Vector3(2, 2, 2)));
    // A nested canvas has its root's mode and scale.
    GameObject* inner = Element("Inner", overlay->transform(), Vector2(0, 0), Vector2(20, 20));
    Canvas* nested = inner->AddComponent<Canvas>();
    nested->SetRenderMode(RenderMode::WorldSpace);
    HX_CHECK(!nested->isRootCanvas() && nested->rootCanvas() == overlay && nested->renderMode() == RenderMode::ScreenSpaceOverlay);
    HX_CHECK(Near(nested->scaleFactor(), 2));
    nested->SetScaleFactor(5);
    HX_CHECK(Near(nested->scaleFactor(), 2));
    // In front of a camera at its plane distance: its rotation, filling its view.
    GameObject* eye = GameObject::Create("Eye");
    eye->transform()->SetPosition(Vector3(1, 2, 3));
    eye->transform()->SetRotation(Quaternion::Euler(0, 90, 0));
    Camera* camera = eye->AddComponent<Camera>();
    camera->SetFieldOfView(60);
    Canvas* view = MakeCanvas("View", RenderMode::ScreenSpaceCamera);
    view->SetWorldCamera(camera);
    view->SetPlaneDistance(10);
    canvas::Batch(false);
    RectTransform* v = Rt(view->gameObject());
    f32 worldHeight = 2 * 10 * std::tan(30.0f * 3.14159265f / 180.0f);
    HX_CHECK(Near(v->position(), Vector3(11, 2, 3)) && Near(v->rect().height, 100) && Near(v->lossyScale().y, worldHeight / 100, 1e-4f));
    HX_CHECK(view->worldCamera() == camera && Near(view->pixelRect().height, 100));
    // A world canvas is where its transform puts it (no longer driven).
    view->SetRenderMode(RenderMode::WorldSpace);
    canvas::Batch(false);
    HX_CHECK(v->drivenByObject() == nullptr);
    // Sorting: the overlay canvases by sorting order, the nested one with its own sorting apart.
    nested->SetOverrideSorting(true);
    nested->SetSortingOrder(-1);
    canvas::Batch(false);
    HX_CHECK(canvas::CanvasCount() == 3 && nested->renderOrder() == 0 && overlay->renderOrder() == 1 && view->renderOrder() == 2);
    DestroyImmediate(overlay->gameObject());
    DestroyImmediate(view->gameObject());
    DestroyImmediate(eye);
    canvas::Batch(false);
    HX_CHECK(canvas::CanvasCount() == 0);
    Camera::SetDisplaySize(1, 1);
    Cleanup();
}

HX_TEST(Canvas, RenderersGetDepthsAlphaAndBatchesInHierarchyOrder)
{
    Camera::SetDisplaySize(100, 100);
    Canvas* c = MakeCanvas("Canvas");
    Transform* root = c->transform();
    GameObject* a = Element("A", root, Vector2(-20, 0), Vector2(10, 10));
    GameObject* group = Element("Group", root, Vector2(0, 0), Vector2(50, 50));
    GameObject* b = Element("B", group->transform(), Vector2(0, 0), Vector2(10, 10));
    GameObject* d = Element("D", group->transform(), Vector2(10, 0), Vector2(10, 10));
    CanvasRenderer* ra = Quad(a, Color32(255, 0, 0, 255));
    CanvasRenderer* rb = Quad(b, Color32(0, 255, 0, 255));
    CanvasRenderer* rd = Quad(d, Color32(0, 0, 255, 255));
    CanvasGroup* g = group->AddComponent<CanvasGroup>();
    g->SetAlpha(0.5f);
    canvas::Batch(false);
    HX_CHECK(ra->absoluteDepth() == 0 && rb->absoluteDepth() == 1 && rd->absoluteDepth() == 2);
    HX_CHECK(Near(ra->GetInheritedAlpha(), 1) && Near(rb->GetInheritedAlpha(), 0.5f) && Near(rd->GetInheritedAlpha(), 0.5f));
    // Same material and texture: one draw for all three.
    HX_CHECK(canvas::BatchCount(*c) == 1 && rb->hasMoved());
    canvas::Batch(false);
    HX_CHECK(!rb->hasMoved());
    Rt(b)->SetAnchoredPosition(Vector2(1, 1));
    canvas::Batch(false);
    HX_CHECK(rb->hasMoved());
    // A clip apart breaks the run; a culled renderer keeps its depth and draws nothing.
    rb->EnableRectClipping(Rect(-5, -5, 10, 10));
    canvas::Batch(false);
    HX_CHECK(canvas::BatchCount(*c) == 3 && rb->hasRectClipping());
    rb->DisableRectClipping();
    rd->SetCull(true);
    canvas::Batch(false);
    HX_CHECK(canvas::BatchCount(*c) == 1 && rd->absoluteDepth() == 2);
    // Outside its clip: not drawn.
    rd->SetCull(false);
    rd->EnableRectClipping(Rect(100, 100, 5, 5));
    canvas::Batch(false);
    HX_CHECK(canvas::BatchCount(*c) == 1);
    rd->DisableRectClipping();
    // A transparent renderer culled when it asks; a group ignoring its parents.
    g->SetAlpha(0);
    rb->SetCullTransparentMesh(true);
    canvas::Batch(false);
    HX_CHECK(canvas::BatchCount(*c) == 1 && Near(rd->GetInheritedAlpha(), 0));
    CanvasGroup* own = d->AddComponent<CanvasGroup>();
    own->SetIgnoreParentGroups(true);
    canvas::Batch(false);
    HX_CHECK(Near(rd->GetInheritedAlpha(), 1) && g->IsRaycastLocationValid(Vector2(0, 0), nullptr));
    // A nested canvas: its own relative order; disabled, it hides what is under it.
    Canvas* nested = group->AddComponent<Canvas>();
    canvas::Batch(false);
    HX_CHECK(rb->relativeDepth() == 0 && rd->relativeDepth() == 1 && rb->absoluteDepth() == 1);
    nested->SetEnabled(false);
    canvas::Batch(false);
    HX_CHECK(rb->absoluteDepth() == -1 && ra->absoluteDepth() == 0);
    // Inactive: no depth.
    a->SetActive(false);
    canvas::Batch(false);
    HX_CHECK(ra->absoluteDepth() == -1);
    // The materials: a pop material after the children, the count settable.
    a->SetActive(true);
    nested->SetEnabled(true);
    ra->SetMaterialCount(2);
    HX_CHECK(ra->materialCount() == 2 && ra->GetMaterial(1) == nullptr && ra->GetMaterial() == Canvas::GetDefaultCanvasMaterial());
    ra->SetPopMaterial(Canvas::GetDefaultCanvasMaterial(), 0);
    ra->SetHasPopInstruction(true);
    HX_CHECK(ra->popMaterialCount() == 1 && ra->GetPopMaterial(0) == Canvas::GetDefaultCanvasMaterial());
    ra->SetSecondaryTexture(0, "_MaskTex", Texture2D::whiteTexture());
    HX_CHECK(ra->GetSecondaryTextureCount() == 1 && ra->GetSecondaryTextureName(0) == StringView("_MaskTex"));
    ra->Clear();
    HX_CHECK(ra->materialCount() == 0 && ra->vertices().Empty() && ra->GetSecondaryTextureCount() == 0);
    DestroyImmediate(c->gameObject());
    Camera::SetDisplaySize(1, 1);
    Cleanup();
}

HX_TEST(Canvas, MeshesAndVertexStreams)
{
    GameObject* g = GameObject::Create("R");
    CanvasRenderer* r = g->AddComponent<CanvasRenderer>();
    Mesh* m = NewObject<Mesh>();
    const Vector3 p[] = {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}};
    const Color32 colors[] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 255, 128}};
    const i32 t[] = {0, 1, 2, 2, 3, 0};
    m->SetVertices(Span<const Vector3>(p, 4));
    m->SetColors(Span<const Color32>(colors, 4));
    m->SetTriangles(Span<const i32>(t, 6));
    r->SetMesh(m);
    HX_CHECK(r->GetMesh() == m && r->vertices().Size() == 4 && r->indices().Size() == 6 && r->vertices()[3].color == colors[3]);
    // A triangle list split into streams with its shared vertices once, and back.
    Array<UIVertex> list;
    for (i32 i : t)
    {
        UIVertex v;
        v.position = p[i];
        v.color = colors[i];
        list.Push(v);
    }
    Array<Vector3> positions, normals;
    Array<Color32> cs;
    Array<Vector4> uv0, uv1, uv2, uv3, tangents;
    Array<i32> indices;
    CanvasRenderer::SplitUIVertexStreams(Span<const UIVertex>(list.Data(), list.Size()), positions, cs, uv0, uv1, uv2, uv3, normals, tangents, indices);
    HX_CHECK(positions.Size() == 4 && indices.Size() == 6 && indices[3] == indices[2] && indices[5] == indices[0]);
    Array<UIVertex> back;
    CanvasRenderer::CreateUIVertexStream(back, Span<const Vector3>(positions.Data(), positions.Size()), Span<const Color32>(cs.Data(), cs.Size()),
                                         Span<const Vector4>(uv0.Data(), uv0.Size()), Span<const Vector4>(uv1.Data(), uv1.Size()),
                                         Span<const Vector4>(uv2.Data(), uv2.Size()), Span<const Vector4>(uv3.Data(), uv3.Size()),
                                         Span<const Vector3>(normals.Data(), normals.Size()), Span<const Vector4>(tangents.Data(), tangents.Size()),
                                         Span<const i32>(indices.Data(), indices.Size()));
    HX_CHECK(back.Size() == 6 && back[4].position == p[3] && back[4].color == colors[3]);
    Array<UIVertex> added;
    CanvasRenderer::AddUIVertexStream(added, Span<const Vector3>(positions.Data(), positions.Size()), Span<const Color32>(cs.Data(), cs.Size()), {}, {}, {}, {},
                                      {}, {});
    HX_CHECK(added.Size() == 4 && added[1].normal == Vector3(0, 0, -1));
    DestroyImmediate(g);
    DestroyImmediate(m);
    Cleanup();
}

HX_TEST(Canvas, PixelPerfectCanvasesSnapPointsToPixels)
{
    Camera::SetDisplaySize(101, 100);
    Canvas* c = MakeCanvas("Canvas");
    GameObject* e = Element("E", c->transform(), Vector2(0.3f, 0.2f), Vector2(10.4f, 10));
    canvas::Batch(false);
    // Not pixel perfect: as it is.
    HX_CHECK(Near(RectTransformUtility::PixelAdjustPoint(Vector2(0, 0), e->transform(), c), Vector2(0, 0)));
    c->SetPixelPerfect(true);
    // The screen's center is at 50.5: the element's origin (50.8) goes to pixel 51.
    Vector2 snapped = RectTransformUtility::PixelAdjustPoint(Vector2(0, 0), e->transform(), c);
    HX_CHECK(Near(snapped, Vector2(0.2f, -0.2f)));
    Rect r = RectTransformUtility::PixelAdjustRect(Rt(e), c);
    HX_CHECK(Near(r.xMin() + 50.8f, std::round(r.xMin() + 50.8f)) && Near(r.width, 10, 1.01f));
    DestroyImmediate(c->gameObject());
    Camera::SetDisplaySize(1, 1);
    Cleanup();
}

// Overlay, camera and world canvases on one frame: quads tinted, faded by a group, clipped softly, masked by the
// stencil, a world canvas turned in 3D and hidden behind an opaque quad where it is behind it.
HX_TEST(Canvas, TheThreeModesDrawOnEveryDevice)
{
    constexpr u32 size = 64;
    u32 devices = 0;
    ForEachDevice([&](rhi::Device& device) {
        devices++;
        TestShaderFolder shaders;
        HX_REQUIRE(Shader::Find("UI/Default") && Shader::Find("Tests/Unlit"));
        rhi::TextureHandle color =
            device.CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
        rhi::TextureHandle depth =
            device.CreateTexture({.format = rhi::Format::D24UnormS8Uint, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(&device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D24UnormS8Uint});
        // The camera looks along +z from z = -10 (perspective, 60 degrees), clearing to black.
        GameObject* eye = GameObject::Create("Eye");
        eye->transform()->SetPosition(Vector3(0, 0, -10));
        Camera* camera = eye->AddComponent<Camera>();
        camera->SetFieldOfView(60);
        camera->SetClearFlags(CameraClearFlags::SolidColor);
        camera->SetBackgroundColor(Color(0, 0, 0, 1));

        // The overlay: a red quad bottom left, a half faded green one, a blue one clipped to its left half (softly).
        Canvas* overlay = MakeCanvas("Overlay");
        Transform* o = overlay->transform();
        Quad(Element("Red", o, Vector2(-22, -22), Vector2(16, 16)), Color32(255, 0, 0, 255));
        GameObject* fade = Element("Fade", o, Vector2(-22, 22), Vector2(16, 16));
        Quad(fade, Color32(0, 255, 0, 255));
        fade->AddComponent<CanvasGroup>()->SetAlpha(0.5f);
        CanvasRenderer* clipped = Quad(Element("Clipped", o, Vector2(22, 22), Vector2(16, 16)), Color32(0, 0, 255, 255));
        clipped->EnableRectClipping(Rect(14, 14, 8, 16));
        clipped->SetClippingSoftness(Vector2(2, 0));
        // A stencil mask: the mask writes 1 without color, the child shows where it is 1 only.
        Material* maskWrite = Material::Create(*Canvas::GetDefaultCanvasMaterial());
        maskWrite->SetFloat("_StencilComp", 8);
        maskWrite->SetFloat("_Stencil", 1);
        maskWrite->SetFloat("_StencilOp", 2);
        maskWrite->SetFloat("_ColorMask", 0);
        Material* masked = Material::Create(*Canvas::GetDefaultCanvasMaterial());
        masked->SetFloat("_StencilComp", 3);
        masked->SetFloat("_Stencil", 1);
        GameObject* mask = Element("Mask", o, Vector2(22, -22), Vector2(8, 16));
        Quad(mask, Color32(255, 255, 255, 255), maskWrite);
        Quad(Element("Masked", mask->transform(), Vector2(4, 0), Vector2(16, 16)), Color32(255, 255, 0, 255), masked);

        // A camera canvas behind the overlay: a white bar across the middle.
        Canvas* view = MakeCanvas("View", RenderMode::ScreenSpaceCamera);
        view->SetWorldCamera(camera);
        view->SetPlaneDistance(5);
        Quad(Element("Bar", view->transform(), Vector2(0, 0), Vector2(64, 6)), Color32(255, 255, 255, 255));

        // A world canvas at z = 0 turned -50 degrees about y: a magenta panel, its right half going away behind an opaque
        // gray quad (depth tested).
        Canvas* world = MakeCanvas("World", RenderMode::WorldSpace);
        RectTransform* w = Rt(world->gameObject());
        w->SetSizeDelta(Vector2(100, 100));
        w->SetLocalScale(Vector3(0.05f, 0.05f, 0.05f));
        w->SetPosition(Vector3(0, -2, 0));
        w->SetRotation(Quaternion::Euler(0, -50, 0));
        Quad(Element("Panel", w, Vector2(0, 0), Vector2(100, 40)), Color32(255, 0, 255, 255));
        Mesh* quadMesh = NewObject<Mesh>();
        {
            const Vector3 v[] = {{-0.5f, -0.5f, 0}, {-0.5f, 0.5f, 0}, {0.5f, 0.5f, 0}, {0.5f, -0.5f, 0}};
            const i32 t[] = {0, 1, 2, 0, 2, 3};
            quadMesh->SetVertices(Span<const Vector3>(v, 4));
            quadMesh->SetTriangles(Span<const i32>(t, 6));
        }
        Material* gray = Material::Create(Shader::Find("Tests/Unlit"));
        gray->SetColor("color", Color(0.5f, 0.5f, 0.5f, 1));
        GameObject* blocker = GameObject::Create("Blocker");
        blocker->transform()->SetPosition(Vector3(1.2f, -2, 0));
        blocker->transform()->SetLocalScale(Vector3(2, 2, 1));
        blocker->AddComponent<MeshFilter>()->SetSharedMesh(quadMesh);
        blocker->AddComponent<MeshRenderer>()->SetSharedMaterial(gray);

        canvas::Batch(true);
        canvas::QueueDraws();
        Graphics::RenderFrame();
        Array<u32> pixels;
        pixels.Resize(size * size);
        HX_REQUIRE(device.ReadTexture(color, 0, pixels.Data(), pixels.Size() * 4));
        // Rows top first; the canvases' y up: (x, size - 1 - y).
        auto at = [&](u32 x, u32 y) { return Pixel(pixels, size, x, size - 1 - y); };
        bool red = NearColor(at(10, 10), Rgba(255, 0, 0));
        // Premultiplied over the opaque clear: the target's alpha stays 1.
        bool faded = NearColor(at(10, 54), Rgba(0, 128, 0), 6);
        // The clip's left edge fades over 2 units (x 46 to 48 here), its right one too (52 to 54).
        bool clipLeft = NearColor(at(50, 52), Rgba(0, 0, 255), 6) && NearColor(at(47, 52), Rgba(0, 0, 191), 8);
        bool clipRight = NearColor(at(56, 52), Rgba(0, 0, 0)) && NearColor(at(53, 52), Rgba(0, 0, 64), 8);
        bool maskIn = NearColor(at(56, 10), Rgba(255, 255, 0)), maskOut = NearColor(at(62, 10), Rgba(0, 0, 0));
        bool bar = NearColor(at(32, 32), Rgba(255, 255, 255));
        bool behind = NearColor(at(39, 21), Rgba(128, 128, 128), 6), panel = NearColor(at(26, 21), Rgba(255, 0, 255));
        if (!(red && faded && clipLeft && clipRight && maskIn && maskOut && bar && behind && panel))
            std::printf("    red %d faded %d clip %d/%d mask %d/%d bar %d world %d/%d (%08x %08x %08x %08x)\n", int(red), int(faded), int(clipLeft), int(clipRight),
                        int(maskIn), int(maskOut), int(bar), int(behind), int(panel), at(10, 54), at(47, 52), at(39, 21), at(26, 21));
        HX_CHECK(red && faded && clipLeft && clipRight && maskIn && maskOut && bar && behind && panel);
        HX_CHECK(MatchGolden("CanvasModes", pixels.Data(), size, size, 6));
        DestroyImmediate(overlay->gameObject());
        DestroyImmediate(view->gameObject());
        DestroyImmediate(world->gameObject());
        DestroyImmediate(blocker);
        DestroyImmediate(eye);
        DestroyImmediate(maskWrite);
        DestroyImmediate(masked);
        DestroyImmediate(gray);
        DestroyImmediate(quadMesh);
        canvas::Batch(false);
        Graphics::ReleaseResources();
        Graphics::SetTarget({});
        device.Destroy(color);
        device.Destroy(depth);
    });
    std::printf("    %u devices\n", devices);
}
