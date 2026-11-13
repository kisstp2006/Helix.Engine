#pragma once

#include <Helix/Engine/Object/Object.h>
#include <Helix/Math/Color.h>
#include <Helix/Math/Geometry.h>
#include <Helix/Math/Vector.h>

// Mesh: vertex streams in the layout of VertexAttributeDescriptor (attributes in the order of VertexAttribute inside
// each of up to 4 streams, all streams in one block), one index block of 16 or 32 bit indices, submeshes as ranges of
// it, and bounds. The simple API (vertices, normals, uv, triangles) converts to and from the layout; the advanced API
// writes the blocks as they are.
namespace helix::engine
{
    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class VertexAttribute : i32
    {
        Position = 0,
        Normal = 1,
        Tangent = 2,
        Color = 3,
        TexCoord0 = 4,
        TexCoord1 = 5,
        TexCoord2 = 6,
        TexCoord3 = 7,
        TexCoord4 = 8,
        TexCoord5 = 9,
        TexCoord6 = 10,
        TexCoord7 = 11,
        BlendWeight = 12,
        BlendIndices = 13,
    };
    inline constexpr u32 kVertexAttributeCount = 14;
    inline constexpr u32 kMaxVertexStreams = 4;

    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class VertexAttributeFormat : i32
    {
        Float32 = 0,
        Float16 = 1,
        UNorm8 = 2,
        SNorm8 = 3,
        UNorm16 = 4,
        SNorm16 = 5,
        UInt8 = 6,
        SInt8 = 7,
        UInt16 = 8,
        SInt16 = 9,
        UInt32 = 10,
        SInt32 = 11,
    };

    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class IndexFormat : i32
    {
        UInt16 = 0,
        UInt32 = 1,
    };

    HX_ENUM(Namespace = "HelixEngine")
    enum class MeshTopology : i32
    {
        Triangles = 0,
        Quads = 2,
        Lines = 3,
        LineStrip = 4,
        Points = 5,
    };

    HX_ENUM(Namespace = "HelixEngine.Rendering", Attributes = "[System.Flags]")
    enum class MeshUpdateFlags : i32
    {
        Default = 0,
        DontValidateIndices = 1,
        DontResetBoneBounds = 2,
        DontNotifyMeshUsers = 4,
        DontRecalculateBounds = 8,
    };
    constexpr MeshUpdateFlags operator|(MeshUpdateFlags a, MeshUpdateFlags b) { return MeshUpdateFlags(i32(a) | i32(b)); }
    constexpr bool Has(MeshUpdateFlags flags, MeshUpdateFlags one) { return (i32(flags) & i32(one)) != 0; }

    // Bytes of one component of the format.
    u32 VertexFormatSize(VertexAttributeFormat format);

    HX_STRUCT(Namespace = "HelixEngine.Rendering")
    struct VertexAttributeDescriptor
    {
        static constexpr StringView kTypeName = "VertexAttributeDescriptor";
        HX_FIELD() VertexAttribute attribute = VertexAttribute::Position;
        HX_FIELD() VertexAttributeFormat format = VertexAttributeFormat::Float32;
        HX_FIELD() i32 dimension = 3;
        HX_FIELD() i32 stream = 0;

        constexpr VertexAttributeDescriptor() = default;
        constexpr VertexAttributeDescriptor(VertexAttribute a, VertexAttributeFormat f = VertexAttributeFormat::Float32, i32 d = 3, i32 s = 0)
            : attribute(a), format(f), dimension(d), stream(s)
        {
        }
        bool operator==(const VertexAttributeDescriptor&) const = default;

        template <class V>
        void Transfer(V& v)
        {
            v.Field("attribute", attribute);
            v.Field("format", format);
            v.Field("dimension", dimension);
            v.Field("stream", stream);
        }
    };

