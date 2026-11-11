#include <Helix/Engine/Mesh/Mesh.h>

#include <Helix/Core/Logging/Log.h>
#include <Helix/Core/Strings/Format.h>
#include <Helix/Image/Texture/Convert.h>

#include <cmath>
#include <cstring>

namespace helix::engine
{
    namespace
    {
        LogChannel MeshLog{"Engine"};

        // Reading and writing one component of each format as a float (normalized formats scaled to their range).
        struct FormatOps
        {
            u8 size;
            f32 (*read)(const u8*);
            void (*write)(u8*, f32);
        };

        template <class T>
        T Load(const u8* p)
        {
            T v;
            std::memcpy(&v, p, sizeof(T));
            return v;
        }
        template <class T>
        void Store(u8* p, T v)
        {
            std::memcpy(p, &v, sizeof(T));
        }
        f32 Clamp(f32 v, f32 lo, f32 hi) { return v < lo ? lo : v > hi ? hi : v; }
        template <class T>
        T Round(f32 v)
        {
            return T(std::lround(v));
        }

        constexpr FormatOps kFormats[] = {
            {4, [](const u8* p) { return Load<f32>(p); }, [](u8* p, f32 v) { Store(p, v); }},
            {2, [](const u8* p) { return image::HalfToFloat(Load<u16>(p)); }, [](u8* p, f32 v) { Store(p, image::FloatToHalf(v)); }},
            {1, [](const u8* p) { return f32(*p) / 255.0f; }, [](u8* p, f32 v) { *p = Round<u8>(Clamp(v, 0, 1) * 255.0f); }},
            {1, [](const u8* p) { return Clamp(f32(i8(*p)) / 127.0f, -1, 1); }, [](u8* p, f32 v) { *p = u8(Round<i8>(Clamp(v, -1, 1) * 127.0f)); }},
            {2, [](const u8* p) { return f32(Load<u16>(p)) / 65535.0f; }, [](u8* p, f32 v) { Store(p, Round<u16>(Clamp(v, 0, 1) * 65535.0f)); }},
            {2, [](const u8* p) { return Clamp(f32(Load<i16>(p)) / 32767.0f, -1, 1); }, [](u8* p, f32 v) { Store(p, Round<i16>(Clamp(v, -1, 1) * 32767.0f)); }},
            {1, [](const u8* p) { return f32(*p); }, [](u8* p, f32 v) { *p = Round<u8>(Clamp(v, 0, 255)); }},
            {1, [](const u8* p) { return f32(i8(*p)); }, [](u8* p, f32 v) { *p = u8(Round<i8>(Clamp(v, -128, 127))); }},
            {2, [](const u8* p) { return f32(Load<u16>(p)); }, [](u8* p, f32 v) { Store(p, Round<u16>(Clamp(v, 0, 65535))); }},
            {2, [](const u8* p) { return f32(Load<i16>(p)); }, [](u8* p, f32 v) { Store(p, Round<i16>(Clamp(v, -32768, 32767))); }},
            {4, [](const u8* p) { return f32(Load<u32>(p)); }, [](u8* p, f32 v) { Store(p, u32(v < 0 ? 0 : v)); }},
            {4, [](const u8* p) { return f32(Load<i32>(p)); }, [](u8* p, f32 v) { Store(p, i32(v)); }},
        };
        constexpr u32 kFormatCount = u32(sizeof(kFormats) / sizeof(kFormats[0]));

        constexpr StringView kAttributeNames[kVertexAttributeCount] = {
            "Position", "Normal", "Tangent", "Color", "TexCoord0", "TexCoord1", "TexCoord2", "TexCoord3",
            "TexCoord4", "TexCoord5", "TexCoord6", "TexCoord7", "BlendWeight", "BlendIndices",
        };

