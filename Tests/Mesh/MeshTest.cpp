#include <Support/TestClasses.h>

#include <Helix/Engine/Mesh/Mesh.h>
#include <Helix/Engine/Texture/Texture.h>
#include <Helix/Testing/Test.h>

#include <cmath>

using namespace helix;
using namespace helix::engine;
using namespace helix::engine::testing;

namespace
{
    bool Near(f32 a, f32 b, f32 e = 1e-4f) { return std::fabs(a - b) < e; }
    bool Near(Vector3 a, Vector3 b, f32 e = 1e-4f) { return Near(a.x, b.x, e) && Near(a.y, b.y, e) && Near(a.z, b.z, e); }

    // A unit quad in the xy plane facing -z (clockwise seen from the front).
    Mesh* Quad()
    {
        Mesh* m = NewObject<Mesh>();
        const Vector3 v[] = {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}};
        const i32 t[] = {0, 1, 2, 0, 2, 3};
        m->SetVertices(Span<const Vector3>(v, 4));
        m->SetTriangles(Span<const i32>(t, 6));
        return m;
    }
}

HX_TEST(Mesh, SimpleArraysGoThroughTheLayout)
{
    Mesh* m = Quad();
    HX_CHECK(m->vertexCount() == 4 && m->subMeshCount() == 1 && m->vertexAttributeCount() == 1);
    HX_CHECK(m->GetVertexBufferStride(0) == 12 && m->GetVertexAttributeOffset(VertexAttribute::Position) == 0);
    HX_CHECK(Near(m->bounds().center, Vector3(0.5f, 0.5f, 0)) && Near(m->bounds().extents, Vector3(0.5f, 0.5f, 0)));
    const Vector2 uv[] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
    HX_CHECK(m->SetUVs(0, Span<const Vector2>(uv, 4)));
    m->RecalculateNormals();
    m->RecalculateTangents();
    // Enum order inside the stream: position, normal, tangent, uv0.
    HX_CHECK(m->GetVertexAttributeOffset(VertexAttribute::Normal) == 12 && m->GetVertexAttributeOffset(VertexAttribute::Tangent) == 24);
    HX_CHECK(m->GetVertexAttributeOffset(VertexAttribute::TexCoord0) == 40 && m->GetVertexBufferStride(0) == 48);
    Array<Vector3> n = m->normals();
    Array<Vector4> t = m->tangents();
    HX_CHECK(n.Size() == 4 && Near(n[2], Vector3(0, 0, -1)));
    HX_CHECK(t.Size() == 4 && Near(Vector3(t[0].x, t[0].y, t[0].z), Vector3(1, 0, 0)) && std::fabs(t[0].w) == 1.0f);
    Array<Vector2> back = m->uv(0);
    HX_CHECK(back.Size() == 4 && back[2] == Vector2(1, 1));
    // Too few values for the vertices: refused, the mesh keeps its normals.
    const Vector3 few[] = {{0, 0, 1}};
    HX_CHECK(!m->SetNormals(Span<const Vector3>(few, 1)) && m->normals().Size() == 4);
    // An empty list removes the attribute.
    HX_CHECK(m->SetNormals(Span<const Vector3>()) && !m->HasVertexAttribute(VertexAttribute::Normal) && m->GetVertexBufferStride(0) == 36);
    HX_CHECK(m->vertices()[2] == Vector3(1, 1, 0));
    Cleanup();
}