    namespace detail
    {
        // A Vector3 as {x, y, z} in files.
        struct Vector3Field
        {
            static constexpr StringView kTypeName = "Vector3f";
            static constexpr bool kFlow = true;
            Vector3* value;
            template <class V>
            void Transfer(V& v)
            {
                v.Field("x", value->x);
                v.Field("y", value->y);
                v.Field("z", value->z);
            }
        };

        struct BoundsField
        {
            static constexpr StringView kTypeName = "AABB";
            Bounds* value;
            template <class V>
            void Transfer(V& v)
            {
                Vector3Field c{&value->center}, e{&value->extents};
                v.Field("m_Center", c);
                v.Field("m_Extent", e);
            }
        };
    }

    HX_STRUCT(Namespace = "HelixEngine.Rendering")
    struct SubMeshDescriptor
    {
        static constexpr StringView kTypeName = "SubMesh";
        // Filled by SetSubMesh unless MeshUpdateFlags::DontRecalculateBounds: the bounds and the range of vertices the
        // indices use.
        HX_FIELD() Bounds bounds;
        HX_FIELD() MeshTopology topology = MeshTopology::Triangles;
        HX_FIELD() i32 indexStart = 0;
        HX_FIELD() i32 indexCount = 0;
        // Added to every index.
        HX_FIELD() i32 baseVertex = 0;
        HX_FIELD() i32 firstVertex = 0;
        HX_FIELD() i32 vertexCount = 0;

        constexpr SubMeshDescriptor() = default;
        constexpr SubMeshDescriptor(i32 start, i32 count, MeshTopology t = MeshTopology::Triangles) : topology(t), indexStart(start), indexCount(count) {}

        template <class V>
        void Transfer(V& v)
        {
            v.Field("indexStart", indexStart);
            v.Field("indexCount", indexCount);
            v.Field("topology", topology);
            v.Field("baseVertex", baseVertex);
            v.Field("firstVertex", firstVertex);
            v.Field("vertexCount", vertexCount);
            detail::BoundsField b{&bounds};
            v.Field("localAABB", b);
        }
    };

    HX_CLASS(Namespace = "HelixEngine", Sealed)
    class Mesh : public Object
    {
        HX_ENGINE_CLASS(Mesh, Object, ClassId::Mesh, ClassNone)

    public:
        Mesh() = default;
        Mesh(const Mesh& other) = default;

        // ---------------------------------------------------------------------------------------------- simple API
        // new Mesh().
        HX_FUNCTION(Constructor) static Mesh* Create() { return NewObject<Mesh>(); }

