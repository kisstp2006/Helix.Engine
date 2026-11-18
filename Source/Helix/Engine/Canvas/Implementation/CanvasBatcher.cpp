#include <Helix/Engine/Canvas/Canvas.h>
#include <Helix/Engine/Canvas/CanvasRenderer.h>

#include <Helix/Engine/Material/Material.h>
#include <Helix/Engine/Mesh/Mesh.h>
#include <Helix/Engine/Rendering/Camera.h>
#include <Helix/Engine/Rendering/Graphics.h>
#include <Helix/Engine/Rendering/Pipeline/CommandBuffer.h>
#include <Helix/Engine/Script/ScriptRuntime.h>
#include <Helix/Engine/Shader/Shader.h>
#include <Helix/Engine/Texture/Texture.h>
#include <Helix/Engine/Transform/RectTransform.h>

#include <cmath>
#include <cstring>
#include <memory>

// The canvas batcher: each frame every root canvas walks its hierarchy in order (a nested canvas with its own sorting
// starts a canvas drawn apart), the CanvasGroups' alphas multiplying down, and puts each CanvasRenderer's geometry (in
// the root canvas's space, tinted, clipped) into the drawing canvas's mesh: one submesh per run of draws that share
// material, textures and clip. Overlay canvases draw over the screen by sorting order; the others are queued for the
// cameras (a camera canvas for its camera only), depth tested against the scene.
namespace helix::engine
{
    struct CanvasBatcherAccess
    {
        static CanvasRenderer& R(Object* o) { return *static_cast<CanvasRenderer*>(o); }
        static CanvasBatches*& Of(Canvas& c) { return c.batches_; }
        static void SetOrder(Canvas& c, i32 order) { c.renderOrder_ = order; }
        static RenderMode OwnMode(const Canvas& c) { return c.renderMode_; }
        static Camera* OwnCamera(const Canvas& c) { return c.camera_.Get(); }
        static f32 OwnScale(const Canvas& c) { return c.scaleFactor_; }
        static void Reset(CanvasRenderer& r)
        {
            r.absoluteDepth_ = r.relativeDepth_ = -1;
            r.inheritedAlpha_ = 1;
        }
        static void Place(CanvasRenderer& r, i32 absolute, i32 relative, f32 alpha, const Matrix4x4& world)
        {
            r.absoluteDepth_ = absolute, r.relativeDepth_ = relative, r.inheritedAlpha_ = alpha;
            r.hasMoved_ = !r.placed_ || std::memcmp(&r.lastMatrix_, &world, sizeof(Matrix4x4)) != 0;
            r.lastMatrix_ = world, r.placed_ = true;
        }
        static bool HasPop(const CanvasRenderer& r) { return r.hasPop_; }
        static Span<const PPtr<Material>> Materials(const CanvasRenderer& r, bool pop)
        {
            const Array<PPtr<Material>>& m = pop ? r.popMaterials_ : r.materials_;
            return Span<const PPtr<Material>>(m.Data(), m.Size());
        }
        struct Secondary
        {
            String name;
            PPtr<Texture2D> texture;
            bool operator==(const Secondary&) const = default;
        };
        static void Secondaries(const CanvasRenderer& r, Array<Secondary>& out)
        {
            out.Clear();
            for (const CanvasRenderer::Secondary& s : r.secondary_)
                out.Push({s.name, s.texture});
        }
    };

    // One interleaved vertex of a canvas's mesh: position, normal, tangent, color (gamma, alpha straight), four
    // texture coordinates.
    struct CanvasVertex
    {
        Vector3 position;
        Vector3 normal;
        Vector4 tangent;
        Vector4 color;
        Vector4 uv[4];
    };
    static_assert(sizeof(CanvasVertex) == 30 * sizeof(f32), "the mesh's interleaved layout");

    struct CanvasBatch
    {
        PPtr<Material> material;
        PPtr<Texture> texture, alpha;
        Array<CanvasBatcherAccess::Secondary> secondaries;
        bool clip = false;
        Rect clipRect;
        Vector2 softness = Vector2(0, 0);
        u32 firstIndex = 0, indexCount = 0;
    };

