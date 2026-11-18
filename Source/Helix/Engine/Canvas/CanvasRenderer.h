#pragma once

#include <Helix/Engine/Event/Event.h>
#include <Helix/Engine/GameObject/GameObject.h>
#include <Helix/Math/Color.h>
#include <Helix/Math/Geometry.h>
#include <Helix/Math/Matrix4x4.h>

// CanvasRenderer: what a canvas element draws (a mesh in its own space, a color, materials, a texture, a clip
// rectangle). The canvas it is under batches the renderers of its hierarchy into its mesh each frame, in hierarchy
// order. UIVertex is the vertex the canvas UI's graphics build their meshes of.
namespace helix::engine
{
    class Material;
    class Mesh;
    class Texture;
    class Texture2D;

    HX_STRUCT(Namespace = "HelixEngine")
    struct UIVertex
    {
        HX_FIELD() Vector3 position = Vector3(0, 0, 0);
        HX_FIELD() Vector3 normal = Vector3(0, 0, -1);
        HX_FIELD() Vector4 tangent = Vector4(1, 0, 0, -1);
        HX_FIELD() Color32 color = Color32(255, 255, 255, 255);
        HX_FIELD() Vector4 uv0 = Vector4(0, 0, 0, 0);
        HX_FIELD() Vector4 uv1 = Vector4(0, 0, 0, 0);
        HX_FIELD() Vector4 uv2 = Vector4(0, 0, 0, 0);
        HX_FIELD() Vector4 uv3 = Vector4(0, 0, 0, 0);

        // A white vertex at the origin facing -z (the reference's simpleVert).
        static UIVertex Simple() { return UIVertex(); }
    };

    HX_CLASS(Namespace = "HelixEngine", Sealed)
    class CanvasRenderer final : public Component
    {
        HX_ENGINE_CLASS(CanvasRenderer, Component, ClassId::CanvasRenderer, ClassDisallowMultiple)

    public:
        CanvasRenderer() = default;
        CanvasRenderer(const CanvasRenderer& other);

        // ---------------------------------------------------------------------------------------------- the geometry
        // The mesh's vertices and triangles taken in (the mesh may change after).
        HX_FUNCTION() void SetMesh(Mesh* mesh);
        HX_FUNCTION() Mesh* GetMesh() const;
        // Quads of four vertices each (the first size vertices).
        HX_FUNCTION() void SetVertices(Span<const UIVertex> vertices, i32 size);
        // No geometry, no materials, no textures.
        HX_FUNCTION() void Clear();

        // ---------------------------------------------------------------------------------------------- color
        HX_FUNCTION() void SetColor(Color color) { color_ = color; }
        HX_FUNCTION() Color GetColor() const { return color_; }
        HX_FUNCTION() f32 GetAlpha() const { return color_.a; }
        HX_FUNCTION() void SetAlpha(f32 alpha) { color_.a = alpha; }
        // The alpha with the CanvasGroups above taken in (the last batching's).
        HX_FUNCTION() f32 GetInheritedAlpha() const { return inheritedAlpha_ * color_.a; }

        // ---------------------------------------------------------------------------------------------- materials
        HX_PROPERTY() i32 materialCount() const { return i32(materials_.Size()); }
        void SetMaterialCount(i32 value);
        HX_PROPERTY() i32 popMaterialCount() const { return i32(popMaterials_.Size()); }
        void SetPopMaterialCount(i32 value);
        HX_FUNCTION() void SetMaterial(Material* material, i32 index);
        // One material and the texture (materialCount becomes 1).
        HX_FUNCTION() void SetMaterial(Material* material, Texture* texture);
        HX_FUNCTION() Material* GetMaterial(i32 index) const;
        HX_FUNCTION() Material* GetMaterial() const { return GetMaterial(0); }
        // Drawn after the children (a mask's unmasking).
        HX_FUNCTION() void SetPopMaterial(Material* material, i32 index);
        HX_FUNCTION() Material* GetPopMaterial(i32 index) const;
        HX_PROPERTY() bool hasPopInstruction() const { return hasPop_; }
        void SetHasPopInstruction(bool value) { hasPop_ = value; }

        // ---------------------------------------------------------------------------------------------- textures
        HX_FUNCTION() void SetTexture(Texture* texture);
        HX_FUNCTION() void SetAlphaTexture(Texture* texture);
        Texture* texture() const;
        Texture* alphaTexture() const;
        HX_FUNCTION() i32 GetSecondaryTextureCount() const { return i32(secondary_.Size()); }
        HX_FUNCTION() void SetSecondaryTextureCount(i32 size);
        HX_FUNCTION() StringView GetSecondaryTextureName(i32 index) const;
        HX_FUNCTION() Texture2D* GetSecondaryTexture(i32 index) const;
        HX_FUNCTION() void SetSecondaryTexture(i32 index, StringView name, Texture2D* texture);