        HX_PROPERTY(ReadOnly) i32 vertexCount() const { return i32(vertexCount_); }
        // The channels as properties (their setters are the Set<Channel> calls with the result dropped).
        HX_PROPERTY(Name = "vertices", Setter = "SetVerticesProperty") Array<Vector3> verticesProperty() const { return vertices(); }
        void SetVerticesProperty(Span<const Vector3> v) { SetVertices(v); }
        HX_PROPERTY(Name = "normals", Setter = "SetNormalsProperty") Array<Vector3> normalsProperty() const { return normals(); }
        void SetNormalsProperty(Span<const Vector3> v) { SetNormals(v); }
        HX_PROPERTY(Name = "tangents", Setter = "SetTangentsProperty") Array<Vector4> tangentsProperty() const { return tangents(); }
        void SetTangentsProperty(Span<const Vector4> v) { SetTangents(v); }
        HX_PROPERTY(Name = "colors", Setter = "SetColorsProperty") Array<Color> colorsProperty() const { return colors(); }
        void SetColorsProperty(Span<const Color> v) { SetColors(v); }
        HX_PROPERTY(Name = "colors32", Setter = "SetColors32Property") Array<Color32> colors32Property() const { return colors32(); }
        void SetColors32Property(Span<const Color32> v) { SetColors(v); }
        HX_PROPERTY(Name = "triangles", Setter = "SetTrianglesProperty") Array<i32> trianglesProperty() const { return triangles(); }
        void SetTrianglesProperty(Span<const i32> v) { SetTriangles(v); }
        HX_PROPERTY(Name = "uv", Setter = "SetUv1") Array<Vector2> uv1() const { return uv(0); }
        void SetUv1(Span<const Vector2> v) { SetUVs(0, v); }
        HX_PROPERTY(Name = "uv2", Setter = "SetUv2") Array<Vector2> uv2() const { return uv(1); }
        void SetUv2(Span<const Vector2> v) { SetUVs(1, v); }
        HX_PROPERTY(Name = "uv3", Setter = "SetUv3") Array<Vector2> uv3() const { return uv(2); }
        void SetUv3(Span<const Vector2> v) { SetUVs(2, v); }
        HX_PROPERTY(Name = "uv4", Setter = "SetUv4") Array<Vector2> uv4() const { return uv(3); }
        void SetUv4(Span<const Vector2> v) { SetUVs(3, v); }
        HX_PROPERTY(Name = "uv5", Setter = "SetUv5") Array<Vector2> uv5() const { return uv(4); }
        void SetUv5(Span<const Vector2> v) { SetUVs(4, v); }
        HX_PROPERTY(Name = "uv6", Setter = "SetUv6") Array<Vector2> uv6() const { return uv(5); }
        void SetUv6(Span<const Vector2> v) { SetUVs(5, v); }
        HX_PROPERTY(Name = "uv7", Setter = "SetUv7") Array<Vector2> uv7() const { return uv(6); }
        void SetUv7(Span<const Vector2> v) { SetUVs(6, v); }
        HX_PROPERTY(Name = "uv8", Setter = "SetUv8") Array<Vector2> uv8() const { return uv(7); }
        void SetUv8(Span<const Vector2> v) { SetUVs(7, v); }
        // The channels of List<T> overloads in C# (internal there).
        HX_FUNCTION(Internal) Array<Vector2> GetUVs2(i32 channel) const
        {
            Array<Vector2> out;
            GetUVs(channel, out);
            return out;
        }
        HX_FUNCTION(Internal) Array<Vector3> GetUVs3(i32 channel) const
        {
            Array<Vector3> out;
            GetUVs(channel, out);
            return out;
        }
        HX_FUNCTION(Internal) Array<Vector4> GetUVs4(i32 channel) const
        {
            Array<Vector4> out;
            GetUVs(channel, out);
            return out;
        }
        // A generic buffer write (C#'s SetVertexBufferData<T> / SetIndexBufferData<T>); starts and count in bytes.
        HX_FUNCTION(Internal, Void) bool SetVertexBufferRaw(Span<const u8> data, i32 dataStart, i32 meshBufferStart, i32 count, i32 stream, MeshUpdateFlags flags)
        {
            return SetVertexBufferBytes(data.Data(), data.Size(), usize(dataStart), usize(meshBufferStart), usize(count), stream, flags);
        }
        HX_FUNCTION(Internal, Void) bool SetIndexBufferRaw(Span<const u8> data, i32 dataStart, i32 meshBufferStart, i32 count, MeshUpdateFlags flags)
        {
            return SetIndexBufferBytes(data.Data(), data.Size(), usize(dataStart), usize(meshBufferStart), usize(count), flags);
        }

        // Positions; a different count resizes every attribute (new vertices zero). False with an error when the
        // indices use more vertices than given. Recalculates the bounds.
        HX_FUNCTION(Void) bool SetVertices(Span<const Vector3> vertices);
        void GetVertices(Array<Vector3>& out) const;
        Array<Vector3> vertices() const;
        // The other attributes need one value per vertex; an empty list removes the attribute.
        HX_FUNCTION(Void) bool SetNormals(Span<const Vector3> normals);
        void GetNormals(Array<Vector3>& out) const;
        Array<Vector3> normals() const;
        HX_FUNCTION(Void) bool SetTangents(Span<const Vector4> tangents);
        void GetTangents(Array<Vector4>& out) const;
        Array<Vector4> tangents() const;
        HX_FUNCTION(Void) bool SetColors(Span<const Color> colors);
        void GetColors(Array<Color>& out) const;
        Array<Color> colors() const;
        HX_FUNCTION(Void) bool SetColors(Span<const Color32> colors);
        void GetColors(Array<Color32>& out) const;
        Array<Color32> colors32() const;
        // Texture coordinates of set 0 to 7 with 2, 3 or 4 components.
        HX_FUNCTION(Void) bool SetUVs(i32 channel, Span<const Vector2> uvs);
        HX_FUNCTION(Void) bool SetUVs(i32 channel, Span<const Vector3> uvs);
        HX_FUNCTION(Void) bool SetUVs(i32 channel, Span<const Vector4> uvs);
        void GetUVs(i32 channel, Array<Vector2>& out) const;
        void GetUVs(i32 channel, Array<Vector3>& out) const;
        void GetUVs(i32 channel, Array<Vector4>& out) const;
        Array<Vector2> uv(i32 channel = 0) const;