    // What a canvas drawn apart keeps: its mesh, the batches and their property blocks.
    struct CanvasBatches
    {
        ~CanvasBatches()
        {
            if (Mesh* m = mesh.Get())
                Object::DestroyImmediate(m);
        }

        PPtr<Mesh> mesh;
        PPtr<Canvas> owner, root;
        Array<CanvasVertex> vertices;
        Array<u32> indices;
        Array<CanvasBatch> list;
        Array<std::unique_ptr<MaterialPropertyBlock>> blocks;
        Array<SubMeshDescriptor> subs;
        u32 vertexCapacity = 0, indexCapacity = 0, serial = 0, encounter = 0;
        // The root canvas's world matrix when batched (the mesh is in its space).
        Matrix4x4 toWorld = Matrix4x4::identity;
        RenderMode mode = RenderMode::ScreenSpaceOverlay;
        PPtr<Camera> camera;
        i32 layer = 0;
    };

    namespace
    {
        struct State
        {
            Array<CanvasBatches*> drawing;
            u32 serial = 0;
            u32 drawer = 0;
            bool queuedThisFrame = false;
            Array<CanvasVertex> scratch;
            Array<CanvasBatcherAccess::Secondary> secondaries;
        };

        State& S()
        {
            static State* s = new State();
            return *s;
        }

        // ------------------------------------------------------------------------------------------- the root's place

        bool Near(f32 a, f32 b) { return std::fabs(a - b) <= 1e-5f * (1.0f + std::fabs(a) + std::fabs(b)); }
        bool Near(Vector3 a, Vector3 b) { return Near(a.x, b.x) && Near(a.y, b.y) && Near(a.z, b.z); }
        bool Near(Vector2 a, Vector2 b) { return Near(a.x, b.x) && Near(a.y, b.y); }

        // The screen space canvases are the screen (or the camera's view at the plane distance): their RectTransform
        // set so, driven by them.
        void DriveRoot(Canvas& canvas)
        {
            Transform* t = canvas.transform();
            if (!t || !t->Is<RectTransform>())
                return;
            auto* rt = static_cast<RectTransform*>(t);
            RenderMode mode = CanvasBatcherAccess::OwnMode(canvas);
            Camera* camera = CanvasBatcherAccess::OwnCamera(canvas);
            if (mode == RenderMode::WorldSpace)
            {
                if (rt->drivenByObject() == &canvas)
                    rt->SetDriven(nullptr, DrivenTransformProperties::None);
                return;
            }
            f32 scale = CanvasBatcherAccess::OwnScale(canvas);
            Vector3 position, worldScale;
            Quaternion rotation = Quaternion::identity;
            Vector2 size;
            if (mode == RenderMode::ScreenSpaceCamera && camera)
            {
                Rect pixels = camera->pixelRect();
                f32 near = camera->nearClipPlane(), far = camera->farClipPlane();
                f32 d = canvas.planeDistance();
                d = d < near ? near : (d > far ? far : d);
                Transform* ct = camera->transform();
                f32 worldHeight = camera->orthographic() ? 2.0f * camera->orthographicSize()
                                                         : 2.0f * d * std::tan(camera->fieldOfView() * 0.5f * 3.14159265358979f / 180.0f);
                size = Vector2(pixels.width / scale, pixels.height / scale);
                f32 k = size.y > 0 ? worldHeight / size.y : 1.0f;
                rotation = ct->rotation();
                position = ct->position() + rotation * Vector3(0, 0, d);
                worldScale = Vector3(k, k, k);
            }
            else
            {
                Vector2 screen = Camera::DisplaySize();
                size = Vector2(screen.x / scale, screen.y / scale);
                position = Vector3(screen.x * 0.5f, screen.y * 0.5f, 0);
                worldScale = Vector3(scale, scale, scale);
            }
            if (!Near(rt->anchorMin(), Vector2(0, 0)))
                rt->SetAnchorMin(Vector2(0, 0));
            if (!Near(rt->anchorMax(), Vector2(0, 0)))
                rt->SetAnchorMax(Vector2(0, 0));
            if (!Near(rt->pivot(), Vector2(0.5f, 0.5f)))
                rt->SetPivot(Vector2(0.5f, 0.5f));
            if (!Near(rt->sizeDelta(), size))
                rt->SetSizeDelta(size);
            // The world placement (a parent above the canvas does not move it).
            Transform* parent = rt->parent();
            Vector3 parentScale = parent ? parent->lossyScale() : Vector3(1, 1, 1);
            Vector3 local(parentScale.x != 0 ? worldScale.x / parentScale.x : worldScale.x, parentScale.y != 0 ? worldScale.y / parentScale.y : worldScale.y,
                          parentScale.z != 0 ? worldScale.z / parentScale.z : worldScale.z);
            if (!Near(rt->localScale(), local))
                rt->SetLocalScale(local);
            if (!Near(rt->position(), position))
                rt->SetPosition(position);
            Quaternion now = rt->rotation();
            if (std::fabs(Quaternion::Dot(now, rotation)) < 1.0f - 1e-6f)
                rt->SetRotation(rotation);
            if (rt->drivenByObject() != &canvas)
                rt->SetDriven(&canvas, DrivenTransformProperties::All);
        }