HX_TEST(Mesh, VerticesResizeAndIndicesAreChecked)
{
    Mesh* m = Quad();
    const Color32 c[] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 255, 0}};
    HX_CHECK(m->SetColors(Span<const Color32>(c, 4)));
    HX_CHECK(m->GetVertexAttributeFormat(VertexAttribute::Color) == VertexAttributeFormat::UNorm8 && m->GetVertexAttributeDimension(VertexAttribute::Color) == 4);
    // Fewer vertices than the triangles use: refused.
    const Vector3 two[] = {{0, 0, 0}, {1, 0, 0}};
    HX_CHECK(!m->SetVertices(Span<const Vector3>(two, 2)) && m->vertexCount() == 4);
    // More vertices: the other attributes grow with zeros.
    const Vector3 six[] = {{0, 0, 0}, {0, 1, 0}, {1, 1, 0}, {1, 0, 0}, {2, 0, 0}, {2, 2, 2}};
    HX_CHECK(m->SetVertices(Span<const Vector3>(six, 6)) && m->vertexCount() == 6);
    Array<Color32> c32 = m->colors32();
    HX_CHECK(c32.Size() == 6 && c32[1].g == 255 && c32[5].a == 0);
    HX_CHECK(Near(m->bounds().max(), Vector3(1, 1, 0)));
    const i32 bad[] = {0, 1, 9};
    HX_CHECK(!m->SetTriangles(Span<const i32>(bad, 3)));
    // Two submeshes in one index buffer.
    m->SetSubMeshCount(2);
    const i32 second[] = {3, 4, 5};
    HX_CHECK(m->SetTriangles(Span<const i32>(second, 3), 1));
    HX_CHECK(m->GetIndexStart(1) == 6 && m->GetIndexCount(1) == 3 && m->triangles().Size() == 9 && m->IndexCount() == 9);
    HX_CHECK(Near(m->bounds().max(), Vector3(2, 2, 2)) && m->GetSubMesh(1).firstVertex == 3 && m->GetSubMesh(1).vertexCount == 3);
    // Replacing the first submesh moves the second.
    const i32 first[] = {0, 1, 2};
    HX_CHECK(m->SetTriangles(Span<const i32>(first, 3), 0));
    HX_CHECK(m->GetIndexStart(1) == 3 && m->GetIndices(1)[2] == 5);
    HX_CHECK(m->SetIndexFormat(IndexFormat::UInt32) && m->IndexData().Size() == 24 && m->GetIndices(1)[0] == 3);
    Cleanup();
}

HX_TEST(Mesh, AdvancedApiWritesTheBlocks)
{
    Mesh* m = NewObject<Mesh>();
    const VertexAttributeDescriptor layout[] = {
        {VertexAttribute::Position, VertexAttributeFormat::Float32, 3, 0},
        {VertexAttribute::Normal, VertexAttributeFormat::SNorm8, 4, 1},
        {VertexAttribute::TexCoord0, VertexAttributeFormat::Float16, 2, 1},
    };
    HX_CHECK(m->SetVertexBufferParams(3, Span<const VertexAttributeDescriptor>(layout, 3)));
    HX_CHECK(m->vertexBufferCount() == 2 && m->GetVertexBufferStride(0) == 12 && m->GetVertexBufferStride(1) == 8);
    const f32 positions[] = {0, 0, 0, 0, 1, 0, 1, 0, 0};
    HX_CHECK(m->SetVertexBufferData(Span<const f32>(positions, 9), 0, 0, 9, 0));
    const u16 indices[] = {0, 1, 2};
    HX_CHECK(m->SetIndexBufferParams(3, IndexFormat::UInt16));
    HX_CHECK(m->SetIndexBufferData(Span<const u16>(indices, 3), 0, 0, 3));
    m->SetSubMeshCount(1);
    HX_CHECK(m->SetSubMesh(0, SubMeshDescriptor(0, 3)));
    HX_CHECK(Near(m->bounds().max(), Vector3(1, 1, 0)) && m->GetSubMesh(0).vertexCount == 3);
    // Normals through the simple API land in the SNorm8 layout of stream 1.
    const Vector3 n[] = {{0, 0, -1}, {0, 0, -1}, {0, 0, -1}};
    HX_CHECK(m->SetNormals(Span<const Vector3>(n, 3)) && m->GetVertexAttributeFormat(VertexAttribute::Normal) == VertexAttributeFormat::SNorm8);
    HX_CHECK(m->VertexData(1)[2] == u8(i8(-127)) && Near(m->normals()[1], Vector3(0, 0, -1)));
    // Layouts that cannot be: duplicates, sizes off 4 bytes, streams past 3.
    const VertexAttributeDescriptor twice[] = {{VertexAttribute::Position}, {VertexAttribute::Position}};
    const VertexAttributeDescriptor odd[] = {{VertexAttribute::Color, VertexAttributeFormat::UNorm8, 3, 0}};
    const VertexAttributeDescriptor stream4[] = {{VertexAttribute::Position, VertexAttributeFormat::Float32, 3, 4}};
    HX_CHECK(!m->SetVertexBufferParams(3, Span<const VertexAttributeDescriptor>(twice, 2)));
    HX_CHECK(!m->SetVertexBufferParams(3, Span<const VertexAttributeDescriptor>(odd, 1)));
    HX_CHECK(!m->SetVertexBufferParams(3, Span<const VertexAttributeDescriptor>(stream4, 1)));
    // Out of range writes are refused.
    HX_CHECK(!m->SetVertexBufferData(Span<const f32>(positions, 9), 0, 1, 9, 0));
    HX_CHECK(!m->SetSubMesh(0, SubMeshDescriptor(1, 3)));
    // Not readable after UploadMeshData(true).
    u32 v = m->Version();
    m->UploadMeshData(true);
    HX_CHECK(!m->isReadable() && m->Version() != v && m->vertices().Empty());
    Cleanup();
}