        // Every submesh's triangles in order; setting them makes the mesh one submesh.
        Array<i32> triangles() const;
        HX_FUNCTION(Void) bool SetTriangles(Span<const i32> triangles);
        HX_FUNCTION(Void) bool SetTriangles(Span<const i32> triangles, i32 submesh, bool calculateBounds = true, i32 baseVertex = 0);
        void GetTriangles(Array<i32>& out, i32 submesh, bool applyBaseVertex = true) const;
        HX_FUNCTION() Array<i32> GetTriangles(i32 submesh, bool applyBaseVertex = true) const;
        HX_FUNCTION(Void) bool SetIndices(Span<const i32> indices, MeshTopology topology, i32 submesh, bool calculateBounds = true, i32 baseVertex = 0);
        void GetIndices(Array<i32>& out, i32 submesh, bool applyBaseVertex = true) const;
        HX_FUNCTION() Array<i32> GetIndices(i32 submesh, bool applyBaseVertex = true) const;
        HX_FUNCTION() u32 GetIndexStart(i32 submesh) const;
        HX_FUNCTION() u32 GetIndexCount(i32 submesh) const;
        HX_FUNCTION() u32 GetBaseVertex(i32 submesh) const;
        HX_FUNCTION() MeshTopology GetTopology(i32 submesh) const;

        HX_PROPERTY() i32 subMeshCount() const { return i32(subMeshes_.Size()); }
        // Fewer submeshes drop their indices; new ones are empty.
        void SetSubMeshCount(i32 count);
        HX_PROPERTY() Bounds bounds() const { return bounds_; }
        void SetBounds(const Bounds& value) { bounds_ = value; }
        HX_PROPERTY(Setter = "SetIndexFormatProperty") IndexFormat indexFormat() const { return indexFormat_; }
        // Converts the indices; 16 bit with an index above 65535 is an error.
        bool SetIndexFormat(IndexFormat format);
        void SetIndexFormatProperty(IndexFormat format) { SetIndexFormat(format); }

        // ---------------------------------------------------------------------------------------------- advanced API
        // The vertex count and layout; data of attributes in both layouts is kept (converted), new ones are zero.
        // C# takes the attributes as params (a partial).
        HX_FUNCTION(Void, Internal, Name = "SetVertexBufferParamsNative") bool SetVertexBufferParams(i32 vertexCount, Span<const VertexAttributeDescriptor> attributes);
        // `count` elements of T from data[dataStart] to element `meshBufferStart` of the stream (elements of T).
        template <class T>
        bool SetVertexBufferData(Span<const T> data, i32 dataStart, i32 meshBufferStart, i32 count, i32 stream = 0,
                                 MeshUpdateFlags flags = MeshUpdateFlags::Default)
        {
            return SetVertexBufferBytes(reinterpret_cast<const u8*>(data.Data()), usize(data.Size()) * sizeof(T), usize(dataStart) * sizeof(T),
                                        usize(meshBufferStart) * sizeof(T), usize(count) * sizeof(T), stream, flags);
        }
        HX_FUNCTION(Void) bool SetIndexBufferParams(i32 indexCount, IndexFormat format);
        template <class T>
        bool SetIndexBufferData(Span<const T> data, i32 dataStart, i32 meshBufferStart, i32 count, MeshUpdateFlags flags = MeshUpdateFlags::Default)
        {
            return SetIndexBufferBytes(reinterpret_cast<const u8*>(data.Data()), usize(data.Size()) * sizeof(T), usize(dataStart) * sizeof(T),
                                       usize(meshBufferStart) * sizeof(T), usize(count) * sizeof(T), flags);
        }
        HX_FUNCTION(Void) bool SetSubMesh(i32 index, const SubMeshDescriptor& descriptor, MeshUpdateFlags flags = MeshUpdateFlags::Default);
        HX_FUNCTION(Void) bool SetSubMeshes(Span<const SubMeshDescriptor> descriptors, MeshUpdateFlags flags = MeshUpdateFlags::Default);
        HX_FUNCTION() SubMeshDescriptor GetSubMesh(i32 index) const;