        Vector3 Sub(Vector3 a, Vector3 b) { return Vector3(a.x - b.x, a.y - b.y, a.z - b.z); }
        Vector3 Cross(Vector3 a, Vector3 b) { return Vector3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
        f32 Dot(Vector3 a, Vector3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
        Vector3 Normalized(Vector3 v)
        {
            f32 l = std::sqrt(Dot(v, v));
            return l > 1e-20f ? Vector3(v.x / l, v.y / l, v.z / l) : Vector3();
        }

        // Calls f(index) for `count` indices from `begin`.
        template <class F>
        void ForIndices(const u8* data, IndexFormat format, usize begin, usize count, F&& f)
        {
            if (format == IndexFormat::UInt16)
            {
                const u8* p = data + begin * 2;
                for (usize i = 0; i < count; i++)
                    f(u32(Load<u16>(p + i * 2)));
            }
            else
            {
                const u8* p = data + begin * 4;
                for (usize i = 0; i < count; i++)
                    f(Load<u32>(p + i * 4));
            }
        }

        template <class I, class F>
        void TrianglesOf(const u8* data, const SubMeshDescriptor& s, F&& f)
        {
            const u8* p = data + usize(s.indexStart) * sizeof(I);
            usize count = usize(s.indexCount);
            i64 base = s.baseVertex;
            auto at = [p, base](usize i) { return i64(Load<I>(p + i * sizeof(I))) + base; };
            if (s.topology == MeshTopology::Triangles)
                for (usize i = 0; i + 2 < count; i += 3)
                    f(at(i), at(i + 1), at(i + 2));
            else if (s.topology == MeshTopology::Quads)
                for (usize i = 0; i + 3 < count; i += 4)
                {
                    i64 a = at(i), b = at(i + 1), c = at(i + 2), d = at(i + 3);
                    f(a, b, c);
                    f(a, c, d);
                }
        }

        // Calls f(a, b, c) for every triangle of a submesh (quads as two triangles), indices with the base vertex.
        template <class F>
        void ForTriangles(const u8* data, IndexFormat format, const SubMeshDescriptor& s, F&& f)
        {
            if (format == IndexFormat::UInt16)
                TrianglesOf<u16>(data, s, f);
            else
                TrianglesOf<u32>(data, s, f);
        }
    }

    u32 VertexFormatSize(VertexAttributeFormat format) { return u32(format) < kFormatCount ? kFormats[u32(format)].size : 0; }

    // ------------------------------------------------------------------------------------------------- layout

    bool Mesh::MakeLayout(Span<const VertexAttributeDescriptor> attributes, Layout& out, String& error)
    {
        out = Layout();
        for (const VertexAttributeDescriptor& a : attributes)
        {
            u32 index = u32(a.attribute);
            if (index >= kVertexAttributeCount)
                return error = Format("vertex attribute {} does not exist", i32(a.attribute)), false;
            StringView name = kAttributeNames[index];
            if (out.channels[index].stream != kNoStream)
                return error = Format("vertex attribute {} is given twice", name), false;
            if (u32(a.format) >= kFormatCount)
                return error = Format("vertex attribute {} has the unknown format {}", name, i32(a.format)), false;
            if (a.dimension < 1 || a.dimension > 4)
                return error = Format("vertex attribute {} has {} components (1 to 4)", name, a.dimension), false;
            if (a.stream < 0 || a.stream >= i32(kMaxVertexStreams))
                return error = Format("vertex attribute {} is in stream {} (0 to 3)", name, a.stream), false;
            if ((kFormats[u32(a.format)].size * u32(a.dimension)) % 4 != 0)
                return error = Format("vertex attribute {} takes {} bytes, not a multiple of 4", name, kFormats[u32(a.format)].size * u32(a.dimension)), false;
            out.channels[index] = {u8(a.stream), 0, u8(a.format), u8(a.dimension)};
        }
        // Attributes in the order of the enum inside each stream.
        for (Channel& c : out.channels)
            if (c.stream != kNoStream)
            {
                c.offset = u8(out.strides[c.stream]);
                out.strides[c.stream] += kFormats[c.format].size * c.dimension;
            }
        return true;
    }

    void Mesh::CurrentLayout(Layout& out) const
    {
        for (u32 i = 0; i < kVertexAttributeCount; i++)
            out.channels[i] = channels_[i];
        for (u32 s = 0; s < kMaxVertexStreams; s++)
            out.strides[s] = strides_[s];
    }

    usize Mesh::StreamOffset(u32 stream) const
    {
        usize offset = 0;
        for (u32 s = 0; s < stream; s++)
            offset += usize(strides_[s]) * vertexCount_;
        return offset;
    }

    void Mesh::Relayout(const Layout& layout, u32 vertexCount)
    {
        usize size = 0;
        usize offsets[kMaxVertexStreams];
        for (u32 s = 0; s < kMaxVertexStreams; s++)
        {
            offsets[s] = size;
            size += usize(layout.strides[s]) * vertexCount;
        }
        Array<u8> data;
        data.Resize(size);
        if (size)
            std::memset(data.Data(), 0, size);
        u32 keep = vertexCount < vertexCount_ ? vertexCount : vertexCount_;
        for (u32 a = 0; a < kVertexAttributeCount; a++)
        {
            const Channel& from = channels_[a];
            const Channel& to = layout.channels[a];
            if (from.stream == kNoStream || to.stream == kNoStream || keep == 0)
                continue;
            const u8* src = vertexData_.Data() + StreamOffset(from.stream) + from.offset;
            u8* dst = data.Data() + offsets[to.stream] + to.offset;
            u32 srcStride = strides_[from.stream], dstStride = layout.strides[to.stream];
            if (from.format == to.format && from.dimension == to.dimension)
            {
                u32 bytes = kFormats[from.format].size * from.dimension;
                for (u32 v = 0; v < keep; v++)
                    std::memcpy(dst + usize(v) * dstStride, src + usize(v) * srcStride, bytes);
                continue;
            }
            const FormatOps& r = kFormats[from.format];
            const FormatOps& w = kFormats[to.format];
            u32 n = from.dimension < to.dimension ? from.dimension : to.dimension;
            for (u32 v = 0; v < keep; v++)
                for (u32 c = 0; c < n; c++)
                    w.write(dst + usize(v) * dstStride + c * w.size, r.read(src + usize(v) * srcStride + c * r.size));
        }
        for (u32 i = 0; i < kVertexAttributeCount; i++)
            channels_[i] = layout.channels[i];
        for (u32 s = 0; s < kMaxVertexStreams; s++)
            strides_[s] = layout.strides[s];
        vertexCount_ = vertexCount;
        vertexData_ = Move(data);
        version_++;
    }

    bool Mesh::Readable(StringView what) const
    {
        if (readable_)
            return true;
        HX_LOG_ERROR(MeshLog, "Not allowed to access {} on mesh '{}' (isReadable is false).", what, name_);
        return false;
    }

    // ------------------------------------------------------------------------------------------------- attributes

    bool Mesh::SetAttribute(VertexAttribute attribute, const f32* values, usize count, u32 components, VertexAttributeFormat format, StringView what)
    {
        if (!Readable(what))
            return false;
        if (count == 0)
        {
            RemoveAttribute(attribute);
            return true;
        }
        if (count != vertexCount_)
        {
            HX_LOG_ERROR(MeshLog, "Mesh.{} is out of bounds: {} values for {} vertices on mesh '{}'.", what, count, vertexCount_, name_);
            return false;
        }
        u32 a = u32(attribute);
        // Texture coordinates take the dimension given; the other attributes keep a layout wide enough.
        bool exact = a >= u32(VertexAttribute::TexCoord0) && a <= u32(VertexAttribute::TexCoord7);
        if (channels_[a].stream == kNoStream || (exact ? channels_[a].dimension != components : channels_[a].dimension < components))
        {
            Array<VertexAttributeDescriptor> attributes;
            GetVertexAttributes(attributes);
            u8 stream = channels_[a].stream == kNoStream ? 0 : channels_[a].stream;
            bool found = false;
            for (VertexAttributeDescriptor& d : attributes)
                if (d.attribute == attribute)
                    d = VertexAttributeDescriptor(attribute, format, i32(components), stream), found = true;
            if (!found)
                attributes.Push(VertexAttributeDescriptor(attribute, format, i32(components), stream));
            Layout layout;
            String error;
            if (!MakeLayout(Span<const VertexAttributeDescriptor>(attributes.Data(), attributes.Size()), layout, error))
            {
                HX_LOG_ERROR(MeshLog, "Mesh.{} on mesh '{}': {}.", what, name_, error);
                return false;
            }
            Relayout(layout, vertexCount_);
        }
        const Channel& c = channels_[a];
        u8* dst = vertexData_.Data() + StreamOffset(c.stream) + c.offset;
        u32 stride = strides_[c.stream];
        if (c.dimension > components)
        {
            // Components past the given ones are zero.
            const FormatOps& w = kFormats[c.format];
            for (usize v = 0; v < count; v++)
                for (u32 k = 0; k < c.dimension; k++)
                    w.write(dst + v * stride + k * w.size, k < components ? values[v * components + k] : 0.0f);
        }
        else if (VertexAttributeFormat(c.format) == VertexAttributeFormat::Float32)
        {
            if (stride == components * sizeof(f32))
                std::memcpy(dst, values, count * stride);
            else
                for (usize v = 0; v < count; v++)
                    std::memcpy(dst + v * stride, values + v * components, components * sizeof(f32));
        }
        else
        {
            const FormatOps& w = kFormats[c.format];
            for (usize v = 0; v < count; v++)
                for (u32 k = 0; k < components; k++)
                    w.write(dst + v * stride + k * w.size, values[v * components + k]);
        }
        version_++;
        return true;
    }

    void Mesh::GetAttribute(VertexAttribute attribute, f32* out, u32 components) const
    {
        const Channel& c = channels_[u32(attribute)];
        const u8* src = vertexData_.Data() + StreamOffset(c.stream) + c.offset;
        u32 stride = strides_[c.stream];
        u32 n = c.dimension < components ? c.dimension : components;
        if (VertexAttributeFormat(c.format) == VertexAttributeFormat::Float32)
        {
            // The common case: plain copies.
            if (n == components && stride == components * 4)
                std::memcpy(out, src, usize(vertexCount_) * stride);
            else
                for (u32 v = 0; v < vertexCount_; v++)
                {
                    f32* o = out + usize(v) * components;
                    std::memcpy(o, src + usize(v) * stride, n * 4);
                    for (u32 k = n; k < components; k++)
                        o[k] = 0;
                }
            return;
        }
        const FormatOps& r = kFormats[c.format];
        for (u32 v = 0; v < vertexCount_; v++)
        {
            f32* o = out + usize(v) * components;
            for (u32 k = 0; k < n; k++)
                o[k] = r.read(src + usize(v) * stride + k * r.size);
            for (u32 k = n; k < components; k++)
                o[k] = 0;
        }
    }

    const f32* Mesh::FloatAttribute(VertexAttribute attribute, Array<f32>& scratch, usize& stride) const
    {
        const Channel& c = channels_[u32(attribute)];
        if (VertexAttributeFormat(c.format) == VertexAttributeFormat::Float32 && c.dimension >= 3)
        {
            stride = strides_[c.stream] / 4;
            return reinterpret_cast<const f32*>(vertexData_.Data() + StreamOffset(c.stream) + c.offset);
        }
        scratch.Resize(usize(vertexCount_) * 3);
        GetAttribute(attribute, scratch.Data(), 3);
        stride = 3;
        return scratch.Data();
    }

    void Mesh::RemoveAttribute(VertexAttribute attribute)
    {
        if (channels_[u32(attribute)].stream == kNoStream)
            return;
        Array<VertexAttributeDescriptor> attributes;
        GetVertexAttributes(attributes);
        Array<VertexAttributeDescriptor> rest;
        for (const VertexAttributeDescriptor& d : attributes)
            if (d.attribute != attribute)
                rest.Push(d);
        Layout layout;
        String error;
        MakeLayout(Span<const VertexAttributeDescriptor>(rest.Data(), rest.Size()), layout, error);
        Relayout(layout, vertexCount_);
    }

    namespace
    {
        template <class T, u32 N>
        Span<const f32> Floats(Span<const T> values)
        {
            static_assert(sizeof(T) == N * sizeof(f32));
            return Span<const f32>(reinterpret_cast<const f32*>(values.Data()), values.Size() * N);
        }

        template <class T>
        void Resize(Array<T>& out, usize n)
        {
            out.Clear();
            out.Resize(n);
        }
    }

    bool Mesh::SetVertices(Span<const Vector3> vertices)
    {
        if (!Readable("vertices"))
            return false;
        u32 n = u32(vertices.Size());
        for (const SubMeshDescriptor& s : subMeshes_)
            if (s.indexCount > 0 && MaxIndex(s) >= n)
            {
                HX_LOG_ERROR(MeshLog, "Mesh.vertices is too small on mesh '{}': the indices use {} vertices, {} given.", name_, MaxIndex(s) + 1, n);
                return false;
            }
        if (!HasVertexAttribute(VertexAttribute::Position))
        {
            Array<VertexAttributeDescriptor> attributes;
            GetVertexAttributes(attributes);
            attributes.Push(VertexAttributeDescriptor(VertexAttribute::Position));
            Layout layout;
            String error;
            MakeLayout(Span<const VertexAttributeDescriptor>(attributes.Data(), attributes.Size()), layout, error);
            Relayout(layout, n);
        }
        else if (n != vertexCount_)
        {
            Layout layout;
            CurrentLayout(layout);
            Relayout(layout, n);
        }
        if (n && !SetAttribute(VertexAttribute::Position, &vertices.Data()->x, n, 3, VertexAttributeFormat::Float32, "vertices"))
            return false;
        RecalculateBounds();
        return true;
    }

#define HX_MESH_GETTER(Type, N, Attr, What)                                    \
    if (!Readable(What) || !HasVertexAttribute(VertexAttribute::Attr))         \
    {                                                                          \
        out.Clear();                                                           \
        return;                                                                \
    }                                                                          \
    Resize(out, vertexCount_);                                                 \
    GetAttribute(VertexAttribute::Attr, reinterpret_cast<f32*>(out.Data()), N)

    void Mesh::GetVertices(Array<Vector3>& out) const { HX_MESH_GETTER(Vector3, 3, Position, "vertices"); }
    Array<Vector3> Mesh::vertices() const
    {
        Array<Vector3> out;
        GetVertices(out);
        return out;
    }

    bool Mesh::SetNormals(Span<const Vector3> normals)
    {
        Span<const f32> f = Floats<Vector3, 3>(normals);
        return SetAttribute(VertexAttribute::Normal, f.Data(), normals.Size(), 3, VertexAttributeFormat::Float32, "normals");
    }
    void Mesh::GetNormals(Array<Vector3>& out) const { HX_MESH_GETTER(Vector3, 3, Normal, "normals"); }
    Array<Vector3> Mesh::normals() const
    {
        Array<Vector3> out;
        GetNormals(out);
        return out;
    }

    bool Mesh::SetTangents(Span<const Vector4> tangents)
    {
        Span<const f32> f = Floats<Vector4, 4>(tangents);
        return SetAttribute(VertexAttribute::Tangent, f.Data(), tangents.Size(), 4, VertexAttributeFormat::Float32, "tangents");
    }
    void Mesh::GetTangents(Array<Vector4>& out) const { HX_MESH_GETTER(Vector4, 4, Tangent, "tangents"); }
    Array<Vector4> Mesh::tangents() const
    {
        Array<Vector4> out;
        GetTangents(out);
        return out;
    }

    bool Mesh::SetColors(Span<const Color> colors)
    {
        Span<const f32> f = Floats<Color, 4>(colors);
        return SetAttribute(VertexAttribute::Color, f.Data(), colors.Size(), 4, VertexAttributeFormat::Float32, "colors");
    }
    void Mesh::GetColors(Array<Color>& out) const { HX_MESH_GETTER(Color, 4, Color, "colors"); }
    Array<Color> Mesh::colors() const
    {
        Array<Color> out;
        GetColors(out);
        return out;
    }

    bool Mesh::SetColors(Span<const Color32> colors)
    {
        Array<f32> f;
        f.Resize(colors.Size() * 4);
        for (usize i = 0; i < colors.Size(); i++)
        {
            f[i * 4 + 0] = colors[i].r / 255.0f;
            f[i * 4 + 1] = colors[i].g / 255.0f;
            f[i * 4 + 2] = colors[i].b / 255.0f;
            f[i * 4 + 3] = colors[i].a / 255.0f;
        }
        return SetAttribute(VertexAttribute::Color, f.Data(), colors.Size(), 4, VertexAttributeFormat::UNorm8, "colors32");
    }

    void Mesh::GetColors(Array<Color32>& out) const
    {
        Array<Color> c;
        GetColors(c);
        out.Clear();
        out.Resize(c.Size());
        for (usize i = 0; i < c.Size(); i++)
            out[i] = Color32(Round<u8>(Clamp(c[i].r, 0, 1) * 255.0f), Round<u8>(Clamp(c[i].g, 0, 1) * 255.0f), Round<u8>(Clamp(c[i].b, 0, 1) * 255.0f),
                             Round<u8>(Clamp(c[i].a, 0, 1) * 255.0f));
    }
    Array<Color32> Mesh::colors32() const
    {
        Array<Color32> out;
        GetColors(out);
        return out;
    }

    namespace
    {
        bool UvChannel(i32 channel)
        {
            if (channel >= 0 && channel < 8)
                return true;
            HX_LOG_ERROR(MeshLog, "UV channel {} does not exist (0 to 7).", channel);
            return false;
        }
    }

    bool Mesh::SetUVs(i32 channel, Span<const Vector2> uvs)
    {
        return UvChannel(channel) && SetAttribute(VertexAttribute(i32(VertexAttribute::TexCoord0) + channel), reinterpret_cast<const f32*>(uvs.Data()), uvs.Size(), 2,
                                                  VertexAttributeFormat::Float32, "uv");
    }
    bool Mesh::SetUVs(i32 channel, Span<const Vector3> uvs)
    {
        return UvChannel(channel) && SetAttribute(VertexAttribute(i32(VertexAttribute::TexCoord0) + channel), reinterpret_cast<const f32*>(uvs.Data()), uvs.Size(), 3,
                                                  VertexAttributeFormat::Float32, "uv");
    }
    bool Mesh::SetUVs(i32 channel, Span<const Vector4> uvs)
    {
        return UvChannel(channel) && SetAttribute(VertexAttribute(i32(VertexAttribute::TexCoord0) + channel), reinterpret_cast<const f32*>(uvs.Data()), uvs.Size(), 4,
                                                  VertexAttributeFormat::Float32, "uv");
    }

#define HX_MESH_UV_GETTER(Type, N)                                                        \
    out.Clear();                                                                          \
    if (!UvChannel(channel) || !Readable("uv"))                                           \
        return;                                                                           \
    VertexAttribute a = VertexAttribute(i32(VertexAttribute::TexCoord0) + channel);       \
    if (!HasVertexAttribute(a))                                                           \
        return;                                                                           \
    Resize(out, vertexCount_);                                                            \
    GetAttribute(a, reinterpret_cast<f32*>(out.Data()), N)

    void Mesh::GetUVs(i32 channel, Array<Vector2>& out) const { HX_MESH_UV_GETTER(Vector2, 2); }
    void Mesh::GetUVs(i32 channel, Array<Vector3>& out) const { HX_MESH_UV_GETTER(Vector3, 3); }
    void Mesh::GetUVs(i32 channel, Array<Vector4>& out) const { HX_MESH_UV_GETTER(Vector4, 4); }
    Array<Vector2> Mesh::uv(i32 channel) const
    {
        Array<Vector2> out;
        GetUVs(channel, out);
        return out;
    }

    // ------------------------------------------------------------------------------------------------- indices

    u32 Mesh::IndexAt(usize i) const
    {
        const u8* p = indexData_.Data();
        return indexFormat_ == IndexFormat::UInt16 ? Load<u16>(p + i * 2) : Load<u32>(p + i * 4);
    }

    u32 Mesh::MaxIndex(const SubMeshDescriptor& s) const
    {
        u32 m = 0;
        ForIndices(indexData_.Data(), indexFormat_, usize(s.indexStart), usize(s.indexCount), [&](u32 v) { m = v > m ? v : m; });
        return u32(i64(m) + s.baseVertex);
    }

    void Mesh::SubMeshBounds(SubMeshDescriptor& s) const
    {
        s.bounds = Bounds();
        s.firstVertex = 0;
        s.vertexCount = 0;
        if (s.indexCount <= 0)
            return;
        u32 lo = ~0u, hi = 0;
        ForIndices(indexData_.Data(), indexFormat_, usize(s.indexStart), usize(s.indexCount), [&](u32 v) {
            lo = v < lo ? v : lo;
            hi = v > hi ? v : hi;
        });
        lo = u32(i64(lo) + s.baseVertex);
        hi = u32(i64(hi) + s.baseVertex);
        s.firstVertex = i32(lo);
        s.vertexCount = i32(hi - lo + 1);
        if (!HasVertexAttribute(VertexAttribute::Position) || hi >= vertexCount_)
            return;
        Array<f32> scratch;
        usize stride = 0;
        const f32* p = FloatAttribute(VertexAttribute::Position, scratch, stride);
        f32 mn[3] = {INFINITY, INFINITY, INFINITY}, mx[3] = {-INFINITY, -INFINITY, -INFINITY};
        i64 base = s.baseVertex;
        ForIndices(indexData_.Data(), indexFormat_, usize(s.indexStart), usize(s.indexCount), [&](u32 v) {
            const f32* q = p + usize(i64(v) + base) * stride;
            for (u32 k = 0; k < 3; k++)
            {
                mn[k] = q[k] < mn[k] ? q[k] : mn[k];
                mx[k] = q[k] > mx[k] ? q[k] : mx[k];
            }
        });
        s.bounds.SetMinMax(Vector3(mn[0], mn[1], mn[2]), Vector3(mx[0], mx[1], mx[2]));
    }

    bool Mesh::SetIndices(Span<const i32> indices, MeshTopology topology, i32 submesh, bool calculateBounds, i32 baseVertex)
    {
        if (!Readable("indices"))
            return false;
        if (submesh < 0 || submesh >= i32(subMeshes_.Size()))
        {
            if (submesh == 0 && subMeshes_.Empty())
                subMeshes_.Emplace();
            else
            {
                HX_LOG_ERROR(MeshLog, "Failed setting indices on mesh '{}': submesh {} of {} does not exist.", name_, submesh, subMeshes_.Size());
                return false;
            }
        }
        u32 per = topology == MeshTopology::Triangles ? 3 : topology == MeshTopology::Quads ? 4 : topology == MeshTopology::Lines ? 2 : 1;
        if (indices.Size() % per != 0)
        {
            HX_LOG_ERROR(MeshLog, "Failed setting indices on mesh '{}': {} indices are not a whole number of primitives.", name_, indices.Size());
            return false;
        }
        i64 hi = 0, lo = 0;
        for (i32 i : indices)
        {
            hi = i > hi ? i : hi;
            lo = i < lo ? i : lo;
        }
        if (!indices.Empty() && (lo < 0 || hi + baseVertex >= i64(vertexCount_) || lo + baseVertex < 0))
        {
            HX_LOG_ERROR(MeshLog, "Failed setting indices on mesh '{}': index {} is outside the {} vertices.", name_,
                         lo < 0 || lo + baseVertex < 0 ? lo + baseVertex : hi + baseVertex, vertexCount_);
            return false;
        }
        if (indexFormat_ == IndexFormat::UInt16 && hi > 0xFFFF)
        {
            HX_LOG_ERROR(MeshLog, "Failed setting indices on mesh '{}': index {} needs the 32 bit index format.", name_, hi);
            return false;
        }
        u32 size = IndexSize();
        Array<u8> data;
        usize total = 0;
        for (usize k = 0; k < subMeshes_.Size(); k++)
            total += k == usize(submesh) ? indices.Size() : usize(subMeshes_[k].indexCount);
        data.Resize(total * size);
        usize at = 0;
        for (usize k = 0; k < subMeshes_.Size(); k++)
        {
            SubMeshDescriptor& s = subMeshes_[k];
            if (k == usize(submesh))
            {
                if (size == 4)
                    std::memcpy(data.Data() + at * 4, indices.Data(), indices.Size() * 4);
                else
                    for (usize i = 0; i < indices.Size(); i++)
                        Store(data.Data() + (at + i) * 2, u16(indices[i]));
                s.indexStart = i32(at);
                s.indexCount = i32(indices.Size());
                s.topology = topology;
                s.baseVertex = baseVertex;
            }
            else
            {
                if (s.indexCount)
                    std::memcpy(data.Data() + at * size, indexData_.Data() + usize(s.indexStart) * size, usize(s.indexCount) * size);
                s.indexStart = i32(at);
            }
            at += usize(s.indexCount);
        }
        indexData_ = Move(data);
        Bounds keep = subMeshes_[usize(submesh)].bounds;
        SubMeshBounds(subMeshes_[usize(submesh)]);
        // The other submeshes keep theirs: the mesh's bounds are their union.
        if (calculateBounds)
            UnionBounds();
        else
            subMeshes_[usize(submesh)].bounds = keep;
        version_++;
        return true;
    }

    bool Mesh::SetTriangles(Span<const i32> triangles)
    {
        if (!Readable("triangles"))
            return false;
        SetSubMeshCount(1);
        return SetIndices(triangles, MeshTopology::Triangles, 0, true, 0);
    }

    bool Mesh::SetTriangles(Span<const i32> triangles, i32 submesh, bool calculateBounds, i32 baseVertex)
    {
        return SetIndices(triangles, MeshTopology::Triangles, submesh, calculateBounds, baseVertex);
    }

    void Mesh::GetIndices(Array<i32>& out, i32 submesh, bool applyBaseVertex) const
    {
        out.Clear();
        if (!Readable("indices"))
            return;
        if (submesh < 0 || submesh >= i32(subMeshes_.Size()))
        {
            HX_LOG_ERROR(MeshLog, "Failed getting indices on mesh '{}': submesh {} of {} does not exist.", name_, submesh, subMeshes_.Size());
            return;
        }
        const SubMeshDescriptor& s = subMeshes_[usize(submesh)];
        out.Resize(usize(s.indexCount));
        i32 base = applyBaseVertex ? s.baseVertex : 0;
        for (i32 i = 0; i < s.indexCount; i++)
            out[usize(i)] = i32(IndexAt(usize(s.indexStart + i))) + base;
    }

    Array<i32> Mesh::GetIndices(i32 submesh, bool applyBaseVertex) const
    {
        Array<i32> out;
        GetIndices(out, submesh, applyBaseVertex);
        return out;
    }

    void Mesh::GetTriangles(Array<i32>& out, i32 submesh, bool applyBaseVertex) const
    {
        out.Clear();
        if (submesh >= 0 && submesh < i32(subMeshes_.Size()) && subMeshes_[usize(submesh)].topology != MeshTopology::Triangles)
        {
            HX_LOG_ERROR(MeshLog, "Failed getting triangles on mesh '{}': submesh {} is not made of triangles.", name_, submesh);
            return;
        }
        GetIndices(out, submesh, applyBaseVertex);
    }

    Array<i32> Mesh::GetTriangles(i32 submesh, bool applyBaseVertex) const
    {
        Array<i32> out;
        GetTriangles(out, submesh, applyBaseVertex);
        return out;
    }

    Array<i32> Mesh::triangles() const
    {
        Array<i32> out;
        if (!Readable("triangles"))
            return out;
        for (const SubMeshDescriptor& s : subMeshes_)
            if (s.topology == MeshTopology::Triangles)
                for (i32 i = 0; i < s.indexCount; i++)
                    out.Push(i32(IndexAt(usize(s.indexStart + i))) + s.baseVertex);
        return out;
    }

    u32 Mesh::GetIndexStart(i32 submesh) const { return submesh >= 0 && submesh < i32(subMeshes_.Size()) ? u32(subMeshes_[usize(submesh)].indexStart) : 0; }
    u32 Mesh::GetIndexCount(i32 submesh) const { return submesh >= 0 && submesh < i32(subMeshes_.Size()) ? u32(subMeshes_[usize(submesh)].indexCount) : 0; }
    u32 Mesh::GetBaseVertex(i32 submesh) const { return submesh >= 0 && submesh < i32(subMeshes_.Size()) ? u32(subMeshes_[usize(submesh)].baseVertex) : 0; }
    MeshTopology Mesh::GetTopology(i32 submesh) const
    {
        return submesh >= 0 && submesh < i32(subMeshes_.Size()) ? subMeshes_[usize(submesh)].topology : MeshTopology::Triangles;
    }

    void Mesh::SetSubMeshCount(i32 count)
    {
        if (count < 0)
            count = 0;
        if (usize(count) < subMeshes_.Size())
        {
            usize end = 0;
            for (i32 k = 0; k < count; k++)
                end = usize(subMeshes_[usize(k)].indexStart + subMeshes_[usize(k)].indexCount) > end ? usize(subMeshes_[usize(k)].indexStart + subMeshes_[usize(k)].indexCount) : end;
            subMeshes_.Resize(usize(count));
            indexData_.Resize(end * IndexSize());
        }
        else
            while (subMeshes_.Size() < usize(count))
            {
                SubMeshDescriptor& s = subMeshes_.Emplace();
                s.indexStart = i32(IndexCount());
            }
        version_++;
    }

    bool Mesh::SetIndexFormat(IndexFormat format)
    {
        if (format == indexFormat_)
            return true;
        usize n = IndexCount();
        Array<u8> data;
        data.Resize(n * (format == IndexFormat::UInt16 ? 2 : 4));
        for (usize i = 0; i < n; i++)
        {
            u32 v = IndexAt(i);
            if (format == IndexFormat::UInt16)
            {
                if (v > 0xFFFF)
                {
                    HX_LOG_ERROR(MeshLog, "Mesh '{}' keeps 32 bit indices: index {} does not fit in 16 bits.", name_, v);
                    return false;
                }
                Store(data.Data() + i * 2, u16(v));
            }
            else
                Store(data.Data() + i * 4, v);
        }
        indexData_ = Move(data);
        indexFormat_ = format;
        version_++;
        return true;
    }

    // ------------------------------------------------------------------------------------------------- advanced API

    bool Mesh::SetVertexBufferParams(i32 vertexCount, Span<const VertexAttributeDescriptor> attributes)
    {
        Layout layout;
        String error;
        if (vertexCount < 0)
            error = Format("the vertex count {} is negative", vertexCount);
        if (!error.Empty() || !MakeLayout(attributes, layout, error))
        {
            HX_LOG_ERROR(MeshLog, "SetVertexBufferParams on mesh '{}': {}.", name_, error);
            return false;
        }
        Relayout(layout, u32(vertexCount));
        return true;
    }

    bool Mesh::SetVertexBufferBytes(const u8* data, usize size, usize from, usize to, usize bytes, i32 stream, MeshUpdateFlags)
    {
        if (stream < 0 || stream >= i32(kMaxVertexStreams) || strides_[stream] == 0)
        {
            HX_LOG_ERROR(MeshLog, "SetVertexBufferData on mesh '{}': stream {} is not in the layout.", name_, stream);
            return false;
        }
        usize streamSize = usize(strides_[stream]) * vertexCount_;
        if (from > size || bytes > size - from || to > streamSize || bytes > streamSize - to)
        {
            HX_LOG_ERROR(MeshLog, "SetVertexBufferData on mesh '{}': the range is outside the data or the stream.", name_);
            return false;
        }
        if (bytes)
            std::memcpy(vertexData_.Data() + StreamOffset(u32(stream)) + to, data + from, bytes);
        version_++;
        return true;
    }

    bool Mesh::SetIndexBufferParams(i32 indexCount, IndexFormat format)
    {
        if (indexCount < 0 || (format != IndexFormat::UInt16 && format != IndexFormat::UInt32))
        {
            HX_LOG_ERROR(MeshLog, "SetIndexBufferParams on mesh '{}': {} indices of format {}.", name_, indexCount, i32(format));
            return false;
        }
        if (format != indexFormat_)
        {
            indexData_.Clear();
            indexFormat_ = format;
        }
        usize old = indexData_.Size();
        indexData_.Resize(usize(indexCount) * IndexSize());
        if (indexData_.Size() > old)
            std::memset(indexData_.Data() + old, 0, indexData_.Size() - old);
        version_++;
        return true;
    }

    bool Mesh::SetIndexBufferBytes(const u8* data, usize size, usize from, usize to, usize bytes, MeshUpdateFlags)
    {
        if (from > size || bytes > size - from || to > indexData_.Size() || bytes > indexData_.Size() - to)
        {
            HX_LOG_ERROR(MeshLog, "SetIndexBufferData on mesh '{}': the range is outside the data or the index buffer.", name_);
            return false;
        }
        if (bytes)
            std::memcpy(indexData_.Data() + to, data + from, bytes);
        version_++;
        return true;
    }

    bool Mesh::SetSubMesh(i32 index, const SubMeshDescriptor& descriptor, MeshUpdateFlags flags)
    {
        if (index < 0 || index >= i32(subMeshes_.Size()))
        {
            HX_LOG_ERROR(MeshLog, "SetSubMesh on mesh '{}': submesh {} of {} does not exist.", name_, index, subMeshes_.Size());
            return false;
        }
        SubMeshDescriptor s = descriptor;
        if (s.indexStart < 0 || s.indexCount < 0 || usize(s.indexStart) + usize(s.indexCount) > IndexCount())
        {
            HX_LOG_ERROR(MeshLog, "SetSubMesh on mesh '{}': indices {} to {} are outside the {} of the index buffer.", name_, s.indexStart,
                         i64(s.indexStart) + s.indexCount, IndexCount());
            return false;
        }
        if (!Has(flags, MeshUpdateFlags::DontValidateIndices))
            for (i32 i = 0; i < s.indexCount; i++)
            {
                i64 v = i64(IndexAt(usize(s.indexStart + i))) + s.baseVertex;
                if (v < 0 || v >= i64(vertexCount_))
                {
                    HX_LOG_ERROR(MeshLog, "SetSubMesh on mesh '{}': index {} is outside the {} vertices.", name_, v, vertexCount_);
                    return false;
                }
            }
        if (!Has(flags, MeshUpdateFlags::DontRecalculateBounds))
            SubMeshBounds(s);
        subMeshes_[usize(index)] = s;
        if (!Has(flags, MeshUpdateFlags::DontRecalculateBounds))
        {
            bool any = false;
            for (const SubMeshDescriptor& d : subMeshes_)
                if (d.indexCount > 0)
                {
                    if (!any)
                        bounds_ = d.bounds;
                    else
                        bounds_.Encapsulate(d.bounds);
                    any = true;
                }
        }
        version_++;
        return true;
    }

    bool Mesh::SetSubMeshes(Span<const SubMeshDescriptor> descriptors, MeshUpdateFlags flags)
    {
        subMeshes_.Clear();
        subMeshes_.Resize(descriptors.Size());
        bool ok = true;
        for (usize i = 0; i < descriptors.Size(); i++)
            ok = SetSubMesh(i32(i), descriptors[i], flags) && ok;
        return ok;
    }

    SubMeshDescriptor Mesh::GetSubMesh(i32 index) const
    {
        if (index < 0 || index >= i32(subMeshes_.Size()))
        {
            HX_LOG_ERROR(MeshLog, "GetSubMesh on mesh '{}': submesh {} of {} does not exist.", name_, index, subMeshes_.Size());
            return SubMeshDescriptor();
        }
        return subMeshes_[usize(index)];
    }

    i32 Mesh::vertexBufferCount() const
    {
        i32 n = 0;
        for (u32 s = 0; s < kMaxVertexStreams; s++)
            if (strides_[s])
                n = i32(s) + 1;
        return n;
    }

    i32 Mesh::GetVertexBufferStride(i32 stream) const { return stream >= 0 && stream < i32(kMaxVertexStreams) ? i32(strides_[stream]) : 0; }

    i32 Mesh::vertexAttributeCount() const
    {
        i32 n = 0;
        for (const Channel& c : channels_)
            n += c.stream != kNoStream;
        return n;
    }

    void Mesh::GetVertexAttributes(Array<VertexAttributeDescriptor>& out) const
    {
        out.Clear();
        for (u32 a = 0; a < kVertexAttributeCount; a++)
            if (channels_[a].stream != kNoStream)
                out.Push(VertexAttributeDescriptor(VertexAttribute(a), VertexAttributeFormat(channels_[a].format), channels_[a].dimension, channels_[a].stream));
    }

    Array<VertexAttributeDescriptor> Mesh::GetVertexAttributes() const
    {
        Array<VertexAttributeDescriptor> out;
        GetVertexAttributes(out);
        return out;
    }

    VertexAttributeDescriptor Mesh::GetVertexAttribute(i32 index) const
    {
        i32 n = 0;
        for (u32 a = 0; a < kVertexAttributeCount; a++)
            if (channels_[a].stream != kNoStream && n++ == index)
                return VertexAttributeDescriptor(VertexAttribute(a), VertexAttributeFormat(channels_[a].format), channels_[a].dimension, channels_[a].stream);
        HX_LOG_ERROR(MeshLog, "GetVertexAttribute on mesh '{}': attribute {} of {} does not exist.", name_, index, n);
        return VertexAttributeDescriptor();
    }

    i32 Mesh::GetVertexAttributeDimension(VertexAttribute a) const { return HasVertexAttribute(a) ? channels_[u32(a)].dimension : -1; }
    VertexAttributeFormat Mesh::GetVertexAttributeFormat(VertexAttribute a) const
    {
        return HasVertexAttribute(a) ? VertexAttributeFormat(channels_[u32(a)].format) : VertexAttributeFormat::Float32;
    }
    i32 Mesh::GetVertexAttributeStream(VertexAttribute a) const { return HasVertexAttribute(a) ? channels_[u32(a)].stream : -1; }
    i32 Mesh::GetVertexAttributeOffset(VertexAttribute a) const { return HasVertexAttribute(a) ? channels_[u32(a)].offset : -1; }

    Span<const u8> Mesh::VertexData(i32 stream) const
    {
        if (stream < 0 || stream >= i32(kMaxVertexStreams) || vertexData_.Empty())
            return {};
        return Span<const u8>(vertexData_.Data() + StreamOffset(u32(stream)), usize(strides_[stream]) * vertexCount_);
    }

    // ------------------------------------------------------------------------------------------------- recalculation

    void Mesh::RecalculateBounds(MeshUpdateFlags)
    {
        for (SubMeshDescriptor& s : subMeshes_)
            SubMeshBounds(s);
        UnionBounds();
    }

    void Mesh::UnionBounds()
    {
        bool any = false;
        for (const SubMeshDescriptor& s : subMeshes_)
        {
            if (s.indexCount <= 0)
                continue;
            if (!any)
                bounds_ = s.bounds;
            else
                bounds_.Encapsulate(s.bounds);
            any = true;
        }
        if (any)
            return;
        // Without indices: every vertex.
        bounds_ = Bounds();
        if (!HasVertexAttribute(VertexAttribute::Position) || vertexCount_ == 0)
            return;
        Array<f32> scratch;
        usize stride = 0;
        const f32* p = FloatAttribute(VertexAttribute::Position, scratch, stride);
        f32 mn[3] = {p[0], p[1], p[2]}, mx[3] = {p[0], p[1], p[2]};
        for (u32 v = 0; v < vertexCount_; v++)
        {
            const f32* q = p + usize(v) * stride;
            for (u32 k = 0; k < 3; k++)
            {
                mn[k] = q[k] < mn[k] ? q[k] : mn[k];
                mx[k] = q[k] > mx[k] ? q[k] : mx[k];
            }
        }
        bounds_.SetMinMax(Vector3(mn[0], mn[1], mn[2]), Vector3(mx[0], mx[1], mx[2]));
    }

    void Mesh::RecalculateNormals(MeshUpdateFlags)
    {
        if (!Readable("normals") || !HasVertexAttribute(VertexAttribute::Position))
            return;
        Array<f32> scratch;
        usize stride = 0;
        const f32* p = FloatAttribute(VertexAttribute::Position, scratch, stride);
        Array<f32> n;
        n.Resize(usize(vertexCount_) * 3);
        if (!n.Empty())
            std::memset(n.Data(), 0, n.Size() * sizeof(f32));
        u64 count = vertexCount_;
        for (const SubMeshDescriptor& s : subMeshes_)
            ForTriangles(indexData_.Data(), indexFormat_, s, [&](i64 a, i64 b, i64 c) {
                if (u64(a) >= count || u64(b) >= count || u64(c) >= count)
                    return;
                const f32* pa = p + usize(a) * stride;
                const f32* pb = p + usize(b) * stride;
                const f32* pc = p + usize(c) * stride;
                f32 e1[3] = {pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]};
                f32 e2[3] = {pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]};
                // Not normalized: larger faces weigh more.
                f32 f[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
                for (i64 v : {a, b, c})
                {
                    f32* o = n.Data() + usize(v) * 3;
                    o[0] += f[0], o[1] += f[1], o[2] += f[2];
                }
            });
        for (usize v = 0; v < vertexCount_; v++)
        {
            f32* o = n.Data() + v * 3;
            f32 l = o[0] * o[0] + o[1] * o[1] + o[2] * o[2];
            f32 inv = l > 1e-40f ? 1.0f / std::sqrt(l) : 0.0f;
            o[0] *= inv, o[1] *= inv, o[2] *= inv;
        }
        SetAttribute(VertexAttribute::Normal, n.Data(), vertexCount_, 3, VertexAttributeFormat::Float32, "normals");
    }