        // ---------------------------------------------------------------------------------------------- the walk

        struct Walk
        {
            Canvas* root = nullptr;
            Matrix4x4 rootFromWorld = Matrix4x4::identity;
            AdditionalCanvasShaderChannels channels = AdditionalCanvasShaderChannels::None;
            // Pixel snapping of screen space canvases: canvas units to pixels and the root's pixel origin.
            f32 pixelScale = 1;
            Vector2 pixelOrigin = Vector2(0, 0);
            bool screenSpace = false;
            i32 absolute = 0;
            Array<CanvasBatches*>* out = nullptr;
            u32 encounter = 0;
        };

        CanvasBatches& Start(Canvas& owner, Walk& w)
        {
            CanvasBatches*& b = CanvasBatcherAccess::Of(owner);
            if (!b)
            {
                b = new CanvasBatches();
                b->serial = S().serial++;
            }
            b->owner = PPtr<Canvas>(&owner);
            b->root = PPtr<Canvas>(w.root);
            b->vertices.Clear();
            b->indices.Clear();
            b->list.Clear();
            b->encounter = w.encounter++;
            b->toWorld = w.root->transform()->localToWorldMatrix();
            b->mode = CanvasBatcherAccess::OwnMode(*w.root);
            b->camera = PPtr<Camera>(CanvasBatcherAccess::OwnCamera(*w.root));
            if (b->mode == RenderMode::ScreenSpaceCamera && !b->camera.Get())
                b->mode = RenderMode::ScreenSpaceOverlay;
            b->layer = owner.gameObject() ? i32(owner.gameObject()->layer()) : 0;
            w.out->Push(b);
            return *b;
        }

        bool Has(AdditionalCanvasShaderChannels set, AdditionalCanvasShaderChannels c) { return (i32(set) & i32(c)) != 0; }