        HX_PROPERTY(ReadOnly) i32 vertexBufferCount() const;
        HX_FUNCTION() i32 GetVertexBufferStride(i32 stream) const;
        HX_PROPERTY(ReadOnly) i32 vertexAttributeCount() const;
        void GetVertexAttributes(Array<VertexAttributeDescriptor>& out) const;
        HX_FUNCTION() Array<VertexAttributeDescriptor> GetVertexAttributes() const;
        HX_FUNCTION() VertexAttributeDescriptor GetVertexAttribute(i32 index) const;
        HX_FUNCTION() bool HasVertexAttribute(VertexAttribute attribute) const { return channels_[u32(attribute)].stream != kNoStream; }
        // -1 for attributes the mesh does not have.
        HX_FUNCTION() i32 GetVertexAttributeDimension(VertexAttribute attribute) const;
        HX_FUNCTION() VertexAttributeFormat GetVertexAttributeFormat(VertexAttribute attribute) const;
        HX_FUNCTION() i32 GetVertexAttributeStream(VertexAttribute attribute) const;
        HX_FUNCTION() i32 GetVertexAttributeOffset(VertexAttribute attribute) const;

        HX_FUNCTION() void RecalculateBounds(MeshUpdateFlags flags = MeshUpdateFlags::Default);
        // Area weighted face normals summed over the vertices the triangles share (the vertex count stays).
        HX_FUNCTION() void RecalculateNormals(MeshUpdateFlags flags = MeshUpdateFlags::Default);
        // From the normals and uv set 0, the bitangent sign in w (the vertex count stays).
        HX_FUNCTION() void RecalculateTangents(MeshUpdateFlags flags = MeshUpdateFlags::Default);
        HX_FUNCTION() void Clear(bool keepVertexLayout = true);
        HX_FUNCTION() void MarkDynamic() { dynamic_ = true; }
        HX_FUNCTION() void MarkModified() { version_++; }
        // Hands the data to the renderer; markNoLongerReadable makes the CPU copy inaccessible (it goes once the
        // renderer holds the data).
        HX_FUNCTION() void UploadMeshData(bool markNoLongerReadable);
        HX_PROPERTY(ReadOnly) bool isReadable() const { return readable_; }
        bool isDynamic() const { return dynamic_; }