    void Mesh::RecalculateTangents(MeshUpdateFlags)
    {
        if (!Readable("tangents"))
            return;
        if (!HasVertexAttribute(VertexAttribute::Position) || !HasVertexAttribute(VertexAttribute::Normal) ||
            GetVertexAttributeDimension(VertexAttribute::TexCoord0) < 2)
        {
            HX_LOG_ERROR(MeshLog, "RecalculateTangents on mesh '{}' needs positions, normals and uv set 0.", name_);
            return;
        }
        usize count = vertexCount_;
        Array<Vector3> p, n, t1, t2;
        Array<Vector2> uv;
        p.Resize(count), n.Resize(count), t1.Resize(count), t2.Resize(count), uv.Resize(count);
        GetAttribute(VertexAttribute::Position, &p.Data()->x, 3);
        GetAttribute(VertexAttribute::Normal, &n.Data()->x, 3);
        GetAttribute(VertexAttribute::TexCoord0, &uv.Data()->x, 2);
        for (usize i = 0; i < count; i++)
            t1[i] = t2[i] = Vector3();
        for (const SubMeshDescriptor& s : subMeshes_)
            ForTriangles(indexData_.Data(), indexFormat_, s, [&](i64 a, i64 b, i64 c) {
                if (u64(a) >= count || u64(b) >= count || u64(c) >= count)
                    return;
                Vector3 e1 = Sub(p[usize(b)], p[usize(a)]), e2 = Sub(p[usize(c)], p[usize(a)]);
                f32 s1 = uv[usize(b)].x - uv[usize(a)].x, s2 = uv[usize(c)].x - uv[usize(a)].x;
                f32 u1 = uv[usize(b)].y - uv[usize(a)].y, u2 = uv[usize(c)].y - uv[usize(a)].y;
                f32 d = s1 * u2 - s2 * u1;
                if (std::fabs(d) < 1e-20f)
                    return;
                f32 r = 1.0f / d;
                Vector3 sd((u2 * e1.x - u1 * e2.x) * r, (u2 * e1.y - u1 * e2.y) * r, (u2 * e1.z - u1 * e2.z) * r);
                Vector3 td((s1 * e2.x - s2 * e1.x) * r, (s1 * e2.y - s2 * e1.y) * r, (s1 * e2.z - s2 * e1.z) * r);
                for (i64 v : {a, b, c})
                {
                    t1[usize(v)] = Vector3(t1[usize(v)].x + sd.x, t1[usize(v)].y + sd.y, t1[usize(v)].z + sd.z);
                    t2[usize(v)] = Vector3(t2[usize(v)].x + td.x, t2[usize(v)].y + td.y, t2[usize(v)].z + td.z);
                }
            });
        Array<Vector4> t;
        t.Resize(count);
        for (usize i = 0; i < count; i++)
        {
            Vector3 nn = n[i];
            f32 d = Dot(nn, t1[i]);
            Vector3 tt = Normalized(Vector3(t1[i].x - nn.x * d, t1[i].y - nn.y * d, t1[i].z - nn.z * d));
            f32 w = Dot(Cross(nn, tt), t2[i]) < 0 ? -1.0f : 1.0f;
            t[i] = Vector4(tt.x, tt.y, tt.z, w);
        }
        SetAttribute(VertexAttribute::Tangent, &t.Data()->x, count, 4, VertexAttributeFormat::Float32, "tangents");
    }