        // The renderer's vertices in the root's space, tinted and faded, the channels the canvas does not keep zero.
        // False when nothing of it shows (culled, transparent, outside its clip).
        bool Transformed(const Walk& w, const CanvasRenderer& r, f32 groupAlpha, bool pixelPerfect, Array<CanvasVertex>& out)
        {
            Span<const UIVertex> vertices = r.vertices();
            if (r.cull() || vertices.Empty() || r.indices().Empty())
                return false;
            Color tint = r.GetColor();
            f32 alpha = tint.a * groupAlpha;
            if (r.cullTransparentMesh() && alpha <= 0.0f)
                return false;
            Matrix4x4 m = w.rootFromWorld * r.transform()->localToWorldMatrix();
            bool keepUv1 = Has(w.channels, AdditionalCanvasShaderChannels::TexCoord1), keepUv2 = Has(w.channels, AdditionalCanvasShaderChannels::TexCoord2);
            bool keepUv3 = Has(w.channels, AdditionalCanvasShaderChannels::TexCoord3), keepNormal = Has(w.channels, AdditionalCanvasShaderChannels::Normal);
            bool keepTangent = Has(w.channels, AdditionalCanvasShaderChannels::Tangent);
            bool snap = pixelPerfect && w.screenSpace && w.pixelScale > 0;
            out.Resize(vertices.Size());
            f32 x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            for (usize i = 0; i < vertices.Size(); i++)
            {
                const UIVertex& v = vertices[i];
                CanvasVertex& o = out[i];
                Vector3 p = m.MultiplyPoint3x4(v.position);
                if (snap)
                {
                    p.x = (std::round(p.x * w.pixelScale + w.pixelOrigin.x) - w.pixelOrigin.x) / w.pixelScale;
                    p.y = (std::round(p.y * w.pixelScale + w.pixelOrigin.y) - w.pixelOrigin.y) / w.pixelScale;
                }
                o.position = p;
                o.normal = keepNormal ? m.MultiplyVector(v.normal).normalized() : Vector3(0, 0, 0);
                if (keepTangent)
                {
                    Vector3 t = m.MultiplyVector(Vector3(v.tangent.x, v.tangent.y, v.tangent.z)).normalized();
                    o.tangent = Vector4(t.x, t.y, t.z, v.tangent.w);
                }
                else
                    o.tangent = Vector4(0, 0, 0, 0);
                Color c = v.color;
                o.color = Vector4(c.r * tint.r, c.g * tint.g, c.b * tint.b, c.a * alpha);
                o.uv[0] = v.uv0;
                o.uv[1] = keepUv1 ? v.uv1 : Vector4(0, 0, 0, 0);
                o.uv[2] = keepUv2 ? v.uv2 : Vector4(0, 0, 0, 0);
                o.uv[3] = keepUv3 ? v.uv3 : Vector4(0, 0, 0, 0);
                if (i == 0)
                    x0 = x1 = p.x, y0 = y1 = p.y;
                x0 = p.x < x0 ? p.x : x0, x1 = p.x > x1 ? p.x : x1;
                y0 = p.y < y0 ? p.y : y0, y1 = p.y > y1 ? p.y : y1;
            }
            if (r.hasRectClipping())
            {
                Rect c = r.clipRect();
                if (x1 < c.xMin() || x0 > c.xMax() || y1 < c.yMin() || y0 > c.yMax())
                    return false;
            }
            return true;
        }

        bool SameBatch(const CanvasBatch& b, Material* m, const CanvasRenderer& r, const Array<CanvasBatcherAccess::Secondary>& secondaries)
        {
            if (b.material.Get() != m || b.texture.Get() != r.texture() || b.alpha.Get() != r.alphaTexture() || b.clip != r.hasRectClipping())
                return false;
            if (b.clip)
            {
                Rect c = r.clipRect();
                if (c.x != b.clipRect.x || c.y != b.clipRect.y || c.width != b.clipRect.width || c.height != b.clipRect.height || !(b.softness == r.clippingSoftness()))
                    return false;
            }
            if (b.secondaries.Size() != secondaries.Size())
                return false;
            for (usize i = 0; i < secondaries.Size(); i++)
                if (!(b.secondaries[i] == secondaries[i]))
                    return false;
            return true;
        }

