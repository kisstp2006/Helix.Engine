#pragma once

#include <Helix/Engine/Mesh/Mesh.h>

// Mesh.MeshData of the script API: a mesh's data for jobs. Each MeshData is a hidden mesh (a copy for the read only
// ones, an empty one for the writable ones) made and destroyed on the main thread; jobs call its members by address,
// which touch only that mesh. ApplyAndDisposeWritableMeshData copies a writable one into a real mesh.
namespace helix::engine
{
    HX_CLASS(Namespace = "HelixEngine.Internal", Static, Internal)
    class MeshDataNative
    {
    public:
        HX_FUNCTION() static u64 AcquireReadOnly(Mesh* mesh);
        HX_FUNCTION() static u64 AllocateWritable();
        HX_FUNCTION() static void Release(u64 data);
        HX_FUNCTION() static void Apply(u64 data, Mesh* mesh, MeshUpdateFlags flags);

        HX_FUNCTION(Leaf) static i32 VertexCount(u64 data) { return M(data)->vertexCount(); }
        HX_FUNCTION(Leaf) static i32 VertexBufferCount(u64 data) { return M(data)->vertexBufferCount(); }
        HX_FUNCTION(Leaf) static IndexFormat GetIndexFormat(u64 data) { return M(data)->indexFormat(); }
        HX_FUNCTION(Leaf) static i32 SubMeshCount(u64 data) { return M(data)->subMeshCount(); }
        HX_FUNCTION() static void SetSubMeshCount(u64 data, i32 count) { M(data)->SetSubMeshCount(count); }
        HX_FUNCTION(Leaf) static bool HasVertexAttribute(u64 data, VertexAttribute attribute) { return M(data)->HasVertexAttribute(attribute); }
        HX_FUNCTION(Leaf) static i32 GetVertexAttributeDimension(u64 data, VertexAttribute attribute) { return M(data)->GetVertexAttributeDimension(attribute); }
        HX_FUNCTION(Leaf) static VertexAttributeFormat GetVertexAttributeFormat(u64 data, VertexAttribute attribute) { return M(data)->GetVertexAttributeFormat(attribute); }
        HX_FUNCTION(Leaf) static i32 GetVertexAttributeStream(u64 data, VertexAttribute attribute) { return M(data)->GetVertexAttributeStream(attribute); }
        HX_FUNCTION(Leaf) static i32 GetVertexAttributeOffset(u64 data, VertexAttribute attribute) { return M(data)->GetVertexAttributeOffset(attribute); }
        HX_FUNCTION(Leaf) static i32 GetVertexBufferStride(u64 data, i32 stream) { return M(data)->GetVertexBufferStride(stream); }
        // The bytes of a stream and of the indices where they are (C# views them as NativeArrays).
        HX_FUNCTION(Leaf) static u64 VertexDataAddress(u64 data, i32 stream) { return u64(uptr(M(data)->VertexData(stream).Data())); }
        HX_FUNCTION(Leaf) static i32 VertexDataSize(u64 data, i32 stream) { return i32(M(data)->VertexData(stream).Size()); }
        HX_FUNCTION(Leaf) static u64 IndexDataAddress(u64 data) { return u64(uptr(M(data)->IndexData().Data())); }
        HX_FUNCTION(Leaf) static i32 IndexDataSize(u64 data) { return i32(M(data)->IndexData().Size()); }
        // One attribute as `components` floats per vertex into `out` (vertexCount * components floats).
        HX_FUNCTION() static void CopyAttribute(u64 data, VertexAttribute attribute, u64 out, i32 components);
        // The indices of a submesh into `out` (as int, or as ushort when `shorts`); their count.
        HX_FUNCTION() static i32 CopyIndices(u64 data, u64 out, i32 capacity, bool shorts, i32 submesh, bool applyBaseVertex);
        HX_FUNCTION(NativeArrays) static void SetVertexBufferParams(u64 data, i32 vertexCount, Span<const VertexAttributeDescriptor> attributes)
        {
            M(data)->SetVertexBufferParams(vertexCount, attributes);
        }
        HX_FUNCTION() static void SetIndexBufferParams(u64 data, i32 indexCount, IndexFormat format) { M(data)->SetIndexBufferParams(indexCount, format); }
        HX_FUNCTION() static SubMeshDescriptor GetSubMesh(u64 data, i32 index) { return M(data)->GetSubMesh(index); }
        HX_FUNCTION() static void SetSubMesh(u64 data, i32 index, SubMeshDescriptor descriptor, MeshUpdateFlags flags) { M(data)->SetSubMesh(index, descriptor, flags); }

    private:
        static Mesh* M(u64 data) { return reinterpret_cast<Mesh*>(uptr(data)); }
    };
}