    void Mesh::Clear(bool keepVertexLayout)
    {
        vertexCount_ = 0;
        vertexData_.Clear();
        indexData_.Clear();
        subMeshes_.Clear();
        bounds_ = Bounds();
        if (!keepVertexLayout)
        {
            for (Channel& c : channels_)
                c = Channel();
            for (u32& s : strides_)
                s = 0;
        }
        version_++;
    }

    void Mesh::UploadMeshData(bool markNoLongerReadable)
    {
        version_++;
        if (markNoLongerReadable)
            readable_ = false;
    }

    void Mesh::ReleaseCpuData()
    {
        if (readable_)
            return;
        vertexData_ = Array<u8>();
        indexData_ = Array<u8>();
    }

    bool Mesh::SetMeshData(u32 vertexCount, Span<const VertexAttributeDescriptor> layout, Array<u8>&& vertexData, IndexFormat indexFormat,
                           Array<u8>&& indexData, Array<SubMeshDescriptor>&& subMeshes, const Bounds& bounds)
    {
        Layout l;
        String error;
        if (!MakeLayout(layout, l, error))
        {
            HX_LOG_ERROR(MeshLog, "SetMeshData on mesh '{}': {}.", name_, error);
            return false;
        }
        usize size = 0;
        for (u32 s = 0; s < kMaxVertexStreams; s++)
            size += usize(l.strides[s]) * vertexCount;
        usize indexSize = indexFormat == IndexFormat::UInt16 ? 2 : 4;
        usize indexCount = indexData.Size() / indexSize;
        bool ok = vertexData.Size() == size && indexData.Size() % indexSize == 0;
        for (const SubMeshDescriptor& s : subMeshes)
            ok = ok && s.indexStart >= 0 && s.indexCount >= 0 && usize(s.indexStart) + usize(s.indexCount) <= indexCount;
        if (!ok)
        {
            HX_LOG_ERROR(MeshLog, "SetMeshData on mesh '{}': the blocks do not match the layout and the submeshes.", name_);
            return false;
        }
        for (u32 i = 0; i < kVertexAttributeCount; i++)
            channels_[i] = l.channels[i];
        for (u32 s = 0; s < kMaxVertexStreams; s++)
            strides_[s] = l.strides[s];
        vertexCount_ = vertexCount;
        vertexData_ = Move(vertexData);
        indexFormat_ = indexFormat;
        indexData_ = Move(indexData);
        subMeshes_ = Move(subMeshes);
        bounds_ = bounds;
        version_++;
        return true;
    }