        // The renderer's geometry with each of its materials (or pop materials): material i draws submesh i (the last
        // one past the count).
        void Emit(CanvasBatches& into, const Walk& w, const CanvasRenderer& r, f32 groupAlpha, bool pixelPerfect, bool pop)
        {
            Span<const PPtr<Material>> materials = CanvasBatcherAccess::Materials(r, pop);
            if (materials.Empty())
                return;
            State& s = S();
            if (!Transformed(w, r, groupAlpha, pixelPerfect, s.scratch))
                return;
            CanvasBatcherAccess::Secondaries(r, s.secondaries);
            Span<const i32> indices = r.indices();
            Span<const u32> starts = r.submeshStarts();
            u32 subCount = starts.Empty() ? 1u : u32(starts.Size());
            u32 base = u32(into.vertices.Size());
            bool appended = false;
            for (u32 k = 0; k < materials.Size(); k++)
            {
                Material* m = materials[k].Get();
                if (!m)
                    m = Canvas::GetDefaultCanvasMaterial();
                if (!m)
                    continue;
                u32 sub = k < subCount ? k : subCount - 1;
                u32 from = starts.Empty() ? 0u : starts[sub];
                u32 to = starts.Empty() || sub + 1 >= starts.Size() ? u32(indices.Size()) : starts[sub + 1];
                if (from >= to)
                    continue;
                if (!appended)
                {
                    for (const CanvasVertex& v : s.scratch)
                        into.vertices.Push(v);
                    appended = true;
                }
                if (into.list.Empty() || !SameBatch(into.list[into.list.Size() - 1], m, r, s.secondaries))
                {
                    CanvasBatch b;
                    b.material = PPtr<Material>(m);
                    b.texture = PPtr<Texture>(r.texture());
                    b.alpha = PPtr<Texture>(r.alphaTexture());
                    b.secondaries = s.secondaries;
                    b.clip = r.hasRectClipping();
                    b.clipRect = r.clipRect();
                    b.softness = r.clippingSoftness();
                    b.firstIndex = u32(into.indices.Size());
                    into.list.Push(b);
                }
                for (u32 i = from; i < to; i++)
                    into.indices.Push(base + u32(indices[i]));
                into.list[into.list.Size() - 1].indexCount = u32(into.indices.Size()) - into.list[into.list.Size() - 1].firstIndex;
            }
        }

        void WalkNode(Transform* t, Walk& w, CanvasBatches* into, Canvas* own, i32& relative, f32 groupAlpha, bool pixelPerfect)
        {
            GameObject* g = t->gameObject();
            if (!g || !g->activeInHierarchy())
                return;
            i32 ownRelative = 0;
            i32* rel = &relative;
            if (Canvas* c = g->GetComponent<Canvas>(); c && c != w.root)
            {
                // A disabled canvas hides what is under it; an enabled one starts its own relative order (and draws
                // apart with its own sorting).
                if (!c->enabled())
                    return;
                own = c;
                rel = &ownRelative;
                if (c->overrideSorting())
                    into = &Start(*c, w);
                if (c->overridePixelPerfect())
                    pixelPerfect = c->pixelPerfect();
            }
            if (CanvasGroup* group = g->GetComponent<CanvasGroup>(); group && group->enabled())
                groupAlpha = group->ignoreParentGroups() ? group->alpha() : groupAlpha * group->alpha();
            CanvasRenderer* r = g->GetComponent<CanvasRenderer>();
            if (r)
            {
                CanvasBatcherAccess::Place(*r, w.absolute++, (*rel)++, groupAlpha, t->localToWorldMatrix());
                Emit(*into, w, *r, groupAlpha, pixelPerfect, false);
            }
            for (u32 i = 0; i < t->childCount(); i++)
                WalkNode(t->GetChild(i), w, into, own, *rel, groupAlpha, pixelPerfect);
            if (r && CanvasBatcherAccess::HasPop(*r))
                Emit(*into, w, *r, groupAlpha, pixelPerfect, true);
        }

        // ---------------------------------------------------------------------------------------------- upload

