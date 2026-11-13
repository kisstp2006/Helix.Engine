#include <Helix/Engine/Mesh/MeshData.h>

#include <cstring>

namespace helix::engine
{
    namespace
    {
        Mesh* Hidden(Mesh* mesh)
        {
            mesh->hideFlags = HideFlags::HideAndDontSave;
            return mesh;
        }
    }

    u64 MeshDataNative::AcquireReadOnly(Mesh* mesh)
    {
        if (!mesh)
            return 0;
        Object* copy = CloneObject(*mesh);
        return copy ? u64(uptr(Hidden(static_cast<Mesh*>(copy)))) : 0;
    }

    u64 MeshDataNative::AllocateWritable() { return u64(uptr(Hidden(NewObject<Mesh>()))); }

    void MeshDataNative::Release(u64 data)
    {
        if (data)
            DeleteObject(M(data));
    }

    void MeshDataNative::Apply(u64 data, Mesh* mesh, MeshUpdateFlags)
    {
        const Mesh* from = M(data);
        if (!from || !mesh)
            return;
        // The streams one after the other, as SetMeshData takes them.
        Array<u8> vertices;
        for (i32 s = 0; s < from->vertexBufferCount(); s++)
        {
            Span<const u8> stream = from->VertexData(s);
            usize at = vertices.Size();
            vertices.Resize(at + stream.Size());
            if (stream.Size())
                std::memcpy(vertices.Data() + at, stream.Data(), stream.Size());
        }
        Span<const u8> indexData = from->IndexData();
        Array<u8> indices;
        indices.Resize(indexData.Size());
        if (indexData.Size())
            std::memcpy(indices.Data(), indexData.Data(), indexData.Size());
        Array<SubMeshDescriptor> subMeshes;
        for (i32 i = 0; i < from->subMeshCount(); i++)
            subMeshes.Push(from->GetSubMesh(i));
        Array<VertexAttributeDescriptor> layout = from->GetVertexAttributes();
        mesh->SetMeshData(u32(from->vertexCount()), Span<const VertexAttributeDescriptor>(layout.Data(), layout.Size()), Move(vertices),
                          from->indexFormat(), Move(indices), Move(subMeshes), from->bounds());
    }

    void MeshDataNative::CopyAttribute(u64 data, VertexAttribute attribute, u64 out, i32 components)
    {
        M(data)->CopyAttributeFloats(attribute, reinterpret_cast<f32*>(uptr(out)), u32(components));
    }

    i32 MeshDataNative::CopyIndices(u64 data, u64 out, i32 capacity, bool shorts, i32 submesh, bool applyBaseVertex)
    {
        const Mesh* m = M(data);
        if (submesh < 0 || submesh >= m->subMeshCount())
            return -1;
        SubMeshDescriptor s = m->GetSubMesh(submesh);
        if (!out)
            return s.indexCount;
        Span<const u8> bytes = m->IndexData();
        bool wide = m->indexFormat() == IndexFormat::UInt32;
        i32 base = applyBaseVertex ? s.baseVertex : 0;
        i32 n = s.indexCount < capacity ? s.indexCount : capacity;
        for (i32 i = 0; i < n; i++)
        {
            usize at = usize(s.indexStart + i);
            u32 index = 0;
            if (wide)
                std::memcpy(&index, bytes.Data() + at * 4, 4);
            else
            {
                u16 v = 0;
                std::memcpy(&v, bytes.Data() + at * 2, 2);
                index = v;
            }
            i32 value = i32(index) + base;
            if (shorts)
                reinterpret_cast<u16*>(uptr(out))[i] = u16(value);
            else
                reinterpret_cast<i32*>(uptr(out))[i] = value;
        }
        return s.indexCount;
    }
}