        // ---------------------------------------------------------------------------------------------- native
        // Bumped by every change: the renderer uploads meshes whose version moved.
        u32 Version() const { return version_; }
        Span<const u8> VertexData(i32 stream) const;
        Span<const u8> IndexData() const { return Span<const u8>(indexData_.Data(), indexData_.Size()); }
        u32 IndexCount() const { return u32(indexData_.Size() / IndexSize()); }
        // The CPU copy of a mesh that is no longer readable (the renderer, once it holds the data).
        void ReleaseCpuData();
        // One attribute as floats, `components` per vertex (the renderer's upload; readable or not).
        void CopyAttributeFloats(VertexAttribute attribute, f32* out, u32 components) const
        {
            if (HasVertexAttribute(attribute))
                GetAttribute(attribute, out, components);
        }
        // Takes whole blocks made elsewhere (importers): the layout, the streams one after the other, the indices and the
        // submeshes with their bounds. False with an error when the sizes do not match.
        bool SetMeshData(u32 vertexCount, Span<const VertexAttributeDescriptor> layout, Array<u8>&& vertexData, IndexFormat indexFormat,
                         Array<u8>&& indexData, Array<SubMeshDescriptor>&& subMeshes, const Bounds& bounds);
        void SetReadable(bool readable) { readable_ = readable; }
        // Resources.UnloadAsset.
        void UnloadDataNative() { Clear(false); }

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("m_Name", name_);
            Array<VertexAttributeDescriptor> attributes;
            u32 count = vertexCount_;
            if constexpr (!V::kReading)
            GetVertexAttributes(attributes);
            v.Field("m_SubMeshes", subMeshes_);
            v.Field("m_IndexFormat", indexFormat_);
            v.Field("m_IndexBuffer", indexData_);
            v.Field("m_VertexCount", count);
            v.Field("m_VertexAttributes", attributes);
            v.Field("m_VertexData", vertexData_);
            detail::BoundsField b{&bounds_};
            v.Field("m_LocalAABB", b);
            v.Field("m_IsReadable", readable_);
            if constexpr (V::kReading)
                Loaded(count, attributes);
        }

    private:
        static constexpr u8 kNoStream = 0xFF;
        struct Channel
        {
            u8 stream = kNoStream;
            u8 offset = 0;
            u8 format = 0;
            u8 dimension = 0;
        };
        struct Layout
        {
            Channel channels[kVertexAttributeCount];
            u32 strides[kMaxVertexStreams] = {};
        };

        u32 IndexSize() const { return indexFormat_ == IndexFormat::UInt16 ? 2 : 4; }
        usize StreamOffset(u32 stream) const;
        bool Readable(StringView what) const;
        static bool MakeLayout(Span<const VertexAttributeDescriptor> attributes, Layout& out, String& error);
        void CurrentLayout(Layout& out) const;
        void Relayout(const Layout& layout, u32 vertexCount);
        // Sets one attribute from floats (`components` per vertex), adding it in `format` / `dimension` when missing.
        bool SetAttribute(VertexAttribute attribute, const f32* values, usize count, u32 components, VertexAttributeFormat format, StringView what);
        void GetAttribute(VertexAttribute attribute, f32* out, u32 components) const;
        // The attribute's first 3 floats per vertex: in place when it is Float32, else converted into `scratch`; the
        // stride in floats.
        const f32* FloatAttribute(VertexAttribute attribute, Array<f32>& scratch, usize& stride) const;
        // The mesh's bounds from its submeshes' (all vertices without indices).
        void UnionBounds();
        void RemoveAttribute(VertexAttribute attribute);
        u32 IndexAt(usize i) const;
        u32 MaxIndex(const SubMeshDescriptor& s) const;
        void SubMeshBounds(SubMeshDescriptor& s) const;
        bool SetVertexBufferBytes(const u8* data, usize size, usize from, usize to, usize bytes, i32 stream, MeshUpdateFlags flags);
        bool SetIndexBufferBytes(const u8* data, usize size, usize from, usize to, usize bytes, MeshUpdateFlags flags);
        void Loaded(u32 vertexCount, const Array<VertexAttributeDescriptor>& attributes);

        Channel channels_[kVertexAttributeCount];
        u32 strides_[kMaxVertexStreams] = {};
        u32 vertexCount_ = 0;
        // The streams one after the other.
        Array<u8> vertexData_ = Array<u8>();
        Array<u8> indexData_ = Array<u8>();
        IndexFormat indexFormat_ = IndexFormat::UInt16;
        Array<SubMeshDescriptor> subMeshes_ = Array<SubMeshDescriptor>();
        Bounds bounds_;
        bool readable_ = true;
        bool dynamic_ = false;
        u32 version_ = 0;
    };
}