        void Upload(CanvasBatches& b)
        {
            Mesh* m = b.mesh.Get();
            if (!m)
            {
                m = Mesh::Create();
                m->SetName("Canvas");
                m->hideFlags = HideFlags::HideAndDontSave;
                m->MarkDynamic();
                b.mesh = PPtr<Mesh>(m);
                b.vertexCapacity = b.indexCapacity = 0;
            }
            static const VertexAttributeDescriptor kLayout[] = {
                {VertexAttribute::Position, VertexAttributeFormat::Float32, 3},  {VertexAttribute::Normal, VertexAttributeFormat::Float32, 3},
                {VertexAttribute::Tangent, VertexAttributeFormat::Float32, 4},   {VertexAttribute::Color, VertexAttributeFormat::Float32, 4},
                {VertexAttribute::TexCoord0, VertexAttributeFormat::Float32, 4}, {VertexAttribute::TexCoord1, VertexAttributeFormat::Float32, 4},
                {VertexAttribute::TexCoord2, VertexAttributeFormat::Float32, 4}, {VertexAttribute::TexCoord3, VertexAttributeFormat::Float32, 4},
            };
            u32 n = u32(b.vertices.Size()), k = u32(b.indices.Size());
            if (n > b.vertexCapacity || b.vertexCapacity == 0)
            {
                b.vertexCapacity = n > 256 ? n + n / 2 : 256;
                m->SetVertexBufferParams(i32(b.vertexCapacity), Span<const VertexAttributeDescriptor>(kLayout, 8));
            }
            if (k > b.indexCapacity || b.indexCapacity == 0)
            {
                b.indexCapacity = k > 768 ? k + k / 2 : 768;
                m->SetIndexBufferParams(i32(b.indexCapacity), IndexFormat::UInt32);
            }
            constexpr MeshUpdateFlags kFlags = MeshUpdateFlags::DontValidateIndices | MeshUpdateFlags::DontRecalculateBounds;
            m->SetVertexBufferData(Span<const CanvasVertex>(b.vertices.Data(), n), 0, 0, i32(n), 0, kFlags);
            m->SetIndexBufferData(Span<const u32>(b.indices.Data(), k), 0, 0, i32(k), kFlags);
            Vector3 lo(0, 0, 0), hi(0, 0, 0);
            for (u32 i = 0; i < n; i++)
            {
                Vector3 p = b.vertices[i].position;
                if (i == 0)
                    lo = hi = p;
                lo = Vector3::Min(lo, p), hi = Vector3::Max(hi, p);
            }
            // One bounds for every batch: the cameras keep their order (same distance).
            Bounds bounds((lo + hi) * 0.5f, hi - lo);
            b.subs.Clear();
            for (const CanvasBatch& batch : b.list)
            {
                SubMeshDescriptor d;
                d.indexStart = i32(batch.firstIndex);
                d.indexCount = i32(batch.indexCount);
                d.topology = MeshTopology::Triangles;
                d.bounds = bounds;
                d.vertexCount = i32(n);
                b.subs.Push(d);
            }
            if (b.subs.Empty())
                b.subs.Push(SubMeshDescriptor());
            m->SetSubMeshes(Span<const SubMeshDescriptor>(b.subs.Data(), b.subs.Size()), kFlags);
            m->SetBounds(bounds);
            // The batches' values over their materials.
            bool overlay = b.mode == RenderMode::ScreenSpaceOverlay;
            f32 linear = !overlay && Shader::GetGlobalFloat("_HelixLinearOutput") > 0.5f ? 1.0f : 0.0f;
            while (b.blocks.Size() < b.list.Size())
                b.blocks.Push(std::make_unique<MaterialPropertyBlock>());
            for (usize i = 0; i < b.list.Size(); i++)
            {
                const CanvasBatch& batch = b.list[i];
                MaterialPropertyBlock& p = *b.blocks[i];
                p.Clear();
                Texture* texture = batch.texture.Get();
                if (texture)
                    p.SetTexture("_MainTex", texture);
                if (Texture* a = batch.alpha.Get())
                    p.SetTexture("_AlphaTex", a);
                for (const CanvasBatcherAccess::Secondary& s : batch.secondaries)
                    if (Texture2D* t = s.texture.Get(); t && !s.name.Empty())
                        p.SetTexture(StringView(s.name), t);
                constexpr f32 kFar = 2e10f;
                Rect c = batch.clipRect;
                p.SetVector("_ClipRect", batch.clip ? Vector4(c.xMin(), c.yMin(), c.xMax(), c.yMax()) : Vector4(-kFar, -kFar, kFar, kFar));
                p.SetFloat("_UIMaskSoftnessX", batch.clip ? batch.softness.x : 0.0f);
                p.SetFloat("_UIMaskSoftnessY", batch.clip ? batch.softness.y : 0.0f);
                bool alphaOnly = texture && texture->Is<Texture2D>() && static_cast<Texture2D*>(texture)->format() == TextureFormat::Alpha8;
                p.SetVector("_TextureSampleAdd", alphaOnly ? Vector4(1, 1, 1, 0) : Vector4(0, 0, 0, 0));
                p.SetFloat("_UIVertexToLinear", linear);
                p.SetFloat("_UISampleToGamma", linear < 0.5f && texture && texture->isDataSRGB() ? 1.0f : 0.0f);
            }
        }

