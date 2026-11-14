#include <Helix/Engine/Material/Material.h>
#include <Helix/Engine/Rendering/Graphics.h>
#include <Helix/Engine/Rendering/Implementation/RenderCore.h>
#include <Helix/Engine/Rendering/Implementation/RendererManager.h>
#include <Helix/Engine/Rendering/Pipeline/RenderPipeline.h>
#include <Helix/Engine/Rendering/Pipeline/CommandBuffer.h>
#include <Helix/Engine/Application/Application.h>
#include <Helix/Engine/Rendering/Light.h>
#include <Helix/Engine/Rendering/ScreenCapture.h>
#include <Helix/Engine/Texture/Texture.h>

#include <Helix/Core/Hashing/Hash.h>
#include <Helix/Core/Logging/Log.h>
#include <Helix/Image/Codecs/Png.h>
#include <Helix/Image/Texture/Convert.h>
#include <Helix/Platform/IO/FileSystem.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace helix::engine
{
    using namespace shadercompiler;

    namespace
    {
        LogChannel RenderLog{"Engine"};
        using render::GpuMesh;
        using render::GpuTexture;
        using render::kNone;

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
            HashMap<EntityId, render::GpuRenderTarget> renderTargets;
            HashMap<u32, GpuShader> shaders;
            HashMap<u64, rhi::PipelineHandle> pipelines;
            RenderStats stats;
            EntityId errorMaterial;
            u32 frame = 0;
            struct Capture
            {
                String path;
                i32 superSize = 1;
            };
            Array<Capture> captures = Array<Capture>();
            Array<render::Queued> queued = Array<render::Queued>();
            // CaptureScreenshotIntoRenderTexture's textures for the end of the frame.
            Array<EntityId> textureCaptures = Array<EntityId>();
            // RenderFrame's first camera clears the whole target.
            bool clearFirst = false;
        };

        RenderState& R()
        {
            static RenderState s;
            return s;
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

    }

    namespace render
    {
        rhi::Device* Device() { return R().device; }
        const RenderTarget& Target() { return R().target; }
        Color Ambient() { return R().ambient; }
        RenderStats& Stats() { return R().stats; }
        Span<const Queued> QueuedMeshes() { return Span<const Queued>(R().queued.Data(), R().queued.Size()); }
        bool ClearFirstCamera() { return R().clearFirst; }

        Matrix4x4 GpuProjection(const Matrix4x4& projection, bool intoTexture)
        {
            // Clip depth -1..1 to 0..1: z' = (z + w) / 2.
            Matrix4x4 p = projection;
            p.m20 = 0.5f * (p.m20 + p.m30);
            p.m21 = 0.5f * (p.m21 + p.m31);
            p.m22 = 0.5f * (p.m22 + p.m32);
            p.m23 = 0.5f * (p.m23 + p.m33);
            if (intoTexture)
                p.m10 = -p.m10, p.m11 = -p.m11, p.m12 = -p.m12, p.m13 = -p.m13;
            return p;
        }

        TargetFormats ScreenFormats()
        {
            TargetFormats f;
            f.colors[0] = R().target.colorFormat;
            f.depth = R().target.depthFormat;
            return f;
        }

        // ------------------------------------------------------------------------------------------------ render textures

        // Graphics formats as the devices have them.
        struct DeviceFormatRow
        {
            GraphicsFormat format;
            rhi::Format device;
        };
        constexpr DeviceFormatRow kDeviceFormats[] = {
            {GraphicsFormat::R8_UNorm, rhi::Format::R8Unorm},
            {GraphicsFormat::R8G8_UNorm, rhi::Format::RG8Unorm},
            {GraphicsFormat::R8G8B8A8_UNorm, rhi::Format::RGBA8Unorm},
            {GraphicsFormat::R8G8B8A8_SRGB, rhi::Format::RGBA8Srgb},
            {GraphicsFormat::B8G8R8A8_UNorm, rhi::Format::BGRA8Unorm},
            {GraphicsFormat::B8G8R8A8_SRGB, rhi::Format::BGRA8Srgb},
            {GraphicsFormat::R16_SFloat, rhi::Format::R16Float},
            {GraphicsFormat::R16G16_SFloat, rhi::Format::RG16Float},
            {GraphicsFormat::R16G16B16A16_SFloat, rhi::Format::RGBA16Float},
            {GraphicsFormat::R32_SFloat, rhi::Format::R32Float},
            {GraphicsFormat::R32G32_SFloat, rhi::Format::RG32Float},
            {GraphicsFormat::R32G32B32A32_SFloat, rhi::Format::RGBA32Float},
            {GraphicsFormat::R32_UInt, rhi::Format::R32Uint},
            {GraphicsFormat::R32G32_UInt, rhi::Format::RG32Uint},
            {GraphicsFormat::R32G32B32A32_UInt, rhi::Format::RGBA32Uint},
            {GraphicsFormat::R32_SInt, rhi::Format::R32Sint},
            {GraphicsFormat::R32G32_SInt, rhi::Format::RG32Sint},
            {GraphicsFormat::R32G32B32A32_SInt, rhi::Format::RGBA32Sint},
            {GraphicsFormat::A2B10G10R10_UNormPack32, rhi::Format::RGB10A2Unorm},
            {GraphicsFormat::B10G11R11_UFloatPack32, rhi::Format::RG11B10Float},
            {GraphicsFormat::D16_UNorm, rhi::Format::D16Unorm},
            {GraphicsFormat::D24_UNorm, rhi::Format::D24UnormS8Uint},
            {GraphicsFormat::D24_UNorm_S8_UInt, rhi::Format::D24UnormS8Uint},
            {GraphicsFormat::D32_SFloat, rhi::Format::D32Float},
            // Stencil comes with the devices' stencil state.
            {GraphicsFormat::D32_SFloat_S8_UInt, rhi::Format::D32Float},
        };

        rhi::Format DeviceFormat(GraphicsFormat format)
        {
            for (const DeviceFormatRow& r : kDeviceFormats)
                if (r.format == format)
                    return r.device;
            return rhi::Format::Unknown;
        }

        void DestroyRenderTarget(GpuRenderTarget& g)
        {
            rhi::Device* d = R().device;
            d->Destroy(g.color);
            d->Destroy(g.msaa);
            d->Destroy(g.depth);
            d->Destroy(g.sampled.sampler);
            g = GpuRenderTarget();
        }

        ShaderTagId PassTag(const ShaderDescription& description, u32 descriptionPass)
        {
            StringView mode = description.passes[descriptionPass].lightMode;
            return ShaderTagId(mode.Empty() ? StringView("SRPDefaultUnlit") : mode);
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
                if (mesh.isReadable())
                    mesh.GetIndices(list, k, false);
                else
                {
                    // Not readable (scripts may not read it, the renderer may): the index block as it is.
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

        const GpuRenderTarget* RenderTargetOf(RenderTexture& texture)
        {
            RenderState& s = R();
            if (!s.device)
                return nullptr;
            // A render texture is created by its first use.
            if (!texture.IsCreated() && !texture.Create())
                return nullptr;
            GpuRenderTarget* g = s.renderTargets.Find(texture.GetEntityId());
            u32 version = texture.GpuVersion() ^ (texture.Version() << 12);
            if (g && g->version == version)
                return g;
            if (!g)
                g = &s.renderTargets.Set(texture.GetEntityId(), GpuRenderTarget());
            DestroyRenderTarget(*g);
            g->version = version;
            const RenderTextureDescriptor& d = texture.descriptor();
            g->width = u32(d.width), g->height = u32(d.height);
            g->samples = u32(d.msaaSamples > 1 ? d.msaaSamples : 1);
            if (g->samples > s.device->Caps().maxSamples)
                g->samples = s.device->Caps().maxSamples;
            g->mips = u32(texture.mipmapCount() > 0 ? texture.mipmapCount() : 1);
            rhi::TextureType type = d.dimension == TextureDimension::Cube         ? rhi::TextureType::TextureCube
                                    : d.dimension == TextureDimension::Tex2DArray ? rhi::TextureType::Texture2DArray
                                    : d.dimension == TextureDimension::Tex3D      ? rhi::TextureType::Texture3D
                                                                                  : rhi::TextureType::Texture2D;
            g->layers = type == rhi::TextureType::TextureCube ? 6u : type == rhi::TextureType::Texture2D ? 1u : u32(d.volumeDepth > 0 ? d.volumeDepth : 1);
            if (type != rhi::TextureType::Texture2D)
                g->samples = 1;
            g->colorFormat = DeviceFormat(d.graphicsFormat);
            g->depthFormat = DeviceFormat(d.depthStencilFormat);
            if (d.graphicsFormat != GraphicsFormat::None && g->colorFormat == rhi::Format::Unknown)
            {
                HX_LOG_ERROR(RenderLog, "RenderTexture '{}': the format {} is not supported on this device.", texture.Name(), i32(d.graphicsFormat));
                return g;
            }
            if (g->colorFormat != rhi::Format::Unknown)
            {
                g->color = s.device->CreateTexture({.type = type, .format = g->colorFormat, .width = g->width, .height = g->height,
                                                    .depthOrLayers = type == rhi::TextureType::TextureCube ? 1u : g->layers, .mipLevels = g->mips,
                                                    .usage = rhi::TextureRenderTarget | rhi::TextureSampled, .debugName = texture.Name()});
                if (g->samples > 1)
                    g->msaa = s.device->CreateTexture({.format = g->colorFormat, .width = g->width, .height = g->height, .sampleCount = g->samples,
                                                       .usage = rhi::TextureRenderTarget, .debugName = texture.Name()});
            }
            if (g->depthFormat != rhi::Format::Unknown)
                g->depth = s.device->CreateTexture({.type = type == rhi::TextureType::Texture3D ? rhi::TextureType::Texture2D : type,
                                                    .format = g->depthFormat, .width = g->width, .height = g->height,
                                                    .depthOrLayers = type == rhi::TextureType::Texture2DArray ? g->layers : 1u, .sampleCount = g->samples,
                                                    .usage = rhi::TextureDepthStencil, .debugName = texture.Name()});
            const TextureSettings& st = texture.settings();
            rhi::SamplerDesc sd;
            sd.minFilter = sd.magFilter = st.filterMode == FilterMode::Point ? rhi::Filter::Nearest : rhi::Filter::Linear;
            sd.mipFilter = st.filterMode == FilterMode::Trilinear ? rhi::Filter::Linear : rhi::Filter::Nearest;
            sd.addressU = Address(st.wrapU);
            sd.addressV = Address(st.wrapV);
            sd.addressW = Address(st.wrapW);
            g->sampled.texture = g->color;
            g->sampled.sampler = s.device->CreateSampler(sd);
            g->sampled.version = version;
            return g;
        }

        void ResolveRenderTexture(RenderTexture& source, RenderTexture* target)
        {
            const GpuRenderTarget* g = RenderTargetOf(source);
            const GpuRenderTarget* t = target ? RenderTargetOf(*target) : g;
            if (!g || !t || !g->msaa.IsValid() || !t->color.IsValid())
                return;
            rhi::CommandList cl;
            cl.ResolveTexture(g->msaa, t->color);
            R().device->Submit(cl);
        }

        void GenerateRenderTextureMips(RenderTexture& texture)
        {
            const GpuRenderTarget* g = RenderTargetOf(texture);
            if (!g || !g->color.IsValid() || g->mips < 2)
                return;
            rhi::CommandList cl;
            cl.GenerateMips(g->color);
            R().device->Submit(cl);
        }

        // Textures draws sample, by the texture's class (render textures from their target, others from their pixels).
        using TextureSource = const GpuTexture* (*)(Texture& texture);
        const GpuTexture* PixelTextureOf(Texture& texture);
        const GpuTexture* RenderTextureOf(Texture& texture)
        {
            const GpuRenderTarget* g = RenderTargetOf(static_cast<RenderTexture&>(texture));
            return g ? &g->sampled : nullptr;
        }

        const GpuTexture* TextureOf(Texture& texture)
        {
            static HashMap<u32, TextureSource> sources = [] {
                HashMap<u32, TextureSource> m;
                m.Set(ClassOf<Texture>(), &PixelTextureOf);
                m.Set(ClassOf<RenderTexture>(), &RenderTextureOf);
                return m;
            }();
            for (u32 k = texture.ClassIndex(); k != kNoClass; k = GetClass(k).base)
                if (TextureSource* source = sources.Find(k))
                    return (*source)(texture);
            return nullptr;
        }

        const GpuTexture* PixelTextureOf(Texture& texture)
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
            // The top mip (the renderer samples without a chain until the render core), widened where the GPU lacks the format.
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

        // The pipeline of a variant for a mesh layout, a topology and the target.
        rhi::PipelineHandle PipelineOf(const ShaderVariant& v, const GpuMesh& m, rhi::PrimitiveTopology topology, const StateOverride* state,
                                       const TargetFormats* target)
        {
            RenderState& s = R();
            TargetFormats screen;
            if (!target)
            {
                screen = ScreenFormats();
                target = &screen;
            }
            u64 key = CombineHash(MixHash(v.serial), MixHash(u64(topology)));
            for (const ReflectedValue& in : v.reflection.vertexInputs)
            {
                u32 a = in.index < kVertexAttributeCount ? in.index : kNone;
                key = CombineHash(key, MixHash(u64(a != kNone && m.offsets[a] != kNone ? u32(m.formats[a]) + 1 : 0)));
            }
            for (u32 c = 0; c < target->colorCount; c++)
                key = CombineHash(key, MixHash(u64(target->colors[c]) + 1));
            key = CombineHash(key, MixHash(u64(target->depth) | u64(target->colorCount) << 8 | u64(target->samples) << 16 | u64(target->flipped) << 24));
            if (state && state->mask != RenderStateMask::Nothing)
                key = CombineHash(key, state->hash);
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
            d.cull = st.cull == shadercompiler::CullMode::Off     ? rhi::CullMode::None
                     : st.cull == shadercompiler::CullMode::Front ? rhi::CullMode::Front
                                                                  : rhi::CullMode::Back;
            d.depthTest = target->depth != rhi::Format::Unknown && (st.zWrite || st.zTest != shadercompiler::CompareFunction::Always);
            d.depthWrite = target->depth != rhi::Format::Unknown && st.zWrite;
            // A texture target is drawn upside down (its projection is flipped): front faces turn around too.
            d.frontCounterClockwise = target->flipped;
            d.depthCompare = Compare(st.zTest);
            for (u32 t = 0; t < target->colorCount; t++)
            {
                rhi::BlendState& b = d.blend[t];
                b.writeMask = st.colorMask;
                for (const shadercompiler::BlendState& bs : st.blend)
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
            bool hasDepth = target->depth != rhi::Format::Unknown;
            if (state && HasFlag(state->mask, RenderStateMask::Blend))
                for (u32 t = 0; t < target->colorCount; t++)
                    d.blend[t] = state->blends[t];
            if (state && HasFlag(state->mask, RenderStateMask::Depth))
            {
                d.depthTest = hasDepth && state->depthTest;
                d.depthWrite = hasDepth && state->depthWrite;
                d.depthCompare = state->depthCompare;
            }
            if (state && HasFlag(state->mask, RenderStateMask::Raster))
                d.cull = state->cull;
            for (u32 t = 0; t < target->colorCount; t++)
                d.colorFormats[t] = target->colors[t];
            d.colorTargetCount = target->colorCount;
            d.depthFormat = target->depth;
            d.sampleCount = target->samples;
            d.debugName = v.description ? StringView(v.description->name) : StringView();
            rhi::PipelineHandle p = s.device->CreatePipeline(d);
            s.pipelines.Set(key, p);
            s.stats.pipelinesCreated++;
            return p;
        }

        StateOverride MakeOverride(const RenderStateBlock& block)
        {
            // The reference's enums in their order, as the device has them.
            static constexpr rhi::BlendFactor kFactors[] = {
                rhi::BlendFactor::Zero,          rhi::BlendFactor::One,      rhi::BlendFactor::DstColor,         rhi::BlendFactor::SrcColor,
                rhi::BlendFactor::OneMinusDstColor, rhi::BlendFactor::SrcAlpha, rhi::BlendFactor::OneMinusSrcColor, rhi::BlendFactor::DstAlpha,
                rhi::BlendFactor::OneMinusDstAlpha, rhi::BlendFactor::One,   rhi::BlendFactor::OneMinusSrcAlpha};
            static constexpr rhi::CompareFunction kCompare[] = {
                rhi::CompareFunction::Always,  rhi::CompareFunction::Never,    rhi::CompareFunction::Less,         rhi::CompareFunction::Equal,
                rhi::CompareFunction::LessEqual, rhi::CompareFunction::Greater, rhi::CompareFunction::NotEqual, rhi::CompareFunction::GreaterEqual,
                rhi::CompareFunction::Always};
            static constexpr rhi::CullMode kCull[] = {rhi::CullMode::None, rhi::CullMode::Front, rhi::CullMode::Back};
            auto factor = [](BlendMode m) { return u32(m) < 11 ? kFactors[u32(m)] : rhi::BlendFactor::One; };
            auto op = [](BlendOp o) { return u32(o) <= u32(BlendOp::Max) ? rhi::BlendOp(u8(o)) : rhi::BlendOp::Add; };
            StateOverride o;
            o.mask = block.mask;
            u64 blendHash = 0;
            for (u32 i = 0; i < rhi::kMaxColorTargets; i++)
            {
                const RenderTargetBlendState& t = block.blendState.targets[block.blendState.separateMRTBlendStates ? i : 0];
                rhi::BlendState& b = o.blends[i];
                u32 w = u32(t.writeMask);
                // The reference's mask is A, B, G, R from bit 0; the device's R, G, B, A.
                b.writeMask = u8(((w >> 3) & 1) | (((w >> 2) & 1) << 1) | (((w >> 1) & 1) << 2) | ((w & 1) << 3));
                b.srcColor = factor(t.sourceColorBlendMode);
                b.dstColor = factor(t.destinationColorBlendMode);
                b.srcAlpha = factor(t.sourceAlphaBlendMode);
                b.dstAlpha = factor(t.destinationAlphaBlendMode);
                b.colorOp = op(t.colorBlendOperation);
                b.alphaOp = op(t.alphaBlendOperation);
                b.enabled = !(b.srcColor == rhi::BlendFactor::One && b.dstColor == rhi::BlendFactor::Zero && b.srcAlpha == rhi::BlendFactor::One &&
                              b.dstAlpha == rhi::BlendFactor::Zero && b.colorOp == rhi::BlendOp::Add && b.alphaOp == rhi::BlendOp::Add);
                blendHash = CombineHash(blendHash, MixHash(u64(b.enabled) | u64(b.writeMask) << 1 | u64(b.srcColor) << 8 | u64(b.dstColor) << 16 |
                                                           u64(b.srcAlpha) << 24 | u64(b.dstAlpha) << 32 | u64(b.colorOp) << 40 | u64(b.alphaOp) << 48));
            }
            CompareFunction compare = block.depthState.compareFunction;
            o.depthTest = compare != CompareFunction::Disabled;
            o.depthWrite = block.depthState.writeEnabled;
            o.depthCompare = u32(compare) < 9 ? kCompare[u32(compare)] : rhi::CompareFunction::Always;
            o.cull = u32(block.rasterState.cullingMode) < 3 ? kCull[u32(block.rasterState.cullingMode)] : rhi::CullMode::Back;
            u64 h = MixHash(u64(o.mask));
            if (HasFlag(o.mask, RenderStateMask::Blend))
                h = CombineHash(h, blendHash);
            if (HasFlag(o.mask, RenderStateMask::Depth))
                h = CombineHash(h, MixHash(u64(o.depthTest) | u64(o.depthWrite) << 1 | u64(o.depthCompare) << 2));
            if (HasFlag(o.mask, RenderStateMask::Raster))
                h = CombineHash(h, MixHash(u64(o.cull) + 77));
            o.hash = h;
            return o;
        }

        void Upload(const void* data, u32 size, rhi::TransientAllocation& out)
        {
            out = R().device->AllocateTransient(size);
            if (out.data && size)
                std::memcpy(out.data, data, size);
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
    }

    namespace
    {

        void ForgetDead()
        {
            RenderState& s = R();
            Array<EntityId> deadTargets;
            for (auto e : s.renderTargets)
            {
                auto* t = EntityIdToObject<RenderTexture>(e.key);
                if (!t || !t->IsCreated())
                    deadTargets.Push(e.key);
            }
            for (EntityId id : deadTargets)
            {
                render::DestroyRenderTarget(*s.renderTargets.Find(id));
                s.renderTargets.Remove(id);
            }
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

    namespace
    {
        void RenderCameras()
        {
            Array<Camera*> cameras;
            Camera::GetAllCameras(cameras);
            R().clearFirst = true;
            render::RenderWithPipeline(Span<Camera* const>(cameras.Data(), cameras.Size()), true);
            R().clearFirst = false;
        }

        // Draws the frame again into textures of the target's formats (so the pipelines are shared) and writes a PNG.
        void WriteCapture(const RenderState::Capture& capture)
        {
            RenderState& s = R();
            RenderTarget saved = s.target;
            u32 scale = capture.superSize > 1 ? u32(capture.superSize) : 1u;
            u32 w = saved.width * scale, h = saved.height * scale;
            rhi::Format colorFormat = saved.colorFormat == rhi::Format::Unknown ? rhi::Format::RGBA8Unorm : saved.colorFormat;
            if (w == 0 || h == 0 || (colorFormat != rhi::Format::RGBA8Unorm && colorFormat != rhi::Format::BGRA8Unorm))
            {
                HX_LOG_ERROR(RenderLog, "ScreenCapture: no screen to capture into '{}'.", capture.path);
                return;
            }
            rhi::TextureHandle color = s.device->CreateTexture({.format = colorFormat, .width = w, .height = h,
                                                                .usage = rhi::TextureRenderTarget | rhi::TextureSampled, .debugName = "ScreenCapture"});
            rhi::TextureHandle depth;
            if (saved.depthFormat != rhi::Format::Unknown)
                depth = s.device->CreateTexture({.format = saved.depthFormat, .width = w, .height = h, .usage = rhi::TextureDepthStencil,
                                                 .debugName = "ScreenCaptureDepth"});
            RenderStats stats = s.stats;
            Graphics::SetTarget({color, depth, w, h, colorFormat, saved.depthFormat});
            RenderCameras();
            image::Image img;
            img.Allocate(w, h, image::ImageFormat::RGBA8);
            bool read = s.device->ReadTexture(color, 0, img.pixels.Data(), img.pixels.Size());
            Graphics::SetTarget(saved);
            s.stats = stats;
            s.device->Destroy(color);
            s.device->Destroy(depth);
            if (!read)
            {
                HX_LOG_ERROR(RenderLog, "ScreenCapture: the frame for '{}' could not be read back.", capture.path);
                return;
            }
            u8* p = img.pixels.Data();
            for (usize i = 0; i < img.pixels.Size(); i += 4)
            {
                if (colorFormat == rhi::Format::BGRA8Unorm)
                {
                    u8 b = p[i];
                    p[i] = p[i + 2];
                    p[i + 2] = b;
                }
                // The screen has no transparency.
                p[i + 3] = 255;
            }
            Array<u8> png;
            image::EncodePng(img, png);
            String path = capture.path;
#if HX_PLATFORM_ANDROID || HX_PLATFORM_WEB
            if (!StringView(path).StartsWith("/"))
                path = platform::JoinPath(Application::persistentDataPath(), path);
#endif
            if (!platform::WriteFile(path, Span<const u8>(png.Data(), png.Size())))
                HX_LOG_ERROR(RenderLog, "ScreenCapture: '{}' could not be written.", path);
        }
    }

    void ScreenCapture::CaptureScreenshot(StringView filename, i32 superSize)
    {
        if (filename.Empty())
            return;
        R().captures.Push({String(filename), superSize});
    }

    u32 ScreenCapture::PendingCaptures() { return u32(R().captures.Size()); }

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
        // RenderMesh's meshes are for this frame, drawn or not.
        if (!s.device)
        {
            s.queued.Clear();
            return;
        }
        if ((++s.frame & 63) == 0)
            ForgetDead();
        RenderCameras();
        // Renderers that became visible or invisible by any camera this frame.
        if (detail::gVisibilityChanged)
            for (u32 i = 0; i < d.visible.Size(); i++)
                if (d.visible[i] != d.wasVisible[i])
                {
                    d.wasVisible[i] = d.visible[i];
                    if (Renderer* r = EntityIdToObject<Renderer>(d.renderer[i]))
                        detail::gVisibilityChanged(r, d.visible[i] != 0);
                }
        if (!s.textureCaptures.Empty())
        {
            Array<EntityId> into = Move(s.textureCaptures);
            s.textureCaptures = Array<EntityId>();
            for (EntityId id : into)
                if (auto* rt = EntityIdToObject<RenderTexture>(id))
                {
                    CommandBuffer b;
                    b.Blit(RenderTargetIdentifier(BuiltinRenderTextureType::CameraTarget), RenderTargetIdentifier(rt));
                    render::ExecuteNow(b.List(), false);
                }
        }
        RenderTexture::ReleaseUnusedTemporaries(s.frame);
        if (!s.captures.Empty())
        {
            Array<RenderState::Capture> captures = Move(s.captures);
            s.captures = Array<RenderState::Capture>();
            for (const RenderState::Capture& c : captures)
                WriteCapture(c);
        }
        s.queued.Clear();
    }

    void Graphics::RenderMesh(const RenderParams& params, Mesh* mesh, i32 submeshIndex, const Matrix4x4& objectToWorld)
    {
        if (!mesh)
        {
            HX_LOG_ERROR(RenderLog, "Graphics.RenderMesh: the mesh is null.");
            return;
        }
        if (submeshIndex < 0 || submeshIndex >= mesh->subMeshCount())
        {
            HX_LOG_ERROR(RenderLog, "Graphics.RenderMesh: submesh index {} is out of range (the mesh has {}).", submeshIndex, mesh->subMeshCount());
            return;
        }
        render::Queued& q = R().queued.Emplace();
        q.mesh = mesh->GetEntityId();
        q.material = params.material ? params.material->GetEntityId() : EntityId();
        q.camera = params.camera ? params.camera->GetEntityId() : EntityId();
        q.submesh = u32(submeshIndex);
        q.layerMask = params.layer >= 0 && params.layer < 32 ? 1u << u32(params.layer) : 0u;
        q.renderingLayerMask = params.renderingLayerMask;
        q.world = objectToWorld;
        Vector3 size = params.worldBounds.size();
        q.bounds = size.x != 0 || size.y != 0 || size.z != 0 ? params.worldBounds : TransformBounds(mesh->bounds(), objectToWorld);
        if (params.matProps)
        {
            q.hasBlock = true;
            q.block = params.matProps->Sheet();
        }
    }

    u32 Graphics::QueuedMeshCount() { return u32(R().queued.Size()); }

    void Graphics::ExecuteCommandBuffer(const CommandBuffer& buffer)
    {
        if (!R().device)
            return;
        render::ExecuteNow(buffer.List());
    }

    namespace
    {
        RenderTargetIdentifier DestinationOf(RenderTexture* dest)
        {
            return dest ? RenderTargetIdentifier(dest) : RenderTargetIdentifier(BuiltinRenderTextureType::CameraTarget);
        }
    }

    void Graphics::Blit(Texture* source, RenderTexture* dest, Material* material, i32 pass)
    {
        if (!R().device)
            return;
        CommandBuffer b;
        b.Blit(RenderTargetIdentifier(source), DestinationOf(dest), material, pass);
        render::ExecuteNow(b.List(), false);
    }

    void Graphics::Blit(Texture* source, RenderTexture* dest, Vector2 scale, Vector2 offset)
    {
        if (!R().device)
            return;
        CommandBuffer b;
        b.Blit(RenderTargetIdentifier(source), DestinationOf(dest), scale, offset);
        render::ExecuteNow(b.List(), false);
    }

    void Graphics::Blit(Texture* source, Material* material, i32 pass) { Blit(source, RenderTexture::active(), material, pass); }

    void Graphics::CopyTexture(Texture* source, Texture* dest)
    {
        if (!R().device || !source || !dest)
            return;
        CommandBuffer b;
        b.CopyTexture(RenderTargetIdentifier(source), RenderTargetIdentifier(dest));
        render::ExecuteNow(b.List(), false);
    }

    void Graphics::CopyTexture(Texture* source, i32 sourceElement, i32 sourceMip, Texture* dest, i32 destElement, i32 destMip)
    {
        if (!R().device || !source || !dest)
            return;
        CommandBuffer b;
        b.CopyTexture(RenderTargetIdentifier(source), sourceElement, sourceMip, RenderTargetIdentifier(dest), destElement, destMip);
        render::ExecuteNow(b.List(), false);
    }

    bool Graphics::ConvertTexture(Texture* source, i32 sourceElement, Texture* dest, i32 destElement)
    {
        RenderTexture* to = dest && dest->Is<RenderTexture>() ? static_cast<RenderTexture*>(dest) : nullptr;
        if (!source || !to)
        {
            HX_LOG_ERROR(RenderLog, "Graphics.ConvertTexture: {}.", !source ? "the source is null" : "the destination is not a render texture");
            return false;
        }
        if (!R().device)
            return false;
        // An element of an array or cube goes through a 2D temporary of its texture's size and format.
        auto flat = [](const RenderTextureDescriptor& d) {
            RenderTextureDescriptor f = d;
            f.dimension = TextureDimension::Tex2D;
            f.volumeDepth = 1;
            f.msaaSamples = 1;
            f.useMipMap = false;
            f.depthStencilFormat = GraphicsFormat::None;
            return f;
        };
        Texture* from = source;
        RenderTexture* fromFlat = nullptr;
        if (source->dimension() != TextureDimension::Tex2D)
        {
            if (!source->Is<RenderTexture>())
            {
                HX_LOG_ERROR(RenderLog, "Graphics.ConvertTexture: the elements of {} cannot be read.", source->Name());
                return false;
            }
            fromFlat = RenderTexture::GetTemporary(flat(static_cast<RenderTexture*>(source)->descriptor()));
            CopyTexture(source, sourceElement, 0, fromFlat, 0, 0);
            from = fromFlat;
        }
        RenderTexture* into = to->dimension() == TextureDimension::Tex2D ? to : RenderTexture::GetTemporary(flat(to->descriptor()));
        Blit(from, into);
        if (into != to)
        {
            CopyTexture(into, 0, 0, to, destElement, 0);
            RenderTexture::ReleaseTemporary(into);
        }
        if (fromFlat)
            RenderTexture::ReleaseTemporary(fromFlat);
        return true;
    }

    void Graphics::SetRenderTarget(RenderTexture* rt) { RenderTexture::SetActive(rt); }

    void ScreenCapture::CaptureScreenshotIntoRenderTexture(RenderTexture* renderTexture)
    {
        if (!renderTexture)
        {
            HX_LOG_ERROR(RenderLog, "ScreenCapture.CaptureScreenshotIntoRenderTexture: the render texture is null.");
            return;
        }
        R().textureCaptures.Push(renderTexture->GetEntityId());
    }

    void Graphics::RenderCamera(Camera& camera)
    {
        RendererManager& m = RendererManager::Get();
        m.Update();
        m.Sync();
        if (!R().device)
            return;
        Camera* one = &camera;
        render::RenderWithPipeline(Span<Camera* const>(&one, 1), false);
    }

    void Graphics::ReleaseResources()
    {
        RenderState& s = R();
        render::ReleasePipeline();
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
            for (auto e : s.renderTargets)
                render::DestroyRenderTarget(e.value);
        }
        s.renderTargets = HashMap<EntityId, render::GpuRenderTarget>();
        s.meshes = HashMap<EntityId, GpuMesh>();
        s.textures = HashMap<EntityId, GpuTexture>();
        s.shaders = HashMap<u32, GpuShader>();
        s.pipelines = HashMap<u64, rhi::PipelineHandle>();
    }
}
