#include <Helix/Engine/Canvas/CanvasRenderer.h>

#include <Helix/Engine/Material/Material.h>
#include <Helix/Engine/Mesh/Mesh.h>
#include <Helix/Engine/Texture/Texture.h>

namespace helix::engine
{
    CanvasRenderer::CanvasRenderer(const CanvasRenderer& other)
        : Component(other), vertices_(other.vertices_), indices_(other.indices_), submeshStarts_(other.submeshStarts_), mesh_(other.mesh_),
          color_(other.color_), materials_(other.materials_), popMaterials_(other.popMaterials_), texture_(other.texture_),
          alphaTexture_(other.alphaTexture_), secondary_(other.secondary_), clip_(other.clip_), softness_(other.softness_), hasClip_(other.hasClip_),
          hasPop_(other.hasPop_), cull_(other.cull_), cullTransparent_(other.cullTransparent_)
    {
    }

    Mesh* CanvasRenderer::GetMesh() const { return mesh_.Get(); }
    Texture* CanvasRenderer::texture() const { return texture_.Get(); }
    Texture* CanvasRenderer::alphaTexture() const { return alphaTexture_.Get(); }
    void CanvasRenderer::SetTexture(Texture* texture) { texture_ = PPtr<Texture>(texture); }
    void CanvasRenderer::SetAlphaTexture(Texture* texture) { alphaTexture_ = PPtr<Texture>(texture); }

    void CanvasRenderer::SetMesh(Mesh* mesh)
    {
        mesh_ = PPtr<Mesh>(mesh);
        vertices_.Clear();
        indices_.Clear();
        submeshStarts_.Clear();
        if (!mesh)
            return;
        Array<Vector3> positions, normals;
        Array<Vector4> tangents, uv[4];
        Array<Color32> colors;
        mesh->GetVertices(positions);
        mesh->GetNormals(normals);
        mesh->GetTangents(tangents);
        mesh->GetColors(colors);
        for (i32 c = 0; c < 4; c++)
            mesh->GetUVs(c, uv[c]);
        u32 n = u32(positions.Size());
        vertices_.Resize(n);
        for (u32 i = 0; i < n; i++)
        {
            UIVertex& v = vertices_[i];
            v = UIVertex();
            v.position = positions[i];
            if (i < normals.Size())
                v.normal = normals[i];
            if (i < tangents.Size())
                v.tangent = tangents[i];
            if (i < colors.Size())
                v.color = colors[i];
            Vector4* channels[4] = {&v.uv0, &v.uv1, &v.uv2, &v.uv3};
            for (i32 c = 0; c < 4; c++)
                if (i < uv[c].Size())
                    *channels[c] = uv[c][i];
        }
        Array<i32> triangles;
        for (i32 s = 0; s < mesh->subMeshCount(); s++)
        {
            submeshStarts_.Push(u32(indices_.Size()));
            mesh->GetTriangles(triangles, s);
            for (i32 t : triangles)
                if (t >= 0 && u32(t) < n)
                    indices_.Push(t);
        }
    }

    void CanvasRenderer::SetVertices(Span<const UIVertex> vertices, i32 size)
    {
        mesh_ = PPtr<Mesh>();
        u32 n = size < 0 ? 0u : u32(size);
        n = n < vertices.Size() ? n : u32(vertices.Size());
        n -= n % 4;
        vertices_.Resize(n);
        indices_.Clear();
        submeshStarts_.Clear();
        submeshStarts_.Push(0);
        for (u32 i = 0; i < n; i++)
            vertices_[i] = vertices[i];
        for (u32 q = 0; q < n; q += 4)
            for (u32 k : {0u, 1u, 2u, 2u, 3u, 0u})
                indices_.Push(i32(q + k));
    }

    void CanvasRenderer::Clear()
    {
        vertices_.Clear();
        indices_.Clear();
        submeshStarts_.Clear();
        mesh_ = PPtr<Mesh>();
        materials_.Clear();
        popMaterials_.Clear();
        texture_ = alphaTexture_ = PPtr<Texture>();
        secondary_.Clear();
    }

    void CanvasRenderer::SetMaterialCount(i32 value) { materials_.Resize(value > 0 ? u32(value) : 0u); }
    void CanvasRenderer::SetPopMaterialCount(i32 value) { popMaterials_.Resize(value > 0 ? u32(value) : 0u); }

    void CanvasRenderer::SetMaterial(Material* material, i32 index)
    {
        if (index < 0)
            return;
        if (u32(index) >= materials_.Size())
            materials_.Resize(u32(index) + 1);
        materials_[u32(index)] = PPtr<Material>(material);
    }

    void CanvasRenderer::SetMaterial(Material* material, Texture* texture)
    {
        materials_.Resize(1);
        materials_[0] = PPtr<Material>(material);
        texture_ = PPtr<Texture>(texture);
    }

    Material* CanvasRenderer::GetMaterial(i32 index) const
    {
        return index >= 0 && u32(index) < materials_.Size() ? materials_[u32(index)].Get() : nullptr;
    }

    void CanvasRenderer::SetPopMaterial(Material* material, i32 index)
    {
        if (index < 0)
            return;
        if (u32(index) >= popMaterials_.Size())
            popMaterials_.Resize(u32(index) + 1);
        popMaterials_[u32(index)] = PPtr<Material>(material);
    }