HX_TEST(Texture2D, PixelsRowsAndMips)
{
    Texture2D* t = Texture2D::Create(4, 2, TextureFormat::RGBA32, true);
    HX_CHECK(t && t->width() == 4 && t->height() == 2 && t->mipmapCount() == 3 && t->format() == TextureFormat::RGBA32 && t->isDataSRGB());
    HX_CHECK(t->GetPixel(1, 1) == Color(0, 0, 0, 0));
    t->SetPixel(0, 0, Color(1, 0, 0, 1));
    t->SetPixel(3, 1, Color(0, 0, 1, 1));
    // Rows bottom first: pixel (0, 0) is the first one in the data.
    HX_CHECK(t->GetRawTextureData()[0] == 255 && t->GetPixelData<u8>(0).Size() == 32);
    HX_CHECK(t->GetPixels()[7].b == 1.0f && t->GetPixels32()[0].r == 255);
    // Wrap modes for coordinates outside.
    HX_CHECK(t->GetPixel(4, 0).r == 1.0f && t->GetPixel(-1, -1).b == 1.0f);
    t->SetWrapMode(TextureWrapMode::Clamp);
    HX_CHECK(t->GetPixel(-5, 0).r == 1.0f && t->GetPixel(9, 9).b == 1.0f);
    Color mid = t->GetPixelBilinear(0.25f, 0.5f);
    HX_CHECK(Near(mid.r, 0.25f) && Near(mid.a, 0.25f));
    t->Apply();
    HX_CHECK(t->updateCount() == 1 && t->GetPixel(0, 0, 2).a > 0.0f);
    Array<u8> png;
    HX_CHECK(t->EncodeToPNG(png) && png.Size() > 8);
    Texture2D* back = Texture2D::Create(1, 1, TextureFormat::RGBA32, false);
    HX_CHECK(back->LoadImage(Span<const u8>(png.Data(), png.Size())) && back->width() == 4 && back->height() == 2);
    HX_CHECK(back->GetPixel(0, 0) == Color(1, 0, 0, 1) && back->GetPixel(3, 1) == Color(0, 0, 1, 1) && back->mipmapCount() == 1);
    // Formats without a layout are refused; non readable textures refuse their pixels.
    HX_CHECK(Texture2D::Create(4, 4, TextureFormat::YUY2) == nullptr);
    t->Apply(false, true);
    HX_CHECK(!t->isReadable() && t->GetPixels().Empty());
    HX_CHECK(Texture2D::whiteTexture()->GetPixel(2, 2) == Color(1, 1, 1, 1) && Texture2D::whiteTexture() == Texture2D::whiteTexture());
    HX_CHECK(!Texture2D::normalTexture()->isDataSRGB());
    Cleanup();
}

HX_TEST(Texture2D, CompressAndHalfFloats)
{
    Texture2D* t = Texture2D::Create(8, 8, TextureFormat::RGB24, false);
    Array<Color32> c;
    for (i32 i = 0; i < 64; i++)
        c.Push(Color32(u8(i * 4), 128, 64, 255));
    HX_CHECK(t->SetPixels32(Span<const Color32>(c.Data(), c.Size())));
    HX_CHECK(t->Compress(true) && t->format() == TextureFormat::DXT1 && t->GetRawTextureData().Size() == 32);
    Color p = t->GetPixel(5, 0);
    HX_CHECK(Near(p.g, 128 / 255.0f, 0.05f) && Near(p.r, 20 / 255.0f, 0.06f));
    HX_CHECK(!t->SetPixel(0, 0, Color(1, 1, 1, 1)));
    Texture2D* h = Texture2D::Create(2, 2, TextureFormat::RGBAHalf, false, true);
    HX_CHECK(h->SetPixel(1, 1, Color(2.5f, -1, 0.5f, 1)) && h->GetPixel(1, 1) == Color(2.5f, -1, 0.5f, 1) && h->GetRawTextureData().Size() == 32);
    Cleanup();
}
