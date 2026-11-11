#include <Helix/Engine/Material/Material.h>
#include <Helix/Engine/Rendering/Graphics.h>
#include <Helix/Engine/Rendering/Implementation/RendererManager.h>
#include <Helix/Engine/Rendering/Light.h>
#include <Helix/Engine/Texture/Texture.h>

#include <Helix/Core/Hashing/Hash.h>
#include <Helix/Core/Logging/Log.h>
#include <Helix/Image/Texture/Convert.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace helix::engine
{
    using namespace shadercompiler;

    namespace
    {
        LogChannel RenderLog{"Engine"};
        constexpr u32 kNone = ~0u;

        struct GpuMesh
        {
            u32 version = ~0u;
            rhi::BufferHandle vertices;
            u32 offsets[kVertexAttributeCount];
            u32 strides[kVertexAttributeCount];
            rhi::VertexFormat formats[kVertexAttributeCount];
            rhi::BufferHandle indices;
            rhi::BufferHandle zeros;
            u32 vertexCount = 0;
            // Each submesh as the GPU draws it (quads made triangles); count 0 skips it.
            struct Sub
            {
                u32 start, count;
                i32 baseVertex;
                rhi::PrimitiveTopology topology;
            };
            Array<Sub> subs = Array<Sub>();
        };

        struct GpuTexture
        {
            u32 version = ~0u;
            rhi::TextureHandle texture;
            rhi::SamplerHandle sampler;
        };

        struct GpuShader
        {
            rhi::ShaderHandle vertex, fragment;
        };

        struct RenderState
        {
            rhi::Device* device = nullptr;
            RenderTarget target;
            Color ambient{0.2f, 0.2f, 0.2f, 1};
            HashMap<EntityId, GpuMesh> meshes;
            HashMap<EntityId, GpuTexture> textures;
            HashMap<u32, GpuShader> shaders;
            HashMap<u64, rhi::PipelineHandle> pipelines;
            RenderStats stats;
            EntityId errorMaterial;
            u32 frame = 0;
        };

        RenderState& R()
        {
            static RenderState s;
            return s;
        }

        // ------------------------------------------------------------------------------------------------ meshes

        const GpuMesh* MeshOf(Mesh& mesh)
        {
            RenderState& s = R();
            GpuMesh* g = s.meshes.Find(mesh.GetEntityId());
            if (g && g->version == mesh.Version())
                return g;
            if (!g)
                g = &s.meshes.Set(mesh.GetEntityId(), GpuMesh());
            s.device->Destroy(g->vertices);
            s.device->Destroy(g->indices);
            s.device->Destroy(g->zeros);
            *g = GpuMesh();
            g->version = mesh.Version();
            g->vertexCount = u32(mesh.vertexCount());
            if (mesh.Version() != 0 && mesh.VertexData(0).Empty() && mesh.vertexCount() > 0)
                return g;
            // Every attribute packed on its own: floats, or four bytes for 8 bit colors.
            Array<u8> data;
            Array<f32> floats;
            for (u32 a = 0; a < kVertexAttributeCount; a++)
            {
                g->offsets[a] = kNone;
                VertexAttribute attribute = VertexAttribute(a);
                if (!mesh.HasVertexAttribute(attribute) || attribute == VertexAttribute::BlendIndices)
                    continue;
                u32 dim = u32(mesh.GetVertexAttributeDimension(attribute));
                usize at = data.Size();
                g->offsets[a] = u32(at);
                if (mesh.GetVertexAttributeFormat(attribute) == VertexAttributeFormat::UNorm8 && dim == 4)
                {
                    g->formats[a] = rhi::VertexFormat::UByte4Norm;
                    g->strides[a] = 4;
                    i32 stream = mesh.GetVertexAttributeStream(attribute), offset = mesh.GetVertexAttributeOffset(attribute);
                    Span<const u8> src = mesh.VertexData(stream);
                    u32 stride = u32(mesh.GetVertexBufferStride(stream));
                    data.Resize(at + usize(g->vertexCount) * 4);
                    for (u32 v = 0; v < g->vertexCount; v++)
                        std::memcpy(data.Data() + at + usize(v) * 4, src.Data() + usize(v) * stride + u32(offset), 4);
                    continue;
                }
                static constexpr rhi::VertexFormat kFloats[] = {rhi::VertexFormat::Float1, rhi::VertexFormat::Float2, rhi::VertexFormat::Float3,
                                                                rhi::VertexFormat::Float4};
                g->formats[a] = kFloats[dim - 1];
                g->strides[a] = dim * 4;
                floats.Resize(usize(g->vertexCount) * dim);
                mesh.CopyAttributeFloats(attribute, floats.Data(), dim);
                data.Resize(at + floats.Size() * 4);
                if (!floats.Empty())
                    std::memcpy(data.Data() + at, floats.Data(), floats.Size() * 4);
            }
            if (!data.Empty())
                g->vertices = s.device->CreateBuffer({.size = data.Size(), .usage = rhi::BufferVertex, .initialData = data.Data(), .debugName = mesh.Name()});
            // Attributes the mesh lacks read zero.
            Array<u8> zeros;
            zeros.Resize(usize(g->vertexCount > 0 ? g->vertexCount : 1) * 16);
            std::memset(zeros.Data(), 0, zeros.Size());
            g->zeros = s.device->CreateBuffer({.size = zeros.Size(), .usage = rhi::BufferVertex, .initialData = zeros.Data()});
            // Indices as 32 bit, quads split into triangles.
            Array<u32> indices;
            for (i32 k = 0; k < mesh.subMeshCount(); k++)
            {
                SubMeshDescriptor d = mesh.GetSubMesh(k);
                Array<i32> list;
                mesh.GetIndices(list, k, false);
                if (!mesh.isReadable())
                {
                    // Not readable: the index block as it is.
                    list.Clear();
                    Span<const u8> block = mesh.IndexData();
                    bool wide = mesh.indexFormat() == IndexFormat::UInt32;
                    for (i32 i = 0; i < d.indexCount; i++)
                    {
                        usize at = usize(d.indexStart + i) * (wide ? 4 : 2);
                        u32 v = 0;
                        std::memcpy(&v, block.Data() + at, wide ? 4 : 2);
                        list.Push(i32(v));
                    }
                }
                GpuMesh::Sub sub{u32(indices.Size()), 0, d.baseVertex, rhi::PrimitiveTopology::TriangleList};
                switch (d.topology)
                {
                case MeshTopology::Quads:
                    for (usize i = 0; i + 3 < list.Size(); i += 4)
                        for (usize c : {usize(0), usize(1), usize(2), usize(0), usize(2), usize(3)})
                            indices.Push(u32(list[i + c]));
                    break;
                case MeshTopology::Lines:
                    sub.topology = rhi::PrimitiveTopology::LineList;
                    break;
                case MeshTopology::LineStrip:
                    sub.topology = rhi::PrimitiveTopology::LineStrip;
                    break;
                case MeshTopology::Points:
                    sub.topology = rhi::PrimitiveTopology::PointList;
                    break;
                default:
                    break;
                }
                if (d.topology != MeshTopology::Quads)
                    for (i32 v : list)
                        indices.Push(u32(v));
                sub.count = u32(indices.Size()) - sub.start;
                g->subs.Push(sub);
            }
            if (!indices.Empty())
                g->indices = s.device->CreateBuffer({.size = indices.Size() * 4, .usage = rhi::BufferIndex, .initialData = indices.Data()});
            s.stats.uploads++;
            // The CPU copy of a mesh that is not readable goes once the GPU has it.
            if (!mesh.isReadable())
                mesh.ReleaseCpuData();
            return g;
        }

        // ------------------------------------------------------------------------------------------------ textures

        struct FormatPair
        {
            image::PixelFormat pixel;
            rhi::Format gpu;
            rhi::Format srgb;
        };
        constexpr FormatPair kTextureFormats[] = {
            {image::PixelFormat::RGBA8, rhi::Format::RGBA8Unorm, rhi::Format::RGBA8Srgb},
            {image::PixelFormat::BGRA8, rhi::Format::BGRA8Unorm, rhi::Format::BGRA8Srgb},
            {image::PixelFormat::R8, rhi::Format::R8Unorm, rhi::Format::R8Unorm},
            {image::PixelFormat::RG8, rhi::Format::RG8Unorm, rhi::Format::RG8Unorm},
            {image::PixelFormat::R16Float, rhi::Format::R16Float, rhi::Format::R16Float},
            {image::PixelFormat::RG16Float, rhi::Format::RG16Float, rhi::Format::RG16Float},
            {image::PixelFormat::RGBA16Float, rhi::Format::RGBA16Float, rhi::Format::RGBA16Float},
            {image::PixelFormat::R32Float, rhi::Format::R32Float, rhi::Format::R32Float},
            {image::PixelFormat::RG32Float, rhi::Format::RG32Float, rhi::Format::RG32Float},
            {image::PixelFormat::RGBA32Float, rhi::Format::RGBA32Float, rhi::Format::RGBA32Float},
            {image::PixelFormat::RG11B10Float, rhi::Format::RG11B10Float, rhi::Format::RG11B10Float},
            {image::PixelFormat::RGB10A2, rhi::Format::RGB10A2Unorm, rhi::Format::RGB10A2Unorm},
            {image::PixelFormat::BC1, rhi::Format::BC1Unorm, rhi::Format::BC1Unorm},
            {image::PixelFormat::BC2, rhi::Format::BC2Unorm, rhi::Format::BC2Unorm},
            {image::PixelFormat::BC3, rhi::Format::BC3Unorm, rhi::Format::BC3Unorm},
            {image::PixelFormat::BC4, rhi::Format::BC4Unorm, rhi::Format::BC4Unorm},
            {image::PixelFormat::BC5, rhi::Format::BC5Unorm, rhi::Format::BC5Unorm},
            {image::PixelFormat::BC6H, rhi::Format::BC6HUfloat, rhi::Format::BC6HUfloat},
            {image::PixelFormat::BC7, rhi::Format::BC7Unorm, rhi::Format::BC7Unorm},
            {image::PixelFormat::ETC2RGB, rhi::Format::ETC2RGB8, rhi::Format::ETC2RGB8},
            {image::PixelFormat::ETC2RGBA1, rhi::Format::ETC2RGB8A1, rhi::Format::ETC2RGB8A1},
            {image::PixelFormat::ETC2RGBA, rhi::Format::ETC2RGBA8, rhi::Format::ETC2RGBA8},
            {image::PixelFormat::EACR11, rhi::Format::EACR11, rhi::Format::EACR11},
            {image::PixelFormat::EACRG11, rhi::Format::EACRG11, rhi::Format::EACRG11},
            {image::PixelFormat::ASTC4x4, rhi::Format::ASTC4x4, rhi::Format::ASTC4x4},
            {image::PixelFormat::ASTC6x6, rhi::Format::ASTC6x6, rhi::Format::ASTC6x6},
            {image::PixelFormat::ASTC8x8, rhi::Format::ASTC8x8, rhi::Format::ASTC8x8},
        };

        bool GpuFormat(image::PixelFormat f, bool srgb, rhi::Format& out)
        {
            for (const FormatPair& p : kTextureFormats)
                if (p.pixel == f)
                    return out = srgb ? p.srgb : p.gpu, true;
            return false;
        }

        // Whether the device reads the block format.
        bool Supported(rhi::Format f, const rhi::DeviceCaps& caps)
        {
            if (!rhi::IsCompressedFormat(f))
                return true;
            if (f == rhi::Format::BC1Unorm || f == rhi::Format::BC2Unorm || f == rhi::Format::BC3Unorm)
                return caps.textureCompressionBC;
            if (f == rhi::Format::BC4Unorm || f == rhi::Format::BC5Unorm)
                return caps.textureCompressionRGTC;
            if (f == rhi::Format::BC6HUfloat || f == rhi::Format::BC7Unorm)
                return caps.textureCompressionBPTC;
            if (f >= rhi::Format::ETC2RGB8 && f <= rhi::Format::EACRG11Snorm)
                return caps.textureCompressionETC2;
            return caps.textureCompressionASTC;
        }

        rhi::AddressMode Address(TextureWrapMode w)
        {
            switch (w)
            {
            case TextureWrapMode::Clamp:
                return rhi::AddressMode::ClampToEdge;
            case TextureWrapMode::Mirror:
            case TextureWrapMode::MirrorOnce:
                return rhi::AddressMode::MirroredRepeat;
            default:
                return rhi::AddressMode::Repeat;
            }
        }

        const GpuTexture* TextureOf(Texture& texture)
        {
            RenderState& s = R();
            GpuTexture* g = s.textures.Find(texture.GetEntityId());
            u32 version = texture.Version() ^ (texture.updateCount() << 16);
            if (g && g->version == version)
                return g;
            if (!g)
                g = &s.textures.Set(texture.GetEntityId(), GpuTexture());
            s.device->Destroy(g->texture);
            s.device->Destroy(g->sampler);
            *g = GpuTexture();
            g->version = version;
            const image::TextureData& d = texture.Data();
            if (d.data.Empty() || d.subresources.Empty())
                return g;
            // The top mip (the placeholder renderer samples without a chain), widened where the GPU lacks the format.
            const image::Subresource& top = d.At(0, 0, 0);
            Array<u8> converted;
            const u8* pixels = d.data.Data() + top.offset;
            rhi::Format format;
            bool known = GpuFormat(d.format, texture.isDataSRGB(), format) && Supported(format, s.device->Caps());
            if (!known)
            {
                if (image::IsBlockFormat(d.format) || !image::CanConvert(d.format))
                {
                    HX_LOG_WARNING(RenderLog, "Texture '{}': its format has no GPU form here; it draws as white.", texture.Name());
                    return g;
                }
                bool wide = image::FormatInfo(d.format).kind == image::PixelKind::Float;
                image::PixelFormat to = wide ? image::PixelFormat::RGBA32Float : image::PixelFormat::RGBA8;
                converted.Resize(usize(top.width) * top.height * (wide ? 16 : 4));
                image::ConvertPixels(d.format, pixels, to, converted.Data(), usize(top.width) * top.height);
                pixels = converted.Data();
                GpuFormat(to, texture.isDataSRGB(), format);
            }
            g->texture = s.device->CreateTexture(
                {.format = format, .width = top.width, .height = top.height, .usage = rhi::TextureSampled, .initialData = pixels, .debugName = texture.Name()});
            const TextureSettings& st = texture.settings();
            rhi::SamplerDesc sd;
            sd.minFilter = sd.magFilter = st.filterMode == FilterMode::Point ? rhi::Filter::Nearest : rhi::Filter::Linear;
            sd.mipFilter = st.filterMode == FilterMode::Trilinear ? rhi::Filter::Linear : rhi::Filter::Nearest;
            sd.addressU = Address(st.wrapU);
            sd.addressV = Address(st.wrapV);
            sd.addressW = Address(st.wrapW);
            sd.maxAnisotropy = u32(st.aniso < 1 ? 1 : st.aniso > 16 ? 16 : st.aniso);
            sd.lodBias = st.mipBias;
            g->sampler = s.device->CreateSampler(sd);
            s.stats.uploads++;
            if (!texture.isReadable())
                texture.ReleaseCpuData();
            return g;
        }

        // ------------------------------------------------------------------------------------------------ pipelines

        const GpuShader& ShaderOf(const ShaderVariant& v)
        {
            RenderState& s = R();
            if (GpuShader* g = s.shaders.Find(v.serial))
                return *g;
            GpuShader g;
            for (u32 stage = 0; stage < 2; stage++)
            {
                const CompiledShader& c = stage == 0 ? v.vertex : v.fragment;
                Array<rhi::ShaderBinding> bindings;
                for (const GlslBinding& b : c.bindings)
                    bindings.Push({b.name, b.kind == GlobalKind::Uniform ? rhi::BindingKind::UniformBuffer
                                                                         : b.kind == GlobalKind::Storage ? rhi::BindingKind::StorageBuffer
                                                                                                         : rhi::BindingKind::Texture,
                                   b.slot});
                rhi::ShaderDesc d;
                d.stage = stage == 0 ? rhi::ShaderStage::Vertex : rhi::ShaderStage::Fragment;
                if (c.target == ShaderTarget::Dxbc)
                {
                    d.bytecode = c.code.Data();
                    d.bytecodeSize = c.code.Size();
                }
                else
                {
                    d.source = c.Text();
                    d.bindings = bindings.Data();
                    d.bindingCount = u32(bindings.Size());
                }
                d.debugName = v.description ? StringView(v.description->name) : StringView();
                (stage == 0 ? g.vertex : g.fragment) = s.device->CreateShader(d);
            }
            return s.shaders.Set(v.serial, g);
        }

        rhi::CompareFunction Compare(shadercompiler::CompareFunction f) { return rhi::CompareFunction(u8(f)); }

        rhi::BlendFactor Blend(BlendFactor f)
        {
            switch (f)
            {
            case BlendFactor::Zero: return rhi::BlendFactor::Zero;
            case BlendFactor::SrcColor: return rhi::BlendFactor::SrcColor;
            case BlendFactor::OneMinusSrcColor: return rhi::BlendFactor::OneMinusSrcColor;
            case BlendFactor::DstColor: return rhi::BlendFactor::DstColor;
            case BlendFactor::OneMinusDstColor: return rhi::BlendFactor::OneMinusDstColor;
            case BlendFactor::SrcAlpha: return rhi::BlendFactor::SrcAlpha;
            case BlendFactor::OneMinusSrcAlpha: return rhi::BlendFactor::OneMinusSrcAlpha;
            case BlendFactor::DstAlpha: return rhi::BlendFactor::DstAlpha;
            case BlendFactor::OneMinusDstAlpha: return rhi::BlendFactor::OneMinusDstAlpha;
            default: return rhi::BlendFactor::One;
            }
        }

        rhi::BlendOp BlendOperation(shadercompiler::BlendOperation o) { return rhi::BlendOp(u8(o)); }

        // The pipeline of a variant for a mesh layout, a topology and the target.
        rhi::PipelineHandle PipelineOf(const ShaderVariant& v, const GpuMesh& m, rhi::PrimitiveTopology topology)
        {
            RenderState& s = R();
            u64 key = CombineHash(MixHash(v.serial), MixHash(u64(topology)));
            for (const ReflectedValue& in : v.reflection.vertexInputs)
            {
                u32 a = in.index < kVertexAttributeCount ? in.index : kNone;
                key = CombineHash(key, MixHash(u64(a != kNone && m.offsets[a] != kNone ? u32(m.formats[a]) + 1 : 0)));
            }
            key = CombineHash(key, CombineHash(MixHash(u64(s.target.colorFormat)), MixHash(u64(s.target.depthFormat))));
            if (rhi::PipelineHandle* p = s.pipelines.Find(key))
                return *p;
            const GpuShader& shader = ShaderOf(v);
            rhi::PipelineDesc d;
            d.vertexShader = shader.vertex;
            d.fragmentShader = shader.fragment;
            u32 k = 0;
            for (const ReflectedValue& in : v.reflection.vertexInputs)
            {
                if (k >= rhi::kMaxVertexBuffers)
                    break;
                u32 a = in.index < kVertexAttributeCount ? in.index : kNone;
                bool present = a != kNone && m.offsets[a] != kNone;
                d.vertexBuffers[k] = {present ? m.strides[a] : 16u, rhi::StepMode::Vertex};
                d.attributes[k] = {in.index, k, present ? m.formats[a] : rhi::VertexFormat::Float4, 0};
                k++;
            }
            d.vertexBufferCount = k;
            d.attributeCount = k;
            d.topology = topology;
            const shadercompiler::RenderState& st = *v.state;
            d.cull = st.cull == CullMode::Off ? rhi::CullMode::None : st.cull == CullMode::Front ? rhi::CullMode::Front : rhi::CullMode::Back;
            d.depthTest = s.target.depthFormat != rhi::Format::Unknown && (st.zWrite || st.zTest != shadercompiler::CompareFunction::Always);
            d.depthWrite = s.target.depthFormat != rhi::Format::Unknown && st.zWrite;
            d.depthCompare = Compare(st.zTest);
            for (u32 t = 0; t < 1; t++)
            {
                rhi::BlendState& b = d.blend[t];
                b.writeMask = st.colorMask;
                for (const BlendState& bs : st.blend)
                    if (bs.target < 0 || u32(bs.target) == t)
                    {
                        b.enabled = !(bs.source == BlendFactor::One && bs.destination == BlendFactor::Zero && bs.sourceAlpha == BlendFactor::One &&
                                      bs.destinationAlpha == BlendFactor::Zero);
                        b.srcColor = Blend(bs.source);
                        b.dstColor = Blend(bs.destination);
                        b.srcAlpha = Blend(bs.sourceAlpha);
                        b.dstAlpha = Blend(bs.destinationAlpha);
                        b.colorOp = BlendOperation(bs.operation);
                        b.alphaOp = BlendOperation(bs.operationAlpha);
                    }
            }
            d.colorFormats[0] = s.target.colorFormat;
            d.colorTargetCount = 1;
            d.depthFormat = s.target.depthFormat;
            d.debugName = v.description ? StringView(v.description->name) : StringView();
            rhi::PipelineHandle p = s.device->CreatePipeline(d);
            s.pipelines.Set(key, p);
            s.stats.pipelinesCreated++;
            return p;
        }

        // ------------------------------------------------------------------------------------------------ drawing

        // Light modes of passes the placeholder renderer does not draw.
        constexpr StringView kSkippedLightModes[] = {"ShadowCaster", "DepthOnly", "Meta", "MotionVectors"};

        i32 DrawnPass(Material& m)
        {
            Shader* s = m.shader();
            const ShaderDescription* d = s ? s->Description() : nullptr;
            if (!d)
                return -1;
            for (i32 p = 0; p < s->passCount(); p++)
            {
                i32 index = s->PassIndex(p);
                StringView mode = d->passes[u32(index)].lightMode;
                bool skipped = false;
                for (StringView k : kSkippedLightModes)
                    skipped |= mode == k;
                if (!skipped && m.GetShaderPassEnabled(mode))
                    return index;
            }
            return -1;
        }

        Material* ErrorMaterial()
        {
            RenderState& s = R();
            if (Material* m = EntityIdToObject<Material>(s.errorMaterial))
                return m;
            Material* m = Material::Create(Shader::ErrorShader());
            if (m)
            {
                m->SetName("Error");
                m->SetPersistent(true);
                m->hideFlags = HideFlags::HideAndDontSave;
                s.errorMaterial = m->GetEntityId();
            }
            return m;
        }

        struct DrawItem
        {
            u64 sortKey;
            f32 distance;
            u32 renderer;
            u32 submesh;
            Material* material;
            const MaterialPassData* data;
        };

        void Upload(const void* data, u32 size, rhi::TransientAllocation& out)
        {
            out = R().device->AllocateTransient(size);
            if (out.data && size)
                std::memcpy(out.data, data, size);
        }

        void RenderOne(Camera& camera, bool clearAsFirst)
        {
            RenderState& s = R();
            rhi::Device& device = *s.device;
            Camera::SetCurrent(&camera);
            RenderData& d = RendererManager::Get().RenderMutable();
            ShaderTarget target = Graphics::ShaderTarget();
            Matrix4x4 view = camera.worldToCameraMatrix();
            Matrix4x4 gpuViewProjection = camera.GpuProjectionMatrix() * view;
            Plane planes[6];
            CalculateFrustumPlanes(camera.cullingMatrix(), planes);
            u32 n = d.Count();
            Array<u8> inside;
            inside.Resize(n);
            if (n)
                CullBoundsSoAParallel(planes, d.Bounds(), inside.Data());
            Vector3 eye = camera.transform() ? camera.transform()->position() : Vector3();
            Vector3 forward = camera.transform() ? camera.transform()->forward() : Vector3::forward();
            Array<DrawItem> items;
            u32 cullingMask = camera.cullingMask();
            for (u32 i = 0; i < n; i++)
            {
                if (!inside[i] || !d.drawn[i] || !(d.layerMask[i] & cullingMask))
                    continue;
                Renderer* r = EntityIdToObject<Renderer>(d.renderer[i]);
                Mesh* mesh = EntityIdToObject<Mesh>(d.mesh[i]);
                if (!r || !mesh || mesh->subMeshCount() == 0)
                    continue;
                d.visible[i] = 1;
                s.stats.visible++;
                Vector3 center(d.centerX[i], d.centerY[i], d.centerZ[i]);
                f32 distance = Vector3::Dot(center - eye, forward);
                Span<const PPtr<Material>> materials = r->Materials();
                u32 subs = u32(mesh->subMeshCount());
                u32 draws = materials.Size() > subs ? u32(materials.Size()) : subs;
                for (u32 k = 0; k < draws; k++)
                {
                    // Extra materials draw the last submesh again; submeshes without a material are not drawn.
                    if (k >= materials.Size())
                        break;
                    Material* m = materials[k].Get();
                    if (!m || !m->shader())
                        m = ErrorMaterial();
                    if (!m)
                        continue;
                    i32 pass = DrawnPass(*m);
                    if (pass < 0)
                        continue;
                    const MaterialPassData* data = m->GetPassData(u32(pass), target);
                    if (!data)
                        continue;
                    i32 queue = m->renderQueue();
                    bool transparent = queue > 2500;
                    // Queue, then sorting order, then (opaque) by variant and material front to back or (transparent) back
                    // to front by distance.
                    u64 order = u64(u32(r->sortingOrder() + 0x8000) & 0xFFFF);
                    u64 key = (u64(u32(queue) & 0xFFFF) << 48) | (order << 32) | (transparent ? 0 : u64(data->variant->serial & 0xFFFFFF) << 8);
                    items.Push({key, transparent ? -distance : distance, i, k < subs ? k : subs - 1, m, data});
                }
            }
            std::sort(items.begin(), items.end(), [](const DrawItem& a, const DrawItem& b) {
                return a.sortKey != b.sortKey ? a.sortKey < b.sortKey : a.distance < b.distance;
            });

            // The camera block.
            struct PerCamera
            {
                Matrix4x4 v, p, vp;
                f32 cameraPos[4];
                f32 lightDirection[4];
                f32 lightColor[4];
                f32 ambient[4];
            } cam{};
            cam.v = view;
            cam.p = camera.GpuProjectionMatrix();
            cam.vp = gpuViewProjection;
            cam.cameraPos[0] = eye.x, cam.cameraPos[1] = eye.y, cam.cameraPos[2] = eye.z, cam.cameraPos[3] = 1;
            const Light* sun = nullptr;
            f32 brightest = -1;
            for (Light* l : Light::EnabledLights())
                if (l->type() == LightType::Directional && l->intensity() > brightest && (l->cullingMask() & cullingMask))
                    sun = l, brightest = l->intensity();
            if (sun && sun->transform())
            {
                Vector3 toLight = -sun->transform()->forward();
                Color c = sun->FinalColor();
                cam.lightDirection[0] = toLight.x, cam.lightDirection[1] = toLight.y, cam.lightDirection[2] = toLight.z, cam.lightDirection[3] = 1;
                cam.lightColor[0] = c.r, cam.lightColor[1] = c.g, cam.lightColor[2] = c.b, cam.lightColor[3] = 1;
            }
            cam.ambient[0] = s.ambient.r, cam.ambient[1] = s.ambient.g, cam.ambient[2] = s.ambient.b, cam.ambient[3] = 1;
            rhi::TransientAllocation cameraBlock;
            Upload(&cam, sizeof(cam), cameraBlock);

            rhi::CommandList cl;
            rhi::RenderPassDesc pass;
            pass.colors[0].texture = s.target.color;
            pass.depth.texture = s.target.depth;
            CameraClearFlags flags = camera.clearFlags();
            Color bg = camera.backgroundColor();
            Rect pr = camera.pixelRect();
            // Skyboxes come with the pipelines: until then Skybox clears with the background color.
            bool clearColor = flags == CameraClearFlags::SolidColor || flags == CameraClearFlags::Skybox;
            bool clearDepth = flags != CameraClearFlags::Nothing;
            // A render pass clear takes the whole target: a camera with a smaller viewport clears it by drawing.
            bool whole = pr.x <= 0 && pr.y <= 0 && pr.width >= f32(s.target.width) && pr.height >= f32(s.target.height);
            pass.colors[0].load = (whole && clearColor) || clearAsFirst ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
            pass.colors[0].clearColor[0] = bg.r, pass.colors[0].clearColor[1] = bg.g, pass.colors[0].clearColor[2] = bg.b,
            pass.colors[0].clearColor[3] = bg.a;
            pass.depth.load = (whole && clearDepth) || clearAsFirst ? rhi::LoadOp::Clear : rhi::LoadOp::Load;
            cl.BeginRenderPass(pass);
            rhi::Viewport vp;
            vp.x = pr.x;
            vp.y = f32(s.target.height) - (pr.y + pr.height);
            vp.width = pr.width;
            vp.height = pr.height;
            cl.SetViewport(vp);
            cl.SetScissor({i32(vp.x), i32(vp.y), u32(vp.width), u32(vp.height)});
            if (!whole && (clearColor || clearDepth))
                if (Shader* clear = Shader::Find("Hidden/InternalClear"))
                    if (const ShaderVariant* cv = clear->GetVariant(clearColor ? 0u : 1u, {}, target); cv && cv->ok)
                    {
                        static const GpuMesh kNoVertices{};
                        cl.SetPipeline(PipelineOf(*cv, kNoVertices, rhi::PrimitiveTopology::TriangleList));
                        if (clearColor)
                        {
                            const f32 c[4] = {bg.r, bg.g, bg.b, bg.a};
                            rhi::TransientAllocation clearBlock;
                            Upload(c, sizeof(c), clearBlock);
                            cl.SetUniformBuffer(0, clearBlock);
                        }
                        cl.Draw(3);
                    }

            HashMap<const MaterialPassData*, u32> materialBlocks;
            Array<rhi::TransientAllocation> uploaded;
            Array<Array<u8>> blocks;
            Array<MaterialPassData::TextureSlot> textures;
            u32 lastRenderer = kNone;
            rhi::TransientAllocation drawBlock;
            for (const DrawItem& item : items)
            {
                Renderer* r = EntityIdToObject<Renderer>(d.renderer[item.renderer]);
                Mesh* mesh = EntityIdToObject<Mesh>(d.mesh[item.renderer]);
                const ShaderVariant& v = *item.data->variant;
                const GpuMesh* gm = MeshOf(*mesh);
                if (!gm->vertices.IsValid() || !gm->indices.IsValid() || item.submesh >= gm->subs.Size() || gm->subs[item.submesh].count == 0)
                    continue;
                const GpuMesh::Sub& sub = gm->subs[item.submesh];
                cl.SetPipeline(PipelineOf(v, *gm, sub.topology));
                u32 k = 0;
                for (const ReflectedValue& in : v.reflection.vertexInputs)
                {
                    if (k >= rhi::kMaxVertexBuffers)
                        break;
                    u32 a = in.index < kVertexAttributeCount ? in.index : kNone;
                    if (a != kNone && gm->offsets[a] != kNone)
                        cl.SetVertexBuffer(k, gm->vertices, gm->offsets[a]);
                    else
                        cl.SetVertexBuffer(k, gm->zeros, 0);
                    k++;
                }
                cl.SetIndexBuffer(gm->indices, rhi::IndexType::U32);
                cl.SetUniformBuffer(9, cameraBlock);
                if (item.renderer != lastRenderer)
                {
                    Matrix4x4 m[2] = {d.world[item.renderer], d.world[item.renderer].inverse()};
                    Upload(m, sizeof(m), drawBlock);
                    lastRenderer = item.renderer;
                }
                cl.SetUniformBuffer(10, drawBlock);
                // Material blocks once per material and pass; a property block makes its own copy.
                const PropertySheet* block = r ? r->PropertyBlock(i32(item.submesh)) : nullptr;
                const Array<Array<u8>>* bytes = &item.data->blocks;
                const Array<MaterialPassData::TextureSlot>* slots = &item.data->textures;
                usize firstUpload;
                if (block)
                {
                    blocks = item.data->blocks;
                    textures = item.data->textures;
                    Material::WriteValues(v, *block, nullptr, true, blocks, textures);
                    bytes = &blocks;
                    slots = &textures;
                    firstUpload = uploaded.Size();
                    for (const Array<u8>& b : *bytes)
                        Upload(b.Data(), u32(b.Size()), uploaded.Emplace());
                }
                else if (const u32* at = materialBlocks.Find(item.data))
                    firstUpload = *at;
                else
                {
                    firstUpload = uploaded.Size();
                    materialBlocks.Set(item.data, u32(firstUpload));
                    for (const Array<u8>& b : *bytes)
                        Upload(b.Data(), u32(b.Size()), uploaded.Emplace());
                }
                for (u32 b = 0; b < v.layout.blocks.Size(); b++)
                    cl.SetUniformBuffer(v.layout.blocks[b].slot, uploaded[firstUpload + b]);
                for (const MaterialPassData::TextureSlot& t : *slots)
                {
                    Texture* tex = EntityIdToObject<Texture>(t.texture);
                    const GpuTexture* gt = tex ? TextureOf(*tex) : nullptr;
                    if (!gt || !gt->texture.IsValid())
                        gt = TextureOf(*Texture2D::whiteTexture());
                    cl.SetTexture(t.slot, gt->texture, gt->sampler);
                }
                // The dynamic keywords of the variant.
                for (const ReflectedBlock& rb : v.reflection.blocks)
                    if (rb.name == StringView(kKeywordBlockName))
                    {
                        Array<u8> values;
                        values.Resize(rb.size);
                        std::memset(values.Data(), 0, values.Size());
                        for (const ReflectedMember& member : rb.members)
                            for (const KeywordValue& kv : v.keywords)
                                if (member.name == kv.name)
                                {
                                    i32 value = i32(kv.value);
                                    std::memcpy(values.Data() + member.offset, &value, 4);
                                }
                        rhi::TransientAllocation keywordBlock;
                        Upload(values.Data(), u32(values.Size()), keywordBlock);
                        cl.SetUniformBuffer(rb.slot, keywordBlock);
                    }
                cl.DrawIndexed(sub.count, 1, sub.start, sub.baseVertex);
                s.stats.drawCalls++;
            }
            cl.EndRenderPass();
            device.Submit(cl);
            Camera::SetCurrent(nullptr);
        }

        void ForgetDead()
        {
            RenderState& s = R();
            Array<EntityId> dead;
            for (auto e : s.meshes)
                if (!EntityIdToObject(e.key))
                    dead.Push(e.key);
            for (EntityId id : dead)
            {
                GpuMesh* g = s.meshes.Find(id);
                s.device->Destroy(g->vertices);
                s.device->Destroy(g->indices);
                s.device->Destroy(g->zeros);
                s.meshes.Remove(id);
            }
            dead.Clear();
            for (auto e : s.textures)
                if (!EntityIdToObject(e.key))
                    dead.Push(e.key);
            for (EntityId id : dead)
            {
                GpuTexture* g = s.textures.Find(id);
                s.device->Destroy(g->texture);
                s.device->Destroy(g->sampler);
                s.textures.Remove(id);
            }
        }
    }

    // ---------------------------------------------------------------------------------------------- Graphics

    void Graphics::SetDevice(rhi::Device* device)
    {
        RenderState& s = R();
        if (s.device == device)
            return;
        ReleaseResources();
        s.device = device;
    }

    rhi::Device* Graphics::Device() { return R().device; }

    ShaderTarget Graphics::ShaderTarget()
    {
        rhi::Device* d = R().device;
        if (!d)
            return ShaderTarget::Glsl330;
        const rhi::DeviceCaps& c = d->Caps();
        switch (c.backend)
        {
        case rhi::Backend::D3D11:
            return ShaderTarget::Dxbc;
        case rhi::Backend::OpenGL:
            return c.apiMajor > 4 || (c.apiMajor == 4 && c.apiMinor >= 5) ? ShaderTarget::Glsl450 : ShaderTarget::Glsl330;
        case rhi::Backend::OpenGLES:
            return c.apiMinor >= 2 ? ShaderTarget::Essl320 : c.apiMinor == 1 ? ShaderTarget::Essl310 : ShaderTarget::Essl300;
        default:
            return ShaderTarget::Essl300;
        }
    }

    void Graphics::SetTarget(const RenderTarget& target)
    {
        R().target = target;
        Camera::SetDisplaySize(target.width, target.height);
    }

    const RenderTarget& Graphics::Target() { return R().target; }
    void Graphics::SetAmbientLight(Color color) { R().ambient = color; }
    const RenderStats& Graphics::LastStats() { return R().stats; }

    void Graphics::RenderFrame()
    {
        RendererManager& m = RendererManager::Get();
        m.Update();
        m.Sync();
        RenderState& s = R();
        s.stats = RenderStats();
        RenderData& d = m.RenderMutable();
        if (!d.visible.Empty())
            std::memset(d.visible.Data(), 0, d.visible.Size());
        if (!s.device)
            return;
        if ((++s.frame & 63) == 0)
            ForgetDead();
        Array<Camera*> cameras;
        Camera::GetAllCameras(cameras);
        bool first = true;
        for (Camera* c : cameras)
        {
            RenderOne(*c, first);
            first = false;
            s.stats.cameras++;
        }
    }

    void Graphics::RenderCamera(Camera& camera)
    {
        RendererManager& m = RendererManager::Get();
        m.Update();
        m.Sync();
        if (!R().device)
            return;
        RenderOne(camera, false);
        R().stats.cameras++;
    }

    void Graphics::ReleaseResources()
    {
        RenderState& s = R();
        if (s.device)
        {
            for (auto e : s.meshes)
            {
                s.device->Destroy(e.value.vertices);
                s.device->Destroy(e.value.indices);
                s.device->Destroy(e.value.zeros);
            }
            for (auto e : s.textures)
            {
                s.device->Destroy(e.value.texture);
                s.device->Destroy(e.value.sampler);
            }
            for (auto e : s.shaders)
            {
                s.device->Destroy(e.value.vertex);
                s.device->Destroy(e.value.fragment);
            }
            for (auto e : s.pipelines)
                s.device->Destroy(e.value);
        }
        s.meshes = HashMap<EntityId, GpuMesh>();
        s.textures = HashMap<EntityId, GpuTexture>();
        s.shaders = HashMap<u32, GpuShader>();
        s.pipelines = HashMap<u64, rhi::PipelineHandle>();
    }
}