        // ---------------------------------------------------------------------------------------------- drawing

        void DrawOverlay(void*, CommandBuffer& cmd)
        {
            State& s = S();
            const RenderTarget& screen = Graphics::Target();
            bool any = false;
            for (CanvasBatches* b : s.drawing)
                any = any || (b->mode == RenderMode::ScreenSpaceOverlay && !b->list.Empty());
            if (!any || screen.width == 0 || screen.height == 0)
                return;
            cmd.SetViewport(Rect(0, 0, f32(screen.width), f32(screen.height)));
            // Their own depth and stencil (masks), the scene's gone; the pixels are the screen's, y up.
            cmd.ClearRenderTarget(RTClearFlags::DepthStencil, Color(0, 0, 0, 0), 1.0f, 0);
            cmd.SetViewProjectionMatrices(Matrix4x4::identity, Matrix4x4::Ortho(0, f32(screen.width), 0, f32(screen.height), -100000, 100000));
            for (CanvasBatches* b : s.drawing)
            {
                Mesh* mesh = b->mesh.Get();
                if (b->mode != RenderMode::ScreenSpaceOverlay || !mesh)
                    continue;
                for (usize i = 0; i < b->list.Size(); i++)
                    if (Material* m = b->list[i].material.Get())
                        cmd.DrawMesh(mesh, b->toWorld, m, i32(i), -1, b->blocks[i].get());
            }
        }

        // Overlay canvases first by sorting order (the earlier met under the later among equal ones), then the
        // others as met.
        void Sort(Array<CanvasBatches*>& list)
        {
            auto before = [](const CanvasBatches* a, const CanvasBatches* b) {
                bool ao = a->mode == RenderMode::ScreenSpaceOverlay, bo = b->mode == RenderMode::ScreenSpaceOverlay;
                if (ao != bo)
                    return ao;
                Canvas* ac = a->owner.Get();
                Canvas* bc = b->owner.Get();
                i32 aOrder = ac ? ac->sortingOrder() : 0, bOrder = bc ? bc->sortingOrder() : 0;
                if (ao && aOrder != bOrder)
                    return aOrder < bOrder;
                return a->encounter < b->encounter;
            };
            for (usize i = 1; i < list.Size(); i++)
                for (usize j = i; j > 0 && before(list[j], list[j - 1]); j--)
                    std::swap(list[j - 1], list[j]);
        }
    }

    namespace canvas::detail
    {
        void Forget(Canvas& canvas)
        {
            CanvasBatches*& b = CanvasBatcherAccess::Of(canvas);
            if (!b)
                return;
            State& s = S();
            for (usize i = 0; i < s.drawing.Size(); i++)
                if (s.drawing[i] == b)
                {
                    s.drawing.RemoveAt(i);
                    break;
                }
            delete b;
            b = nullptr;
        }

        void HierarchyChanged(Canvas& canvas)
        {
            if (GameObject* g = canvas.gameObject())
                SendHierarchyMessage(g, ScriptMessage::OnCanvasHierarchyChanged, true);
        }
    }