        // ---------------------------------------------------------------------------------------------- clipping
        // A rectangle in the root canvas's space outside of which nothing is drawn.
        HX_FUNCTION() void EnableRectClipping(Rect rect);
        HX_FUNCTION() void DisableRectClipping() { hasClip_ = false; }
        HX_PROPERTY(ReadOnly) bool hasRectClipping() const { return hasClip_; }
        Rect clipRect() const { return clip_; }
        // The clip's fade, in canvas units on each axis.
        HX_PROPERTY() Vector2 clippingSoftness() const { return softness_; }
        void SetClippingSoftness(Vector2 value) { softness_ = value; }
        // Not drawn (still in the depth order).
        HX_PROPERTY() bool cull() const { return cull_; }
        void SetCull(bool value) { cull_ = value; }
        // Not drawn while its alpha (with the groups') is zero.
        HX_PROPERTY() bool cullTransparentMesh() const { return cullTransparent_; }
        void SetCullTransparentMesh(bool value) { cullTransparent_ = value; }

        // ---------------------------------------------------------------------------------------------- the batching's
        // Its place in the drawing order of its root canvas (-1: not under a canvas that draws).
        HX_PROPERTY(ReadOnly) i32 absoluteDepth() const { return absoluteDepth_; }
        // Its place in the order of its own canvas.
        HX_PROPERTY(ReadOnly) i32 relativeDepth() const { return relativeDepth_; }
        // Moved since the last batching.
        HX_PROPERTY(ReadOnly) bool hasMoved() const { return hasMoved_; }

        // Raised when the graphics must give their geometry again (the canvas UI's graphics listen).
        HX_EVENT(Internal, ApiName = "onRequestRebuild") static Event<>& onRequestRebuildInternal();

        // ---------------------------------------------------------------------------------------------- streams
        // Triangles of vertices into separate streams and indices (shared vertices kept once each), and back.
        static void SplitUIVertexStreams(Span<const UIVertex> vertices, Array<Vector3>& positions, Array<Color32>& colors, Array<Vector4>& uv0S,
                                         Array<Vector4>& uv1S, Array<Vector4>& uv2S, Array<Vector4>& uv3S, Array<Vector3>& normals,
                                         Array<Vector4>& tangents, Array<i32>& indices);
        static void CreateUIVertexStream(Array<UIVertex>& vertices, Span<const Vector3> positions, Span<const Color32> colors, Span<const Vector4> uv0S,
                                         Span<const Vector4> uv1S, Span<const Vector4> uv2S, Span<const Vector4> uv3S, Span<const Vector3> normals,
                                         Span<const Vector4> tangents, Span<const i32> indices);
        static void AddUIVertexStream(Array<UIVertex>& vertices, Span<const Vector3> positions, Span<const Color32> colors, Span<const Vector4> uv0S,
                                      Span<const Vector4> uv1S, Span<const Vector4> uv2S, Span<const Vector4> uv3S, Span<const Vector3> normals,
                                      Span<const Vector4> tangents);

        // The geometry in its own space: vertices and triangles per submesh.
        Span<const UIVertex> vertices() const { return Span<const UIVertex>(vertices_.Data(), vertices_.Size()); }
        Span<const i32> indices() const { return Span<const i32>(indices_.Data(), indices_.Size()); }
        // Where each submesh's indices start (submesh i ends where i + 1 starts).
        Span<const u32> submeshStarts() const { return Span<const u32>(submeshStarts_.Data(), submeshStarts_.Size()); }

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("m_CullTransparentMesh", cullTransparent_);
        }

    private:
        friend struct CanvasBatcherAccess;

        struct Secondary
        {
            String name;
            PPtr<Texture2D> texture;
        };

        Array<UIVertex> vertices_;
        Array<i32> indices_;
        Array<u32> submeshStarts_;
        PPtr<Mesh> mesh_;
        Color color_ = Color(1, 1, 1, 1);
        Array<PPtr<Material>> materials_, popMaterials_;
        PPtr<Texture> texture_, alphaTexture_;
        Array<Secondary> secondary_;
        Rect clip_;
        Vector2 softness_ = Vector2(0, 0);
        bool hasClip_ = false, hasPop_ = false, cull_ = false, cullTransparent_ = false;
        // The batching's.
        f32 inheritedAlpha_ = 1;
        i32 absoluteDepth_ = -1, relativeDepth_ = -1;
        bool hasMoved_ = true, placed_ = false;
        Matrix4x4 lastMatrix_ = Matrix4x4::identity;
    };
}