    void Mesh::Loaded(u32 vertexCount, const Array<VertexAttributeDescriptor>& attributes)
    {
        Layout layout;
        String error;
        bool ok = MakeLayout(Span<const VertexAttributeDescriptor>(attributes.Data(), attributes.Size()), layout, error);
        usize size = 0;
        for (u32 s = 0; s < kMaxVertexStreams; s++)
            size += usize(layout.strides[s]) * vertexCount;
        if (ok && vertexData_.Size() != size)
            ok = false, error = Format("{} bytes of vertex data for {} vertices of {} bytes", vertexData_.Size(), vertexCount, size);
        if (ok && indexFormat_ != IndexFormat::UInt16 && indexFormat_ != IndexFormat::UInt32)
            ok = false, error = Format("the index format {} does not exist", i32(indexFormat_));
        if (ok && indexData_.Size() % IndexSize() != 0)
            ok = false, error = "the index buffer is not a whole number of indices";
        if (ok)
            for (const SubMeshDescriptor& s : subMeshes_)
                if (s.indexStart < 0 || s.indexCount < 0 || usize(s.indexStart) + usize(s.indexCount) > indexData_.Size() / IndexSize())
                    ok = false, error = "a submesh is outside the index buffer";
        if (!ok)
        {
            HX_LOG_ERROR(MeshLog, "Mesh '{}' read from a file is damaged: {}.", name_, error);
            vertexData_.Clear();
            indexData_.Clear();
            subMeshes_.Clear();
            vertexCount = 0;
            layout = Layout();
        }
        for (u32 i = 0; i < kVertexAttributeCount; i++)
            channels_[i] = layout.channels[i];
        for (u32 s = 0; s < kMaxVertexStreams; s++)
            strides_[s] = layout.strides[s];
        vertexCount_ = vertexCount;
        version_++;
    }
}