    namespace canvas
    {
        void Batch(bool raiseEvents)
        {
            if (raiseEvents)
            {
                UISystemProfilerApi::BeginSample(UISystemProfilerApi::SampleType::Layout);
                Canvas::preWillRenderCanvasesInternal().Invoke();
                Canvas::willRenderCanvasesInternal().Invoke();
                UISystemProfilerApi::EndSample(UISystemProfilerApi::SampleType::Layout);
            }
            UISystemProfilerApi::BeginSample(UISystemProfilerApi::SampleType::Render);
            State& s = S();
            ForEachObjectOfClass(CanvasRenderer::StaticClass(), [](Object* o) {
                CanvasBatcherAccess::Reset(CanvasBatcherAccess::R(o));
                return true;
            });
            Array<Canvas*> roots;
            ForEachObjectOfClass(Canvas::StaticClass(), [&](Object* o) {
                auto* c = static_cast<Canvas*>(o);
                if (c->isActiveAndEnabled() && c->isRootCanvas())
                    roots.Push(c);
                return true;
            });
            // Made earlier first among equal sorting (the pool's order is not the making order).
            for (usize i = 1; i < roots.Size(); i++)
                for (usize j = i; j > 0 && roots[j]->GetEntityId().value < roots[j - 1]->GetEntityId().value; j--)
                    std::swap(roots[j - 1], roots[j]);
            Array<CanvasBatches*> drawing;
            Walk w;
            w.out = &drawing;
            for (Canvas* root : roots)
            {
                DriveRoot(*root);
                w.root = root;
                w.rootFromWorld = root->transform()->worldToLocalMatrix();
                w.channels = root->additionalShaderChannels();
                w.absolute = 0;
                RenderMode mode = root->renderMode();
                Camera* camera = root->worldCamera();
                w.screenSpace = mode == RenderMode::ScreenSpaceOverlay || mode == RenderMode::ScreenSpaceCamera;
                w.pixelScale = root->scaleFactor();
                Rect pixels = root->pixelRect();
                (void)camera;
                w.pixelOrigin = Vector2(pixels.x + pixels.width * 0.5f, pixels.y + pixels.height * 0.5f);
                CanvasBatches& b = Start(*root, w);
                i32 relative = 0;
                WalkNode(root->transform(), w, &b, root, relative, 1.0f, root->pixelPerfect());
            }
            // Canvases that no longer draw drop their meshes.
            Array<Canvas*> gone;
            for (CanvasBatches* old : s.drawing)
            {
                bool kept = false;
                for (CanvasBatches* b : drawing)
                    kept = kept || b == old;
                if (!kept)
                    if (Canvas* c = old->owner.Get())
                        gone.Push(c);
            }
            for (Canvas* c : gone)
                canvas::detail::Forget(*c);
            Sort(drawing);
            for (usize i = 0; i < drawing.Size(); i++)
            {
                if (Canvas* c = drawing[i]->owner.Get())
                    CanvasBatcherAccess::SetOrder(*c, i32(i));
                Upload(*drawing[i]);
            }
            s.drawing = Move(drawing);
            bool overlay = false;
            for (CanvasBatches* b : s.drawing)
                overlay = overlay || b->mode == RenderMode::ScreenSpaceOverlay;
            if (overlay && !s.drawer)
                s.drawer = Graphics::screenOverlayRendering().Add(&DrawOverlay);
            UISystemProfilerApi::EndSample(UISystemProfilerApi::SampleType::Render);
        }

        void QueueDraws()
        {
            for (CanvasBatches* b : S().drawing)
            {
                Mesh* mesh = b->mesh.Get();
                if (b->mode == RenderMode::ScreenSpaceOverlay || !mesh)
                    continue;
                RenderParams params;
                params.camera = b->mode == RenderMode::ScreenSpaceCamera ? b->camera.Get() : nullptr;
                params.layer = b->layer;
                for (usize i = 0; i < b->list.Size(); i++)
                    if (Material* m = b->list[i].material.Get())
                    {
                        params.material = m;
                        params.matProps = b->blocks[i].get();
                        Graphics::RenderMesh(params, mesh, i32(i), b->toWorld);
                    }
            }
        }

        u32 CanvasCount() { return u32(S().drawing.Size()); }

        u32 BatchCount(const Canvas& canvas)
        {
            CanvasBatches* b = CanvasBatcherAccess::Of(const_cast<Canvas&>(canvas));
            return b ? u32(b->list.Size()) : 0u;
        }
    }
}