    Material* CanvasRenderer::GetPopMaterial(i32 index) const
    {
        return index >= 0 && u32(index) < popMaterials_.Size() ? popMaterials_[u32(index)].Get() : nullptr;
    }

    void CanvasRenderer::SetSecondaryTextureCount(i32 size) { secondary_.Resize(size > 0 ? u32(size) : 0u); }

    StringView CanvasRenderer::GetSecondaryTextureName(i32 index) const
    {
        return index >= 0 && u32(index) < secondary_.Size() ? StringView(secondary_[u32(index)].name) : StringView();
    }

    Texture2D* CanvasRenderer::GetSecondaryTexture(i32 index) const
    {
        return index >= 0 && u32(index) < secondary_.Size() ? secondary_[u32(index)].texture.Get() : nullptr;
    }

    void CanvasRenderer::SetSecondaryTexture(i32 index, StringView name, Texture2D* texture)
    {
        if (index < 0)
            return;
        if (u32(index) >= secondary_.Size())
            secondary_.Resize(u32(index) + 1);
        secondary_[u32(index)].name = String(name);
        secondary_[u32(index)].texture = PPtr<Texture2D>(texture);
    }

    void CanvasRenderer::EnableRectClipping(Rect rect)
    {
        clip_ = rect;
        hasClip_ = true;
    }

    Event<>& CanvasRenderer::onRequestRebuildInternal()
    {
        static Event<>* e = new Event<>();
        return *e;
    }

    // ------------------------------------------------------------------------------------------------------ streams

    namespace
    {
        bool Same(const UIVertex& a, const UIVertex& b)
        {
            return a.position == b.position && a.normal == b.normal && a.tangent == b.tangent && a.color == b.color && a.uv0 == b.uv0 && a.uv1 == b.uv1 &&
                   a.uv2 == b.uv2 && a.uv3 == b.uv3;
        }
    }

    void CanvasRenderer::SplitUIVertexStreams(Span<const UIVertex> vertices, Array<Vector3>& positions, Array<Color32>& colors, Array<Vector4>& uv0S,
                                              Array<Vector4>& uv1S, Array<Vector4>& uv2S, Array<Vector4>& uv3S, Array<Vector3>& normals,
                                              Array<Vector4>& tangents, Array<i32>& indices)
    {
        positions.Clear(), colors.Clear(), uv0S.Clear(), uv1S.Clear(), uv2S.Clear(), uv3S.Clear(), normals.Clear(), tangents.Clear(), indices.Clear();
        // Each triangle's vertices: one kept per distinct vertex (the last few are compared, as quads share theirs).
        Array<UIVertex> kept;
        for (const UIVertex& v : vertices)
        {
            i32 found = -1;
            usize from = kept.Size() > 8 ? kept.Size() - 8 : 0;
            for (usize k = kept.Size(); k > from && found < 0; k--)
                if (Same(kept[k - 1], v))
                    found = i32(k - 1);
            if (found < 0)
            {
                found = i32(kept.Size());
                kept.Push(v);
                positions.Push(v.position), colors.Push(v.color), uv0S.Push(v.uv0), uv1S.Push(v.uv1), uv2S.Push(v.uv2), uv3S.Push(v.uv3);
                normals.Push(v.normal), tangents.Push(v.tangent);
            }
            indices.Push(found);
        }
    }

    void CanvasRenderer::CreateUIVertexStream(Array<UIVertex>& vertices, Span<const Vector3> positions, Span<const Color32> colors, Span<const Vector4> uv0S,
                                              Span<const Vector4> uv1S, Span<const Vector4> uv2S, Span<const Vector4> uv3S, Span<const Vector3> normals,
                                              Span<const Vector4> tangents, Span<const i32> indices)
    {
        vertices.Clear();
        for (i32 i : indices)
        {
            if (i < 0 || usize(i) >= positions.Size())
                continue;
            UIVertex v;
            usize k = usize(i);
            v.position = positions[k];
            if (k < colors.Size())
                v.color = colors[k];
            if (k < uv0S.Size())
                v.uv0 = uv0S[k];
            if (k < uv1S.Size())
                v.uv1 = uv1S[k];
            if (k < uv2S.Size())
                v.uv2 = uv2S[k];
            if (k < uv3S.Size())
                v.uv3 = uv3S[k];
            if (k < normals.Size())
                v.normal = normals[k];
            if (k < tangents.Size())
                v.tangent = tangents[k];
            vertices.Push(v);
        }
    }

    void CanvasRenderer::AddUIVertexStream(Array<UIVertex>& vertices, Span<const Vector3> positions, Span<const Color32> colors, Span<const Vector4> uv0S,
                                           Span<const Vector4> uv1S, Span<const Vector4> uv2S, Span<const Vector4> uv3S, Span<const Vector3> normals,
                                           Span<const Vector4> tangents)
    {
        for (usize k = 0; k < positions.Size(); k++)
        {
            UIVertex v;
            v.position = positions[k];
            if (k < colors.Size())
                v.color = colors[k];
            if (k < uv0S.Size())
                v.uv0 = uv0S[k];
            if (k < uv1S.Size())
                v.uv1 = uv1S[k];
            if (k < uv2S.Size())
                v.uv2 = uv2S[k];
            if (k < uv3S.Size())
                v.uv3 = uv3S[k];
            if (k < normals.Size())
                v.normal = normals[k];
            if (k < tangents.Size())
                v.tangent = tangents[k];
            vertices.Push(v);
        }
    }
}
