#include <Helix/Engine/Rendering/Pipeline/ScriptableRenderContext.h>
#include <Helix/Engine/Rendering/Pipeline/RenderPipeline.h>
#include <Helix/Engine/Rendering/Implementation/RenderCore.h>
#include <Helix/Engine/Rendering/Implementation/RendererManager.h>
#include <Helix/Engine/Rendering/Light.h>
#include <Helix/Engine/Texture/Texture.h>

#include <Helix/Core/Hashing/Hash.h>
#include <Helix/Core/Logging/Log.h>
#include <Helix/Profiler/Profiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

namespace helix::engine
{
    using namespace render;

    namespace
    {
        LogChannel ContextLog{"Engine"};

        // Live buffers and contexts by id, live culling results (C# holds ids; a stale one is found out).
        struct Live
        {
            std::mutex mutex;
            HashMap<u64, CommandBuffer*> buffers;
            HashMap<u64, ScriptableRenderContext*> contexts;
            HashMap<u64, u8> culling;
            u64 next = 0;
        };

        Live& L()
        {
            static Live l;
            return l;
        }

        u64 NextId()
        {
            return ++L().next;
        }

        // Profiler markers of sample names (they live as long as the process: a marker keeps its name's address).
        profiler::Marker& MarkerOf(StringView name)
        {
            static std::mutex mutex;
            static HashMap<u64, profiler::Marker*> markers;
            static Array<String*> names;
            std::lock_guard<std::mutex> lock(mutex);
            u64 key = Hash64(name.Data(), name.Size());
            if (profiler::Marker** m = markers.Find(key))
                return **m;
            auto* text = new String(name);
            names.Push(text);
            auto* marker = new profiler::Marker(text->CStr(), "CommandBuffer", 0);
            markers.Set(key, marker);
            return *marker;
        }
    }

    // ---------------------------------------------------------------------------------------------- CommandList

    namespace render
    {
        namespace
        {
            // Which array a command's index points into.
            enum class Refers : u8
            {
                Nothing,
                Matrices,
                Texts,
                Draws,
                Targets,
                Descriptors,
            };
            constexpr Refers kRefers[] = {
                Refers::Nothing,  // ClearTarget
                Refers::Nothing,  // Viewport
                Refers::Nothing,  // Scissor
                Refers::Nothing,  // NoScissor
                Refers::Matrices, // ViewProjection
                Refers::Nothing,  // GlobalFloat
                Refers::Nothing,  // GlobalInteger
                Refers::Nothing,  // GlobalVector
                Refers::Matrices, // GlobalMatrix
                Refers::Nothing,  // GlobalTexture
                Refers::Texts,    // Keyword
                Refers::Matrices, // DrawMesh
                Refers::Nothing,  // DrawRenderer
                Refers::Texts,    // BeginSample
                Refers::Texts,    // EndSample
                Refers::Targets,  // SetTarget
                Refers::Targets,  // Blit
                Refers::Targets,  // CopyTexture
                Refers::Nothing,  // ResolveTarget
                Refers::Targets,  // GenerateMips
                Refers::Descriptors, // GetTemporary
                Refers::Nothing,  // ReleaseTemporary
                Refers::Targets,  // GlobalTextureTarget
                Refers::Draws,    // DrawRenderers
                Refers::Nothing,  // SetupCamera
                Refers::Nothing,  // RenderObjects
            };
            static_assert(sizeof(kRefers) / sizeof(kRefers[0]) == usize(Op::Count), "a table entry per command kind");
        }

        Command& CommandList::Add(Op op)
        {
            Command& c = commands.Emplace();
            c.op = op;
            return c;
        }

        void CommandList::Append(const CommandList& other)
        {
            const u32 base[] = {0, u32(matrices.Size()), u32(texts.Size()), u32(draws.Size()), u32(targets.Size()), u32(descriptors.Size())};
            u32 sheetBase = u32(sheets.Size());
            for (Command c : other.commands)
            {
                c.index += base[u32(kRefers[u32(c.op)])];
                if (c.sheet != kNone)
                    c.sheet += sheetBase;
                commands.Push(c);
            }
            for (const Matrix4x4& m : other.matrices)
                matrices.Push(m);
            for (const String& t : other.texts)
                texts.Push(t);
            for (const PropertySheet& s : other.sheets)
                sheets.Push(s);
            for (const DrawRequest& d : other.draws)
                draws.Push(d);
            for (const RenderTargetIdentifier& t : other.targets)
                targets.Push(t);
            for (const RenderTextureDescriptor& d : other.descriptors)
                descriptors.Push(d);
        }

        void CommandList::Clear()
        {
            commands.Clear();
            matrices.Clear();
            texts.Clear();
            sheets.Clear();
            draws.Clear();
            targets.Clear();
            descriptors.Clear();
        }

        usize CommandList::Bytes() const
        {
            usize n = commands.Size() * sizeof(Command) + matrices.Size() * sizeof(Matrix4x4) + sheets.Size() * sizeof(PropertySheet) +
                      draws.Size() * sizeof(DrawRequest) + targets.Size() * sizeof(RenderTargetIdentifier) +
                      descriptors.Size() * sizeof(RenderTextureDescriptor);
            for (const String& t : texts)
                n += sizeof(String) + t.Size();
            return n;
        }
    }

    // ---------------------------------------------------------------------------------------------- CommandBuffer

    CommandBuffer::CommandBuffer()
    {
        std::lock_guard<std::mutex> lock(L().mutex);
        id_ = NextId();
        L().buffers.Set(id_, this);
    }

    CommandBuffer::~CommandBuffer()
    {
        std::lock_guard<std::mutex> lock(L().mutex);
        L().buffers.Remove(id_);
    }

    CommandBuffer* CommandBuffer::Find(u64 id)
    {
        std::lock_guard<std::mutex> lock(L().mutex);
        CommandBuffer** b = L().buffers.Find(id);
        return b ? *b : nullptr;
    }

    void CommandBuffer::ClearRenderTarget(bool clearDepth, bool clearColor, Color backgroundColor, f32 depth)
    {
        Command& c = list_.Add(Op::ClearTarget);
        c.flag = clearDepth, c.flag2 = clearColor;
        c.v[0] = backgroundColor.r, c.v[1] = backgroundColor.g, c.v[2] = backgroundColor.b, c.v[3] = backgroundColor.a;
        c.f = depth;
    }

    void CommandBuffer::SetViewport(const Rect& r)
    {
        Command& c = list_.Add(Op::Viewport);
        c.v[0] = r.x, c.v[1] = r.y, c.v[2] = r.width, c.v[3] = r.height;
    }

    void CommandBuffer::EnableScissorRect(const Rect& r)
    {
        Command& c = list_.Add(Op::Scissor);
        c.v[0] = r.x, c.v[1] = r.y, c.v[2] = r.width, c.v[3] = r.height;
    }

    void CommandBuffer::DisableScissorRect() { list_.Add(Op::NoScissor); }

    void CommandBuffer::SetViewProjectionMatrices(const Matrix4x4& view, const Matrix4x4& proj)
    {
        Command& c = list_.Add(Op::ViewProjection);
        c.index = u32(list_.matrices.Size());
        list_.matrices.Push(view);
        list_.matrices.Push(proj);
    }

    void CommandBuffer::SetGlobalFloat(i32 nameID, f32 value)
    {
        Command& c = list_.Add(Op::GlobalFloat);
        c.a = nameID, c.f = value;
    }

    void CommandBuffer::SetGlobalInteger(i32 nameID, i32 value)
    {
        Command& c = list_.Add(Op::GlobalInteger);
        c.a = nameID, c.b = value;
    }

    void CommandBuffer::SetGlobalVector(i32 nameID, Vector4 value)
    {
        Command& c = list_.Add(Op::GlobalVector);
        c.a = nameID;
        c.v[0] = value.x, c.v[1] = value.y, c.v[2] = value.z, c.v[3] = value.w;
    }

    void CommandBuffer::SetGlobalMatrix(i32 nameID, const Matrix4x4& value)
    {
        Command& c = list_.Add(Op::GlobalMatrix);
        c.a = nameID;
        c.index = u32(list_.matrices.Size());
        list_.matrices.Push(value);
    }

    void CommandBuffer::SetGlobalTexture(i32 nameID, Texture* value)
    {
        Command& c = list_.Add(Op::GlobalTexture);
        c.a = nameID;
        c.object = value ? value->GetEntityId() : EntityId();
    }

    void CommandBuffer::EnableShaderKeyword(StringView keyword) { SetKeyword(GlobalKeyword(keyword), true); }
    void CommandBuffer::DisableShaderKeyword(StringView keyword) { SetKeyword(GlobalKeyword(keyword), false); }

    void CommandBuffer::SetKeyword(const GlobalKeyword& keyword, bool value)
    {
        Command& c = list_.Add(Op::Keyword);
        c.flag = value;
        c.index = u32(list_.texts.Size());
        list_.texts.Push(String(keyword.name()));
    }

    void CommandBuffer::DrawMesh(Mesh* mesh, const Matrix4x4& matrix, Material* material, i32 submeshIndex, i32 shaderPass,
                                 const MaterialPropertyBlock* properties)
    {
        if (!mesh)
        {
            HX_LOG_ERROR(ContextLog, "CommandBuffer.DrawMesh: the mesh is null.");
            return;
        }
        Command& c = list_.Add(Op::DrawMesh);
        c.object = mesh->GetEntityId();
        c.object2 = material ? material->GetEntityId() : EntityId();
        c.a = submeshIndex, c.b = shaderPass;
        c.index = u32(list_.matrices.Size());
        list_.matrices.Push(matrix);
        if (properties && !properties->isEmpty())
        {
            c.sheet = u32(list_.sheets.Size());
            list_.sheets.Push(properties->Sheet());
        }
    }

    void CommandBuffer::DrawRenderer(Renderer* renderer, Material* material, i32 submeshIndex, i32 shaderPass)
    {
        if (!renderer)
        {
            HX_LOG_ERROR(ContextLog, "CommandBuffer.DrawRenderer: the renderer is null.");
            return;
        }
        Command& c = list_.Add(Op::DrawRenderer);
        c.object = renderer->GetEntityId();
        c.object2 = material ? material->GetEntityId() : EntityId();
        c.a = submeshIndex, c.b = shaderPass;
    }

    void CommandBuffer::BeginSample(StringView name)
    {
        Command& c = list_.Add(Op::BeginSample);
        c.index = u32(list_.texts.Size());
        list_.texts.Push(String(name));
    }

    void CommandBuffer::EndSample(StringView name)
    {
        Command& c = list_.Add(Op::EndSample);
        c.index = u32(list_.texts.Size());
        list_.texts.Push(String(name));
    }

    // ---------------------------------------------------------------------------------------------- targets

    RenderTargetIdentifier::RenderTargetIdentifier(Texture* t)
    {
        if (!t)
            return;
        type = t->Is<RenderTexture>() ? BuiltinRenderTextureType::RenderTexture : BuiltinRenderTextureType::BindableTexture;
        texture = t->GetEntityId();
    }

    void CommandBuffer::SetRenderTarget(const RenderTargetIdentifier& target, RenderBufferLoadAction load, RenderBufferStoreAction store)
    {
        SetRenderTarget(&target, 1, target, false, load, store);
    }

    void CommandBuffer::SetRenderTarget(const RenderTargetIdentifier* colors, u32 count, const RenderTargetIdentifier& depth, bool separateDepth,
                                        RenderBufferLoadAction load, RenderBufferStoreAction store)
    {
        if (count > rhi::kMaxColorTargets)
        {
            HX_LOG_ERROR(ContextLog, "CommandBuffer.SetRenderTarget: {} color targets (at most {}).", count, rhi::kMaxColorTargets);
            return;
        }
        Command& c = list_.Add(Op::SetTarget);
        c.index = u32(list_.targets.Size());
        c.a = i32(count);
        c.b = i32(load), c.c = i32(store);
        c.flag = separateDepth;
        for (u32 i = 0; i < count; i++)
            list_.targets.Push(colors[i]);
        list_.targets.Push(depth);
    }

    void CommandBuffer::GetTemporaryRT(i32 nameID, i32 width, i32 height, i32 depthBuffer, FilterMode filter, RenderTextureFormat format,
                                       RenderTextureReadWrite readWrite, i32 antiAliasing, bool enableRandomWrite)
    {
        RenderTextureDescriptor d;
        d.width = width, d.height = height;
        d.sRGB = readWrite == RenderTextureReadWrite::sRGB;
        d.SetColorFormat(format);
        d.SetDepthBufferBits(depthBuffer);
        d.msaaSamples = antiAliasing < 1 ? 1 : antiAliasing;
        d.enableRandomWrite = enableRandomWrite;
        GetTemporaryRT(nameID, d, filter);
    }

    void CommandBuffer::GetTemporaryRT(i32 nameID, i32 width, i32 height, i32 depthBuffer, FilterMode filter, GraphicsFormat format, i32 antiAliasing,
                                       bool enableRandomWrite)
    {
        RenderTextureDescriptor d(width, height, format, depthBuffer);
        d.msaaSamples = antiAliasing < 1 ? 1 : antiAliasing;
        d.enableRandomWrite = enableRandomWrite;
        GetTemporaryRT(nameID, d, filter);
    }

    void CommandBuffer::GetTemporaryRT(i32 nameID, const RenderTextureDescriptor& descriptor, FilterMode filter)
    {
        Command& c = list_.Add(Op::GetTemporary);
        c.a = nameID, c.b = i32(filter);
        c.index = u32(list_.descriptors.Size());
        list_.descriptors.Push(descriptor);
    }

    void CommandBuffer::GetTemporaryRTArray(i32 nameID, i32 width, i32 height, i32 slices, i32 depthBuffer, FilterMode filter,
                                            RenderTextureFormat format, RenderTextureReadWrite readWrite, i32 antiAliasing, bool enableRandomWrite)
    {
        RenderTextureDescriptor d;
        d.width = width, d.height = height;
        d.dimension = TextureDimension::Tex2DArray;
        d.volumeDepth = slices < 1 ? 1 : slices;
        d.sRGB = readWrite == RenderTextureReadWrite::sRGB;
        d.SetColorFormat(format);
        d.SetDepthBufferBits(depthBuffer);
        d.msaaSamples = antiAliasing < 1 ? 1 : antiAliasing;
        d.enableRandomWrite = enableRandomWrite;
        GetTemporaryRT(nameID, d, filter);
    }

    void CommandBuffer::GetTemporaryRTArray(i32 nameID, i32 width, i32 height, i32 slices, i32 depthBuffer, FilterMode filter, GraphicsFormat format,
                                            i32 antiAliasing, bool enableRandomWrite, bool useDynamicScale)
    {
        RenderTextureDescriptor d(width, height, format, depthBuffer);
        d.dimension = TextureDimension::Tex2DArray;
        d.volumeDepth = slices < 1 ? 1 : slices;
        d.msaaSamples = antiAliasing < 1 ? 1 : antiAliasing;
        d.enableRandomWrite = enableRandomWrite;
        d.useDynamicScale = useDynamicScale;
        GetTemporaryRT(nameID, d, filter);
    }

    void CommandBuffer::ReleaseTemporaryRT(i32 nameID) { list_.Add(Op::ReleaseTemporary).a = nameID; }

    void CommandBuffer::Blit(const RenderTargetIdentifier& source, const RenderTargetIdentifier& dest, Material* material, i32 pass)
    {
        Command& c = list_.Add(Op::Blit);
        c.index = u32(list_.targets.Size());
        list_.targets.Push(source);
        list_.targets.Push(dest);
        c.object = material ? material->GetEntityId() : EntityId();
        c.a = pass;
        c.v[0] = 1, c.v[1] = 1, c.v[2] = 0, c.v[3] = 0;
    }

    void CommandBuffer::Blit(const RenderTargetIdentifier& source, const RenderTargetIdentifier& dest, Vector2 scale, Vector2 offset)
    {
        Blit(source, dest);
        Command& c = list_.commands.Back();
        c.v[0] = scale.x, c.v[1] = scale.y, c.v[2] = offset.x, c.v[3] = offset.y;
    }

    void CommandBuffer::CopyTexture(const RenderTargetIdentifier& source, i32 sourceElement, i32 sourceMip, const RenderTargetIdentifier& dest,
                                    i32 destElement, i32 destMip)
    {
        Command& c = list_.Add(Op::CopyTexture);
        c.index = u32(list_.targets.Size());
        list_.targets.Push(source);
        list_.targets.Push(dest);
        c.a = sourceElement, c.b = sourceMip, c.c = destElement, c.d = destMip;
    }

    void CommandBuffer::ResolveAntiAliasedSurface(RenderTexture* rt, RenderTexture* target)
    {
        if (!rt)
        {
            HX_LOG_ERROR(ContextLog, "CommandBuffer.ResolveAntiAliasedSurface: the render texture is null.");
            return;
        }
        Command& c = list_.Add(Op::ResolveTarget);
        c.object = rt->GetEntityId();
        c.object2 = target ? target->GetEntityId() : EntityId();
    }

    void CommandBuffer::GenerateMips(const RenderTargetIdentifier& rt)
    {
        Command& c = list_.Add(Op::GenerateMips);
        c.index = u32(list_.targets.Size());
        list_.targets.Push(rt);
    }

    void CommandBuffer::SetGlobalTexture(i32 nameID, const RenderTargetIdentifier& value)
    {
        Command& c = list_.Add(Op::GlobalTextureTarget);
        c.a = nameID;
        c.index = u32(list_.targets.Size());
        list_.targets.Push(value);
    }

    void CommandBuffer::ClearRenderTarget(RTClearFlags clearFlags, Color backgroundColor, f32 depth, u32)
    {
        ClearRenderTarget((i32(clearFlags) & i32(RTClearFlags::Depth)) != 0, (i32(clearFlags) & i32(RTClearFlags::Color)) != 0, backgroundColor, depth);
    }

    // ---------------------------------------------------------------------------------------------- execution

    namespace render
    {
        // The camera block every draw reads (slot 9).
        struct PerCamera
        {
            Matrix4x4 v, p, vp;
            f32 cameraPos[4];
            f32 lightDirection[4];
            f32 lightColor[4];
            f32 ambient[4];
        };

        // The textures draws go into: colors (each with its mip and layer) and a depth, their size and formats, and the
        // render textures among them (resolved and given their mips when they stop being the target).
        struct BoundTarget
        {
            rhi::TextureHandle colors[rhi::kMaxColorTargets];
            u32 mips[rhi::kMaxColorTargets] = {}, layers[rhi::kMaxColorTargets] = {};
            rhi::TextureHandle depth;
            u32 depthMip = 0, depthLayer = 0;
            u32 width = 0, height = 0;
            TargetFormats formats;
            EntityId textures[rhi::kMaxColorTargets + 1];
        };

        BoundTarget ScreenTarget()
        {
            const RenderTarget& t = Target();
            BoundTarget b;
            b.colors[0] = t.color;
            b.depth = t.depth;
            b.width = t.width, b.height = t.height;
            b.formats = ScreenFormats();
            return b;
        }

        // What commands set that stays for the context's next Submit.
        struct ContextState
        {
            rhi::Viewport viewport;
            bool scissorOn = false;
            rhi::ScissorRect scissor;
            // The camera block with the device's projection for the screen (flipped when uploaded for a texture target).
            PerCamera camera{};
            Array<String> samples = Array<String>();
            BoundTarget bound;
            // The camera's target (none: the screen) and its pixel size (negative temporary sizes divide it).
            EntityId cameraTexture;
            i32 cameraWidth = 0, cameraHeight = 0;
            // The temporary render textures by property id.
            HashMap<i32, EntityId> temporaries = HashMap<i32, EntityId>();
        };

        ContextState WholeTarget()
        {
            ContextState s;
            s.bound = ScreenTarget();
            s.viewport.width = f32(s.bound.width);
            s.viewport.height = f32(s.bound.height);
            s.cameraWidth = i32(s.bound.width), s.cameraHeight = i32(s.bound.height);
            s.camera.v = s.camera.p = s.camera.vp = Matrix4x4::identity;
            return s;
        }

        void ReleaseTemporaries(ContextState& s)
        {
            for (auto t : s.temporaries)
                RenderTexture::ReleaseTemporary(EntityIdToObject<RenderTexture>(t.value));
            s.temporaries = HashMap<i32, EntityId>();
        }
    }

    namespace
    {
        struct Executor
        {
            rhi::Device& device;
            ContextState& state;
            const CommandList& list;
            shadercompiler::ShaderTarget shaderTarget;
            rhi::CommandList cl = rhi::CommandList();
            bool open = false;
            bool cameraDirty = true;
            rhi::TransientAllocation cameraBlock = rhi::TransientAllocation();
            HashMap<const MaterialPassData*, u32> materialBlocks = HashMap<const MaterialPassData*, u32>();
            Array<rhi::TransientAllocation> uploaded = Array<rhi::TransientAllocation>();
            u64 lastWorld = ~0ull;
            rhi::TransientAllocation drawBlock = rhi::TransientAllocation();
            Array<Array<u8>> blocks = Array<Array<u8>>();
            Array<MaterialPassData::TextureSlot> textures = Array<MaterialPassData::TextureSlot>();
            u64 drawSerial = 0;
        };

        void ApplyViewport(Executor& e)
        {
            const rhi::Viewport& vp = e.state.viewport;
            e.cl.SetViewport(vp);
            e.cl.SetScissor(e.state.scissorOn ? e.state.scissor : rhi::ScissorRect{i32(vp.x), i32(vp.y), u32(vp.width), u32(vp.height)});
        }

        void BeginPass(Executor& e, rhi::LoadOp color, rhi::LoadOp depth, const f32* clearColor, f32 clearDepth)
        {
            const BoundTarget& b = e.state.bound;
            rhi::RenderPassDesc pass;
            pass.colorCount = b.formats.colorCount;
            for (u32 i = 0; i < b.formats.colorCount; i++)
            {
                pass.colors[i].texture = b.colors[i];
                pass.colors[i].mipLevel = b.mips[i];
                pass.colors[i].layer = b.layers[i];
                pass.colors[i].load = color;
                if (clearColor)
                    for (u32 k = 0; k < 4; k++)
                        pass.colors[i].clearColor[k] = clearColor[k];
            }
            pass.depth.texture = b.depth;
            pass.depth.mipLevel = b.depthMip;
            pass.depth.layer = b.depthLayer;
            pass.depth.load = depth;
            pass.depth.clearDepth = clearDepth;
            e.cl.BeginRenderPass(pass);
            e.open = true;
            ApplyViewport(e);
        }

        void EnsurePass(Executor& e)
        {
            if (!e.open)
                BeginPass(e, rhi::LoadOp::Load, rhi::LoadOp::Load, nullptr, 1.0f);
        }

        void EndPass(Executor& e)
        {
            if (e.open)
                e.cl.EndRenderPass();
            e.open = false;
        }

        // A device rect from a rect of the target in pixels from its bottom left (a texture target keeps rows bottom
        // first: its rows count from the bottom on the device too).
        rhi::Viewport DeviceRect(const Executor& e, const f32* r)
        {
            rhi::Viewport vp;
            vp.x = r[0];
            vp.y = e.state.bound.formats.flipped ? r[1] : f32(e.state.bound.height) - (r[1] + r[3]);
            vp.width = r[2];
            vp.height = r[3];
            return vp;
        }

        // The camera block for the bound target: the projection flipped for a texture.
        void UploadCamera(Executor& e)
        {
            if (!e.cameraDirty)
                return;
            PerCamera cam = e.state.camera;
            if (e.state.bound.formats.flipped)
            {
                cam.p.m10 = -cam.p.m10, cam.p.m11 = -cam.p.m11, cam.p.m12 = -cam.p.m12, cam.p.m13 = -cam.p.m13;
                cam.vp = cam.p * cam.v;
            }
            Upload(&cam, sizeof(PerCamera), e.cameraBlock);
            e.cameraDirty = false;
        }

        // The render textures of the bound target resolved (multisampled ones) and given their mips (autoGenerateMips).
        void FinishTarget(Executor& e)
        {
            for (EntityId id : e.state.bound.textures)
            {
                auto* rt = EntityIdToObject<RenderTexture>(id);
                const GpuRenderTarget* g = rt ? RenderTargetOf(*rt) : nullptr;
                if (!g || !g->color.IsValid())
                    continue;
                if (g->msaa.IsValid())
                    e.cl.ResolveTexture(g->msaa, g->color);
                if (rt->useMipMap() && rt->autoGenerateMips() && g->mips > 1)
                    e.cl.GenerateMips(g->color);
            }
        }

        // A new target: what was drawn is finished, the viewport is the whole target, the scissor goes.
        void Bind(Executor& e, const BoundTarget& target)
        {
            EndPass(e);
            FinishTarget(e);
            bool flipChanged = e.state.bound.formats.flipped != target.formats.flipped;
            e.state.bound = target;
            e.state.viewport = rhi::Viewport();
            e.state.viewport.width = f32(target.width);
            e.state.viewport.height = f32(target.height);
            e.state.scissorOn = false;
            if (flipChanged)
                e.cameraDirty = true;
        }

        // A render texture as a target: its color (the multisampled one when it has one) and depth.
        BoundTarget TextureTarget(RenderTexture& rt, u32 mip, u32 layer, bool color, bool depth)
        {
            BoundTarget b;
            const GpuRenderTarget* g = RenderTargetOf(rt);
            if (!g)
                return b;
            b.width = Max(1u, g->width >> mip), b.height = Max(1u, g->height >> mip);
            b.formats.flipped = true;
            b.formats.colorCount = 0;
            // A mip other than the first is drawn into the sampled texture (the multisampled one has no mips).
            bool multisampled = g->msaa.IsValid() && mip == 0 && layer == 0;
            if (color && g->color.IsValid())
            {
                b.colors[0] = multisampled ? g->msaa : g->color;
                b.mips[0] = multisampled ? 0 : mip;
                b.layers[0] = multisampled ? 0 : layer;
                b.formats.colors[0] = g->colorFormat;
                b.formats.colorCount = 1;
                b.textures[0] = rt.GetEntityId();
            }
            if (depth && g->depth.IsValid())
            {
                b.depth = g->depth;
                b.depthLayer = g->layers > 1 && layer < g->layers ? layer : 0;
                b.formats.depth = g->depthFormat;
            }
            b.formats.samples = multisampled ? g->samples : 1;
            return b;
        }

        // The render texture an identifier names (temporaries by id, the camera's target texture, a texture).
        RenderTexture* TextureOfIdentifier(Executor& e, const RenderTargetIdentifier& id)
        {
            switch (id.type)
            {
            case BuiltinRenderTextureType::PropertyName:
            {
                EntityId* t = e.state.temporaries.Find(id.nameID);
                return t ? EntityIdToObject<RenderTexture>(*t) : nullptr;
            }
            case BuiltinRenderTextureType::CameraTarget:
                return EntityIdToObject<RenderTexture>(e.state.cameraTexture);
            case BuiltinRenderTextureType::CurrentActive:
                return EntityIdToObject<RenderTexture>(e.state.bound.textures[0]);
            default:
                return EntityIdToObject<RenderTexture>(id.texture);
            }
        }

        // An identifier as a target (the screen for the camera target of a camera without a target texture).
        bool TargetOf(Executor& e, const RenderTargetIdentifier& id, bool color, bool depth, BoundTarget& out)
        {
            if (id.type == BuiltinRenderTextureType::None)
                return false;
            if (id.type == BuiltinRenderTextureType::CurrentActive)
            {
                out = e.state.bound;
                return true;
            }
            RenderTexture* rt = TextureOfIdentifier(e, id);
            if (!rt && id.type == BuiltinRenderTextureType::CameraTarget)
            {
                out = ScreenTarget();
                if (!color)
                    out.formats.colorCount = 0;
                return true;
            }
            if (!rt)
            {
                HX_LOG_ERROR(ContextLog, "No render target for identifier (type {}, id {}).", i32(id.type), id.nameID);
                return false;
            }
            out = TextureTarget(*rt, u32(id.mipLevel > 0 ? id.mipLevel : 0), id.Layer(), color, depth);
            return out.width > 0;
        }

        void BindMaterial(Executor& e, const MaterialPassData& data, const PropertySheet* block);

        // One submesh with one pass's data: the camera block, its matrices, the material's blocks (once per pass data
        // in a Submit; a property block makes its own), textures and dynamic keywords.
        void DrawOne(Executor& e, Mesh& mesh, u32 submesh, const MaterialPassData& data, const Matrix4x4& world, u64 worldKey,
                     const PropertySheet* block, const StateOverride* state)
        {
            const ShaderVariant& v = *data.variant;
            const GpuMesh* gm = MeshOf(mesh);
            if (!gm->vertices.IsValid() || !gm->indices.IsValid() || submesh >= gm->subs.Size() || gm->subs[submesh].count == 0)
                return;
            EnsurePass(e);
            UploadCamera(e);
            const GpuMesh::Sub& sub = gm->subs[submesh];
            e.cl.SetPipeline(PipelineOf(v, *gm, sub.topology, state, &e.state.bound.formats));
            u32 k = 0;
            for (const shadercompiler::ReflectedValue& in : v.reflection.vertexInputs)
            {
                if (k >= rhi::kMaxVertexBuffers)
                    break;
                u32 a = in.index < kVertexAttributeCount ? in.index : kNone;
                if (a != kNone && gm->offsets[a] != kNone)
                    e.cl.SetVertexBuffer(k, gm->vertices, gm->offsets[a]);
                else
                    e.cl.SetVertexBuffer(k, gm->zeros, 0);
                k++;
            }
            e.cl.SetIndexBuffer(gm->indices, rhi::IndexType::U32);
            e.cl.SetUniformBuffer(9, e.cameraBlock);
            if (worldKey != e.lastWorld)
            {
                Matrix4x4 m[2] = {world, world.inverse()};
                Upload(m, sizeof(m), e.drawBlock);
                e.lastWorld = worldKey;
            }
            e.cl.SetUniformBuffer(10, e.drawBlock);
            BindMaterial(e, data, block);
            e.cl.DrawIndexed(sub.count, 1, sub.start, sub.baseVertex);
            Stats().drawCalls++;
        }

        // A pass's material blocks (once per pass data in a Submit; a property block makes its own), textures and the
        // variant's dynamic keywords.
        void BindMaterial(Executor& e, const MaterialPassData& data, const PropertySheet* block)
        {
            const ShaderVariant& v = *data.variant;
            const Array<Array<u8>>* bytes = &data.blocks;
            const Array<MaterialPassData::TextureSlot>* slots = &data.textures;
            usize firstUpload;
            if (block)
            {
                e.blocks = data.blocks;
                e.textures = data.textures;
                Material::WriteValues(v, *block, nullptr, true, e.blocks, e.textures);
                bytes = &e.blocks;
                slots = &e.textures;
                firstUpload = e.uploaded.Size();
                for (const Array<u8>& b : *bytes)
                    Upload(b.Data(), u32(b.Size()), e.uploaded.Emplace());
            }
            else if (const u32* at = e.materialBlocks.Find(&data))
                firstUpload = *at;
            else
            {
                firstUpload = e.uploaded.Size();
                e.materialBlocks.Set(&data, u32(firstUpload));
                for (const Array<u8>& b : *bytes)
                    Upload(b.Data(), u32(b.Size()), e.uploaded.Emplace());
            }
            for (u32 b = 0; b < v.layout.blocks.Size(); b++)
                e.cl.SetUniformBuffer(v.layout.blocks[b].slot, e.uploaded[firstUpload + b]);
            for (const MaterialPassData::TextureSlot& t : *slots)
            {
                Texture* tex = EntityIdToObject<Texture>(t.texture);
                const GpuTexture* gt = tex ? TextureOf(*tex) : nullptr;
                if (!gt || !gt->texture.IsValid())
                    gt = TextureOf(*Texture2D::whiteTexture());
                e.cl.SetTexture(t.slot, gt->texture, gt->sampler);
            }
            // The dynamic keywords of the variant.
            for (const shadercompiler::ReflectedBlock& rb : v.reflection.blocks)
                if (rb.name == StringView(shadercompiler::kKeywordBlockName))
                {
                    Array<u8> values;
                    values.Resize(rb.size);
                    std::memset(values.Data(), 0, values.Size());
                    for (const shadercompiler::ReflectedMember& member : rb.members)
                        for (const shadercompiler::KeywordValue& kv : v.keywords)
                            if (member.name == kv.name)
                            {
                                i32 value = i32(kv.value);
                                std::memcpy(values.Data() + member.offset, &value, 4);
                            }
                    rhi::TransientAllocation keywordBlock;
                    Upload(values.Data(), u32(values.Size()), keywordBlock);
                    e.cl.SetUniformBuffer(rb.slot, keywordBlock);
                }
        }

        // Draws a material's passes (pass < 0: every pass, else the shader pass of that index).
        void DrawPasses(Executor& e, Mesh& mesh, u32 submesh, Material* material, i32 pass, const Matrix4x4& world, u64 worldKey,
                        const PropertySheet* block)
        {
            if (!material || !material->shader())
                material = ErrorMaterial();
            if (!material)
                return;
            Shader* shader = material->shader();
            i32 first = pass < 0 ? 0 : pass, last = pass < 0 ? shader->passCount() - 1 : pass;
            if (pass >= shader->passCount())
            {
                HX_LOG_ERROR(ContextLog, "Material '{}': its shader has no pass {}.", material->Name(), pass);
                return;
            }
            for (i32 p = first; p <= last; p++)
                if (const MaterialPassData* data = material->GetPassData(u32(shader->PassIndex(p)), e.shaderTarget))
                    DrawOne(e, mesh, submesh, *data, world, worldKey, block, nullptr);
        }

        // ------------------------------------------------------------------------------------------ DrawRenderers

        struct Item
        {
            u64 key;
            f32 distance;
            u32 tag;
            // A renderer index of the render data, or a queued mesh's index with the top bit.
            u32 source;
            u32 submesh;
            Material* material;
            const MaterialPassData* data;
        };

        constexpr u32 kQueuedBit = 0x80000000u;
        // Light modes the fallback pipeline does not draw.
        constexpr StringView kNotForward[] = {"ShadowCaster", "DepthOnly", "Meta", "MotionVectors"};

        // The draws of one material on one object.
        struct MaterialDraws
        {
            Executor& e;
            const DrawRequest& request;
            Array<Item>& items;
            // Materials made for an override shader, by the material and the shader.
            HashMap<u64, EntityId>& swapped;

            Material* WithOverrideShader(Material& source, Shader& shader)
            {
                u64 key = CombineHash(MixHash(EntityId::ToULong(source.GetEntityId())), MixHash(EntityId::ToULong(shader.GetEntityId())));
                Material* m = nullptr;
                if (EntityId* id = swapped.Find(key))
                    m = EntityIdToObject<Material>(*id);
                if (!m)
                {
                    m = Material::Create(&shader);
                    if (!m)
                        return nullptr;
                    m->hideFlags = HideFlags::HideAndDontSave;
                    swapped.Set(key, m->GetEntityId());
                }
                m->CopyPropertiesFromMaterial(source);
                return m;
            }

            void Push(Material* m, i32 shaderPass, u32 tag, u64 order, f32 distance, u32 source, u32 submesh)
            {
                if (!m || !m->shader() || shaderPass < 0 || shaderPass >= m->shader()->passCount())
                    return;
                const MaterialPassData* data = m->GetPassData(u32(m->shader()->PassIndex(shaderPass)), e.shaderTarget);
                if (!data)
                    return;
                i32 queue = m->renderQueue();
                if (!request.filtering.renderQueueRange.Contains(queue))
                    return;
                SortingCriteria c = request.drawing.sortingSettings.criteria;
                bool backToFront = HasFlag(c, SortingCriteria::BackToFront);
                u64 key = 0;
                if (HasFlag(c, SortingCriteria::RenderQueue))
                    key |= u64(u32(queue) & 0xFFFF) << 48;
                if (HasFlag(c, SortingCriteria::SortingLayer))
                    key |= order << 32;
                if (HasFlag(c, SortingCriteria::OptimizeStateChanges) && !backToFront)
                    key |= u64(data->variant->serial & 0xFFFFFF) << 8;
                f32 d = backToFront ? -distance : HasFlag(c, SortingCriteria::QuantizedFrontToBack) ? distance : 0.0f;
                items.Push({key, d, tag, source, submesh, m, data});
            }

            // The object's passes of the request's names (or the fallback pipeline's forward pass).
            void Add(Material* m, i32 sortingOrder, f32 distance, u32 source, u32 submesh)
            {
                if (!m || !m->shader())
                    m = ErrorMaterial();
                if (!m)
                    return;
                Shader* shader = m->shader();
                const shadercompiler::ShaderDescription* d = shader->Description();
                if (!d)
                    return;
                u64 order = u64(u32(sortingOrder + 0x8000) & 0xFFFF);
                if (request.forward)
                {
                    for (i32 p = 0; p < shader->passCount(); p++)
                    {
                        StringView mode = d->passes[u32(shader->PassIndex(p))].lightMode;
                        bool skipped = false;
                        for (StringView k : kNotForward)
                            skipped |= mode == k;
                        if (!skipped && m->GetShaderPassEnabled(mode))
                        {
                            Push(m, p, 0, order, distance, source, submesh);
                            return;
                        }
                    }
                    return;
                }
                bool matched = false;
                for (u32 t = 0; t < u32(DrawingSettings::maxShaderPasses); t++)
                {
                    ShaderTagId tag = request.drawing.passNames[t];
                    if (tag.id == 0)
                        continue;
                    for (i32 p = 0; p < shader->passCount(); p++)
                    {
                        u32 index = u32(shader->PassIndex(p));
                        if (!(PassTag(*d, index) == tag) || !m->GetShaderPassEnabled(d->passes[index].lightMode))
                            continue;
                        matched = true;
                        if (Material* over = EntityIdToObject<Material>(request.overrideMaterial))
                            Push(over, request.drawing.overrideMaterialPassIndex, t, order, distance, source, submesh);
                        else if (Shader* overShader = EntityIdToObject<Shader>(request.overrideShader))
                            Push(WithOverrideShader(*m, *overShader), request.drawing.overrideShaderPassIndex, t, order, distance, source, submesh);
                        else
                            Push(m, p, t, order, distance, source, submesh);
                        break;
                    }
                }
                if (!matched)
                    if (Material* fallback = EntityIdToObject<Material>(request.fallbackMaterial))
                        Push(fallback, 0, 0, order, distance, source, submesh);
            }
        };

        HashMap<u64, EntityId>& SwappedMaterials()
        {
            static HashMap<u64, EntityId> m;
            return m;
        }

        f32 SortDistance(const SortingSettings& s, Vector3 center)
        {
            switch (s.distanceMetric)
            {
            case DistanceMetric::Orthographic:
            {
                // The camera looks along -z of its space.
                Vector3 forward(-s.worldToCameraMatrix.m20, -s.worldToCameraMatrix.m21, -s.worldToCameraMatrix.m22);
                return Vector3::Dot(center - s.cameraPosition, forward);
            }
            case DistanceMetric::CustomAxis:
                return Vector3::Dot(center, s.customAxis);
            default:
                return (center - s.cameraPosition).magnitude();
            }
        }

        void RunDrawRenderers(Executor& e, const DrawRequest& request)
        {
            if (!ScriptableRenderContext::IsLive(request.culling))
            {
                HX_LOG_ERROR(ContextLog, "DrawRenderers: the culling results are not of a context that renders.");
                return;
            }
            const CullingData& cull = *request.culling;
            RenderData& d = RendererManager::Get().RenderMutable();
            Span<const Queued> queued = QueuedMeshes();
            Array<Item> items;
            MaterialDraws draws{e, request, items, SwappedMaterials()};
            u32 layers = u32(request.filtering.layerMask);
            u32 renderingLayers = request.filtering.renderingLayerMask;
            for (u32 i : cull.renderers)
            {
                if (i >= d.Count() || !(d.layerMask[i] & layers))
                    continue;
                Renderer* r = EntityIdToObject<Renderer>(d.renderer[i]);
                Mesh* mesh = EntityIdToObject<Mesh>(d.mesh[i]);
                if (!r || !mesh || mesh->subMeshCount() == 0 || !(r->renderingLayerMask() & renderingLayers))
                    continue;
                f32 distance = SortDistance(request.drawing.sortingSettings, Vector3(d.centerX[i], d.centerY[i], d.centerZ[i]));
                Span<const PPtr<Material>> materials = r->Materials();
                u32 subs = u32(mesh->subMeshCount());
                // Extra materials draw the last submesh again; submeshes without a material are not drawn.
                for (u32 k = 0; k < materials.Size(); k++)
                    draws.Add(materials[k].Get(), r->sortingOrder(), distance, i, k < subs ? k : subs - 1);
            }
            for (u32 q : cull.queued)
            {
                if (q >= queued.Size())
                    continue;
                const Queued& item = queued[q];
                if (!(item.layerMask & layers) || !(item.renderingLayerMask & renderingLayers))
                    continue;
                Mesh* mesh = EntityIdToObject<Mesh>(item.mesh);
                if (!mesh || item.submesh >= u32(mesh->subMeshCount()))
                    continue;
                draws.Add(EntityIdToObject<Material>(item.material), 0, SortDistance(request.drawing.sortingSettings, item.bounds.center), kQueuedBit | q,
                          item.submesh);
            }
            std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
                return a.key != b.key ? a.key < b.key : a.distance != b.distance ? a.distance < b.distance : a.tag < b.tag;
            });
            StateOverride state;
            bool overridden = request.state.mask != RenderStateMask::Nothing;
            if (overridden)
                state = MakeOverride(request.state);
            for (const Item& item : items)
            {
                bool isQueued = (item.source & kQueuedBit) != 0;
                u32 index = item.source & ~kQueuedBit;
                const Queued* q = isQueued ? &queued[index] : nullptr;
                Renderer* r = q ? nullptr : EntityIdToObject<Renderer>(d.renderer[index]);
                Mesh* mesh = EntityIdToObject<Mesh>(q ? q->mesh : d.mesh[index]);
                if (!mesh)
                    continue;
                const PropertySheet* block = q ? (q->hasBlock ? &q->block : nullptr) : r ? r->PropertyBlock(i32(item.submesh)) : nullptr;
                DrawOne(e, *mesh, item.submesh, *item.data, q ? q->world : d.world[index], u64(item.source), block, overridden ? &state : nullptr);
            }
        }

        // ------------------------------------------------------------------------------------------ the command table

        using Run = void (*)(Executor& e, const Command& c);

        void RunClear(Executor& e, const Command& c)
        {
            const rhi::Viewport& vp = e.state.viewport;
            const BoundTarget& b = e.state.bound;
            bool whole = !e.state.scissorOn && vp.x <= 0 && vp.y <= 0 && vp.width >= f32(b.width) && vp.height >= f32(b.height);
            bool hasColor = b.formats.colorCount > 0;
            if (!c.flag && !c.flag2)
                return;
            if (whole)
            {
                // A render pass clear takes the whole target.
                EndPass(e);
                BeginPass(e, c.flag2 ? rhi::LoadOp::Clear : rhi::LoadOp::Load, c.flag ? rhi::LoadOp::Clear : rhi::LoadOp::Load, c.v, c.f);
                return;
            }
            // A smaller viewport: a triangle over it writing the color and the far depth (or only the depth).
            Shader* clear = Shader::Find("Hidden/InternalClear");
            const ShaderVariant* cv = clear ? clear->GetVariant(c.flag2 && hasColor ? 0u : 1u, {}, e.shaderTarget) : nullptr;
            if (!cv || !cv->ok)
                return;
            EnsurePass(e);
            static const GpuMesh kNoVertices{};
            e.cl.SetPipeline(PipelineOf(*cv, kNoVertices, rhi::PrimitiveTopology::TriangleList, nullptr, &e.state.bound.formats));
            if (c.flag2)
            {
                rhi::TransientAllocation clearBlock;
                Upload(c.v, sizeof(c.v), clearBlock);
                e.cl.SetUniformBuffer(0, clearBlock);
            }
            e.cl.Draw(3);
        }

        void RunViewport(Executor& e, const Command& c)
        {
            e.state.viewport = DeviceRect(e, c.v);
            if (e.open)
                ApplyViewport(e);
        }

        void RunScissor(Executor& e, const Command& c)
        {
            rhi::Viewport r = DeviceRect(e, c.v);
            e.state.scissorOn = true;
            e.state.scissor = {i32(r.x), i32(r.y), u32(r.width > 0 ? r.width : 0), u32(r.height > 0 ? r.height : 0)};
            if (e.open)
                ApplyViewport(e);
        }

        void RunNoScissor(Executor& e, const Command&)
        {
            e.state.scissorOn = false;
            if (e.open)
                ApplyViewport(e);
        }

        void RunViewProjection(Executor& e, const Command& c)
        {
            const Matrix4x4& view = e.list.matrices[c.index];
            PerCamera& cam = e.state.camera;
            cam.v = view;
            cam.p = GpuProjection(e.list.matrices[c.index + 1]);
            cam.vp = cam.p * view;
            Matrix4x4 inverse = view.inverse();
            cam.cameraPos[0] = inverse.m03, cam.cameraPos[1] = inverse.m13, cam.cameraPos[2] = inverse.m23, cam.cameraPos[3] = 1;
            e.cameraDirty = true;
        }

        void RunGlobalFloat(Executor&, const Command& c) { Shader::SetGlobalFloat(c.a, c.f); }
        void RunGlobalInteger(Executor&, const Command& c) { Shader::SetGlobalInteger(c.a, c.b); }
        void RunGlobalVector(Executor&, const Command& c) { Shader::SetGlobalVector(c.a, Vector4(c.v[0], c.v[1], c.v[2], c.v[3])); }
        void RunGlobalMatrix(Executor& e, const Command& c) { Shader::SetGlobalMatrix(c.a, e.list.matrices[c.index]); }
        void RunGlobalTexture(Executor&, const Command& c) { Shader::SetGlobalTexture(c.a, EntityIdToObject<Texture>(c.object)); }

        void RunKeyword(Executor& e, const Command& c)
        {
            if (c.flag)
                Shader::EnableKeyword(e.list.texts[c.index]);
            else
                Shader::DisableKeyword(e.list.texts[c.index]);
        }

        void RunDrawMesh(Executor& e, const Command& c)
        {
            Mesh* mesh = EntityIdToObject<Mesh>(c.object);
            if (!mesh || c.a < 0 || c.a >= mesh->subMeshCount())
                return;
            const PropertySheet* block = c.sheet != kNone ? &e.list.sheets[c.sheet] : nullptr;
            DrawPasses(e, *mesh, u32(c.a), EntityIdToObject<Material>(c.object2), c.b, e.list.matrices[c.index], (1ull << 63) | ++e.drawSerial, block);
        }

        void RunDrawRenderer(Executor& e, const Command& c)
        {
            Renderer* r = EntityIdToObject<Renderer>(c.object);
            if (!r)
                return;
            RenderData& d = RendererManager::Get().RenderMutable();
            u32 index = RendererAccess::Index(*r);
            if (index >= d.Count() || d.renderer[index] != r->GetEntityId())
                return;
            Mesh* mesh = EntityIdToObject<Mesh>(d.mesh[index]);
            if (!mesh || c.a < 0 || c.a >= mesh->subMeshCount())
                return;
            DrawPasses(e, *mesh, u32(c.a), EntityIdToObject<Material>(c.object2), c.b, d.world[index], u64(index), r->PropertyBlock(c.a));
        }

        void RunBeginSample(Executor& e, const Command& c)
        {
            e.state.samples.Push(e.list.texts[c.index]);
            if (profiler::IsCapturing())
                profiler::detail::Begin(MarkerOf(e.list.texts[c.index]));
        }

        void RunEndSample(Executor& e, const Command& c)
        {
            StringView name = e.list.texts[c.index];
            if (e.state.samples.Empty() || StringView(e.state.samples.Back()) != name)
            {
                HX_LOG_ERROR(ContextLog, "CommandBuffer.EndSample('{}'): no BeginSample of that name is open.", name);
                return;
            }
            e.state.samples.Pop();
            if (profiler::IsCapturing())
                profiler::detail::End(MarkerOf(name));
        }

        void RunDrawRenderersCommand(Executor& e, const Command& c) { RunDrawRenderers(e, e.list.draws[c.index]); }

        void RunSetupCamera(Executor& e, const Command& c)
        {
            Camera* camera = EntityIdToObject<Camera>(c.object);
            if (!camera)
                return;
            Camera::SetCurrent(camera);
            // The camera's target: its target texture, else the screen.
            RenderTexture* rt = camera->targetTexture();
            BoundTarget target = rt ? TextureTarget(*rt, 0, 0, true, true) : ScreenTarget();
            if (target.width > 0)
                Bind(e, target);
            e.state.cameraTexture = rt ? rt->GetEntityId() : EntityId();
            PerCamera& cam = e.state.camera;
            Matrix4x4 view = camera->worldToCameraMatrix();
            cam.v = view;
            cam.p = camera->GpuProjectionMatrix();
            cam.vp = cam.p * view;
            Vector3 eye = camera->transform() ? camera->transform()->position() : Vector3();
            cam.cameraPos[0] = eye.x, cam.cameraPos[1] = eye.y, cam.cameraPos[2] = eye.z, cam.cameraPos[3] = 1;
            // The brightest directional light the camera sees, and the ambient light.
            const Light* sun = nullptr;
            f32 brightest = -1;
            u32 mask = camera->cullingMask();
            for (Light* l : Light::EnabledLights())
                if (l->type() == LightType::Directional && l->intensity() > brightest && (l->cullingMask() & mask))
                    sun = l, brightest = l->intensity();
            for (u32 i = 0; i < 4; i++)
                cam.lightDirection[i] = cam.lightColor[i] = 0;
            if (sun && sun->transform())
            {
                Vector3 toLight = -sun->transform()->forward();
                Color col = sun->FinalColor();
                cam.lightDirection[0] = toLight.x, cam.lightDirection[1] = toLight.y, cam.lightDirection[2] = toLight.z, cam.lightDirection[3] = 1;
                cam.lightColor[0] = col.r, cam.lightColor[1] = col.g, cam.lightColor[2] = col.b, cam.lightColor[3] = 1;
            }
            Color ambient = Ambient();
            cam.ambient[0] = ambient.r, cam.ambient[1] = ambient.g, cam.ambient[2] = ambient.b, cam.ambient[3] = 1;
            e.cameraDirty = true;
            Rect pr = camera->pixelRect();
            e.state.cameraWidth = i32(pr.width), e.state.cameraHeight = i32(pr.height);
            const f32 r[4] = {pr.x, pr.y, pr.width, pr.height};
            e.state.viewport = DeviceRect(e, r);
            e.state.scissorOn = false;
            if (e.open)
                ApplyViewport(e);
        }

        void RunRenderObjects(Executor& e, const Command&)
        {
            // The scripts' OnRenderObject draw with the device themselves: what is recorded so far goes first.
            EndPass(e);
            e.device.Submit(e.cl);
            e.cl = rhi::CommandList();
            e.materialBlocks = HashMap<const MaterialPassData*, u32>();
            e.cameraDirty = true;
            e.lastWorld = ~0ull;
            if (detail::gRenderStage)
                detail::gRenderStage(nullptr, detail::RenderStage::RenderObject);
        }

        void RunSetTarget(Executor& e, const Command& c)
        {
            const RenderTargetIdentifier* ids = &e.list.targets[c.index];
            u32 count = u32(c.a);
            BoundTarget b;
            b.formats.colorCount = 0;
            for (u32 i = 0; i < count; i++)
            {
                BoundTarget one;
                // The depth of a single target comes with it unless a depth target is given.
                if (!TargetOf(e, ids[i], true, !c.flag && i == 0, one))
                    continue;
                if (i == 0)
                    b = one, b.formats.colorCount = 0;
                if (one.formats.colorCount > 0)
                {
                    u32 k = b.formats.colorCount++;
                    b.colors[k] = one.colors[0], b.mips[k] = one.mips[0], b.layers[k] = one.layers[0];
                    b.formats.colors[k] = one.formats.colors[0];
                    b.textures[k] = one.textures[0];
                }
            }
            if (c.flag)
            {
                BoundTarget d;
                if (TargetOf(e, ids[count], false, true, d))
                {
                    b.depth = d.depth, b.depthMip = d.depthMip, b.depthLayer = d.depthLayer;
                    b.formats.depth = d.formats.depth;
                    if (b.width == 0)
                        b.width = d.width, b.height = d.height, b.formats.flipped = d.formats.flipped, b.formats.samples = d.formats.samples;
                }
                else
                {
                    b.depth = rhi::TextureHandle();
                    b.formats.depth = rhi::Format::Unknown;
                }
            }
            if (b.width > 0)
                Bind(e, b);
        }

        // A fullscreen triangle with a material's pass and a property block (Blit).
        void DrawFullscreen(Executor& e, Material& material, i32 pass, const PropertySheet& block)
        {
            Shader* shader = material.shader();
            const MaterialPassData* data = shader && pass < shader->passCount() ? material.GetPassData(u32(shader->PassIndex(pass)), e.shaderTarget) : nullptr;
            if (!data)
                return;
            EnsurePass(e);
            UploadCamera(e);
            static const GpuMesh kNoVertices{};
            e.cl.SetPipeline(PipelineOf(*data->variant, kNoVertices, rhi::PrimitiveTopology::TriangleList, nullptr, &e.state.bound.formats));
            e.cl.SetUniformBuffer(9, e.cameraBlock);
            BindMaterial(e, *data, &block);
            e.cl.Draw(3);
        }

        // A texture object for an identifier as a source: render textures, textures, the screen (copied into a temporary).
        Texture* SourceOf(Executor& e, const RenderTargetIdentifier& id, bool* fromScreen = nullptr)
        {
            if (fromScreen)
                *fromScreen = false;
            if (id.type == BuiltinRenderTextureType::BindableTexture)
                return EntityIdToObject<Texture>(id.texture);
            if (RenderTexture* rt = TextureOfIdentifier(e, id))
                return rt;
            if (id.type != BuiltinRenderTextureType::CameraTarget && !(id.type == BuiltinRenderTextureType::CurrentActive))
                return nullptr;
            // The screen: copied into a temporary of its size and format.
            const RenderTarget& screen = Target();
            if (!screen.color.IsValid())
            {
                HX_LOG_ERROR(ContextLog, "Blit: the screen of this device cannot be read.");
                return nullptr;
            }
            RenderTexture* copy = RenderTexture::GetTemporary(i32(screen.width), i32(screen.height), 0, RenderTextureFormat::ARGB32);
            const GpuRenderTarget* g = copy ? RenderTargetOf(*copy) : nullptr;
            if (!g || !g->color.IsValid())
                return nullptr;
            EndPass(e);
            e.cl.CopyTexture(screen.color, 0, 0, g->color, 0, 0);
            e.state.temporaries.Set(-1 - i32(e.state.temporaries.Size()), copy->GetEntityId());
            // Its rows are the screen's (top first), not a texture's.
            if (fromScreen)
                *fromScreen = true;
            return copy;
        }

        void RunBlit(Executor& e, const Command& c)
        {
            const RenderTargetIdentifier& from = e.list.targets[c.index];
            const RenderTargetIdentifier& to = e.list.targets[c.index + 1];
            EndPass(e);
            FinishTarget(e);
            bool fromScreen = false;
            Texture* source = SourceOf(e, from, &fromScreen);
            BoundTarget target;
            if (!TargetOf(e, to, true, false, target))
                return;
            Bind(e, target);
            Material* material = EntityIdToObject<Material>(c.object);
            static EntityId blitMaterial;
            if (!material)
            {
                material = EntityIdToObject<Material>(blitMaterial);
                if (!material)
                    if (Shader* s = Shader::Find("Hidden/BlitCopy"))
                    {
                        material = Material::Create(s);
                        material->SetName("BlitCopy");
                        material->SetPersistent(true);
                        material->hideFlags = HideFlags::HideAndDontSave;
                        blitMaterial = material->GetEntityId();
                    }
            }
            if (!material || !material->shader())
                return;
            PropertySheet block;
            static const i32 mainTex = PropertyToID("_MainTex");
            block.SetTexture(mainTex, source ? source->GetEntityId() : EntityId());
            // A copy of the screen is read upside down (its rows are the screen's).
            block.SetTextureScale(mainTex, Vector2(c.v[0], fromScreen ? -c.v[1] : c.v[1]));
            block.SetTextureOffset(mainTex, Vector2(c.v[2], fromScreen ? 1.0f - c.v[3] : c.v[3]));
            // A fullscreen triangle in clip space (the blit shader takes the camera block's projection: flipped for a texture).
            PerCamera saved = e.state.camera;
            e.state.camera.v = e.state.camera.p = e.state.camera.vp = Matrix4x4::identity;
            e.cameraDirty = true;
            i32 passes = material->shader()->passCount();
            for (i32 p = c.a < 0 ? 0 : c.a; p < (c.a < 0 ? passes : c.a + 1); p++)
                DrawFullscreen(e, *material, p, block);
            e.state.camera = saved;
            e.cameraDirty = true;
        }

        // A texture object's sampled texture and mip count for copies.
        bool CopyTextureOf(Executor& e, const RenderTargetIdentifier& id, rhi::TextureHandle& out, u32& layers)
        {
            Texture* t = SourceOf(e, id);
            if (!t)
                return false;
            if (t->Is<RenderTexture>())
            {
                const GpuRenderTarget* g = RenderTargetOf(static_cast<RenderTexture&>(*t));
                if (!g)
                    return false;
                out = g->color;
                layers = g->layers;
                return out.IsValid();
            }
            const GpuTexture* g = TextureOf(*t);
            out = g ? g->texture : rhi::TextureHandle();
            layers = 1;
            return out.IsValid();
        }

        void RunCopyTexture(Executor& e, const Command& c)
        {
            EndPass(e);
            FinishTarget(e);
            rhi::TextureHandle from, to;
            u32 fromLayers = 1, toLayers = 1;
            if (!CopyTextureOf(e, e.list.targets[c.index], from, fromLayers) || !CopyTextureOf(e, e.list.targets[c.index + 1], to, toLayers))
            {
                HX_LOG_ERROR(ContextLog, "CopyTexture: the source or the destination is no texture.");
                return;
            }
            // A whole texture: each of its layers (mip 0).
            if (c.a < 0)
            {
                for (u32 l = 0; l < fromLayers && l < toLayers; l++)
                    e.cl.CopyTexture(from, 0, l, to, 0, l);
                return;
            }
            e.cl.CopyTexture(from, u32(c.b > 0 ? c.b : 0), u32(c.a), to, u32(c.d > 0 ? c.d : 0), u32(c.c > 0 ? c.c : 0));
        }

        void RunResolveTarget(Executor& e, const Command& c)
        {
            auto* rt = EntityIdToObject<RenderTexture>(c.object);
            auto* target = EntityIdToObject<RenderTexture>(c.object2);
            const GpuRenderTarget* g = rt ? RenderTargetOf(*rt) : nullptr;
            const GpuRenderTarget* t = target ? RenderTargetOf(*target) : g;
            if (!g || !t || !g->msaa.IsValid())
                return;
            EndPass(e);
            e.cl.ResolveTexture(g->msaa, t->color);
        }

        void RunGenerateMips(Executor& e, const Command& c)
        {
            RenderTexture* rt = TextureOfIdentifier(e, e.list.targets[c.index]);
            const GpuRenderTarget* g = rt ? RenderTargetOf(*rt) : nullptr;
            if (!g || g->mips < 2)
            {
                HX_LOG_ERROR(ContextLog, "CommandBuffer.GenerateMips: the texture has no mips.");
                return;
            }
            EndPass(e);
            e.cl.GenerateMips(g->color);
        }

        void RunGetTemporary(Executor& e, const Command& c)
        {
            RenderTextureDescriptor d = e.list.descriptors[c.index];
            // Negative sizes divide the camera's pixel size.
            if (d.width < 0)
                d.width = Max(1, e.state.cameraWidth / -d.width);
            if (d.height < 0)
                d.height = Max(1, e.state.cameraHeight / -d.height);
            if (EntityId* old = e.state.temporaries.Find(c.a))
                RenderTexture::ReleaseTemporary(EntityIdToObject<RenderTexture>(*old));
            RenderTexture* rt = RenderTexture::GetTemporary(d);
            if (!rt)
                return;
            rt->SetFilterMode(FilterMode(c.b));
            e.state.temporaries.Set(c.a, rt->GetEntityId());
        }

        void RunReleaseTemporary(Executor& e, const Command& c)
        {
            EntityId* t = e.state.temporaries.Find(c.a);
            if (!t)
                return;
            // Finished first when it is the target.
            for (EntityId id : e.state.bound.textures)
                if (id == *t)
                {
                    EndPass(e);
                    FinishTarget(e);
                }
            RenderTexture::ReleaseTemporary(EntityIdToObject<RenderTexture>(*t));
            e.state.temporaries.Remove(c.a);
        }

        void RunGlobalTextureTarget(Executor& e, const Command& c)
        {
            Texture* t = SourceOf(e, e.list.targets[c.index]);
            Shader::SetGlobalTexture(c.a, t);
        }

        constexpr Run kRun[] = {
            RunClear,         RunViewport,     RunScissor,     RunNoScissor,    RunViewProjection, RunGlobalFloat,          RunGlobalInteger,
            RunGlobalVector,  RunGlobalMatrix, RunGlobalTexture, RunKeyword,    RunDrawMesh,       RunDrawRenderer,         RunBeginSample,
            RunEndSample,     RunSetTarget,    RunBlit,        RunCopyTexture,  RunResolveTarget,  RunGenerateMips,         RunGetTemporary,
            RunReleaseTemporary, RunGlobalTextureTarget, RunDrawRenderersCommand, RunSetupCamera, RunRenderObjects,
        };
        static_assert(sizeof(kRun) / sizeof(kRun[0]) == usize(Op::Count), "a table entry per command kind");

        void Execute(const CommandList& list, ContextState& state)
        {
            rhi::Device* device = Device();
            if (!device || list.commands.Empty())
                return;
            Executor e{*device, state, list, Graphics::ShaderTarget()};
            for (const Command& c : list.commands)
                kRun[u32(c.op)](e, c);
            EndPass(e);
            FinishTarget(e);
            device->Submit(e.cl);
        }
    }

    // ---------------------------------------------------------------------------------------------- context

    ScriptableRenderContext::ScriptableRenderContext()
    {
        std::lock_guard<std::mutex> lock(L().mutex);
        id_ = NextId();
        L().contexts.Set(id_, this);
    }

    ScriptableRenderContext::~ScriptableRenderContext()
    {
        std::lock_guard<std::mutex> lock(L().mutex);
        L().contexts.Remove(id_);
        for (CullingData* c : culling_)
        {
            L().culling.Remove(u64(reinterpret_cast<uintptr_t>(c)));
            delete c;
        }
        if (state_)
            ReleaseTemporaries(*state_);
        delete state_;
    }

    ScriptableRenderContext* ScriptableRenderContext::Find(u64 id)
    {
        std::lock_guard<std::mutex> lock(L().mutex);
        ScriptableRenderContext** c = L().contexts.Find(id);
        return c ? *c : nullptr;
    }

    bool ScriptableRenderContext::IsLive(const CullingData* data)
    {
        std::lock_guard<std::mutex> lock(L().mutex);
        return data && L().culling.Find(u64(reinterpret_cast<uintptr_t>(data))) != nullptr;
    }

    void ScriptableRenderContext::SetCameras(Span<Camera* const> cameras)
    {
        cameras_.Clear();
        for (Camera* c : cameras)
            cameras_.Push(c);
    }

    namespace
    {
        // Whether a box is on the inner side of a plane.
        bool Inside(const Plane& p, Vector3 center, Vector3 extent)
        {
            f32 reach = std::fabs(p.normal.x) * extent.x + std::fabs(p.normal.y) * extent.y + std::fabs(p.normal.z) * extent.z;
            return Vector3::Dot(p.normal, center) + p.distance + reach >= 0;
        }

        // Camera.current set while scripts hear of the camera, then back.
        struct CurrentCamera
        {
            Camera* saved;
            explicit CurrentCamera(Camera* c) : saved(Camera::current()) { Camera::SetCurrent(c); }
            ~CurrentCamera() { Camera::SetCurrent(saved); }
        };
    }

    CullingResults ScriptableRenderContext::Cull(const ScriptableCullingParameters& parameters)
    {
        i32 planeCount = parameters.cullingPlaneCount;
        if (planeCount < 0 || planeCount > ScriptableCullingParameters::maximumCullingPlanes)
        {
            HX_LOG_ERROR(ContextLog, "ScriptableRenderContext.Cull: {} culling planes (0 to {} are valid).", planeCount,
                         ScriptableCullingParameters::maximumCullingPlanes);
            return {};
        }
        auto* data = new CullingData();
        {
            std::lock_guard<std::mutex> lock(L().mutex);
            L().culling.Set(u64(reinterpret_cast<uintptr_t>(data)), 1);
        }
        culling_.Push(data);
        data->camera = parameters.camera;
        data->origin = parameters.origin;
        // The first six planes through the batched test (missing ones let everything through), the rest one by one.
        Plane six[6];
        for (u32 i = 0; i < 6; i++)
        {
            if (i32(i) < planeCount)
                six[i] = parameters.planes[i];
            else
                six[i].distance = 1e30f;
        }
        RenderData& d = RendererManager::Get().RenderMutable();
        u32 n = d.Count();
        Array<u8> inside;
        inside.Resize(n);
        if (n)
            CullBoundsSoAParallel(six, d.Bounds(), inside.Data());
        u32 mask = u32(parameters.cullingMask);
        for (u32 i = 0; i < n; i++)
        {
            if (!inside[i] || !d.drawn[i] || !(d.layerMask[i] & mask))
                continue;
            Vector3 center(d.centerX[i], d.centerY[i], d.centerZ[i]), extent(d.extentX[i], d.extentY[i], d.extentZ[i]);
            bool in = true;
            for (i32 k = 6; in && k < planeCount; k++)
                in = Inside(parameters.planes[k], center, extent);
            if (!in || !EntityIdToObject<Renderer>(d.renderer[i]))
                continue;
            data->renderers.Push(i);
            d.visible[i] = 1;
            Stats().visible++;
        }
        Span<const Queued> queued = QueuedMeshes();
        for (u32 q = 0; q < queued.Size(); q++)
        {
            const Queued& item = queued[q];
            if ((item.camera.IsValid() && item.camera != parameters.camera) || !(item.layerMask & mask))
                continue;
            bool in = true;
            for (i32 k = 0; in && k < planeCount; k++)
                in = Inside(parameters.planes[k], item.bounds.center, item.bounds.extents);
            if (in)
                data->queued.Push(q);
        }
        // Lights: directional ones always, others when their range reaches inside the planes.
        Camera* camera = EntityIdToObject<Camera>(parameters.camera);
        for (Light* l : Light::EnabledLights())
        {
            GameObject* go = l->gameObject();
            Transform* t = l->transform();
            if (!go || !t || !((1u << u32(go->layer())) & mask))
                continue;
            Vector3 position = t->position();
            f32 range = l->range();
            bool directional = l->type() == LightType::Directional;
            u32 flags = 0;
            if (!directional)
            {
                bool in = true;
                for (i32 k = 0; in && k < planeCount; k++)
                    in = Vector3::Dot(parameters.planes[k].normal, position) + parameters.planes[k].distance + range >= 0;
                if (!in)
                    continue;
                // Planes 4 and 5 of a camera's six are its near and far planes.
                if (planeCount >= 6)
                {
                    if (std::fabs(Vector3::Dot(parameters.planes[4].normal, position) + parameters.planes[4].distance) < range)
                        flags |= 1u;
                    if (std::fabs(Vector3::Dot(parameters.planes[5].normal, position) + parameters.planes[5].distance) < range)
                        flags |= 2u;
                }
            }
            VisibleLight& v = data->lights.Emplace();
            v.lightId = EntityId::ToULong(l->GetEntityId());
            v.lightType = l->type();
            v.finalColor = l->FinalColor();
            v.localToWorldMatrix = t->localToWorldMatrix();
            v.range = range;
            v.spotAngle = l->spotAngle();
            v.flags = flags;
            if (camera)
            {
                Rect pr = camera->pixelRect();
                v.screenRect = pr;
                if (!directional)
                {
                    // The screen rect of the light's sphere: its box's corners on the screen, within the camera.
                    f32 x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
                    bool behind = false;
                    for (u32 k = 0; k < 8; k++)
                    {
                        Vector3 corner = position + Vector3(k & 1 ? range : -range, k & 2 ? range : -range, k & 4 ? range : -range);
                        Vector3 s = camera->WorldToScreenPoint(corner);
                        behind |= s.z <= 0;
                        x0 = std::min(x0, s.x), y0 = std::min(y0, s.y), x1 = std::max(x1, s.x), y1 = std::max(y1, s.y);
                    }
                    if (!behind)
                    {
                        x0 = std::max(x0, pr.x), y0 = std::max(y0, pr.y);
                        x1 = std::min(x1, pr.x + pr.width), y1 = std::min(y1, pr.y + pr.height);
                        v.screenRect = Rect{x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
                    }
                }
            }
        }
        // The camera's visible renderers' scripts hear of it.
        if (camera && detail::gRenderStage)
        {
            CurrentCamera current(camera);
            for (u32 i : data->renderers)
                if (Renderer* r = EntityIdToObject<Renderer>(d.renderer[i]))
                    detail::gRenderStage(r, detail::RenderStage::WillRenderObject);
        }
        return CullingResults{data};
    }

    void ScriptableRenderContext::DrawRenderers(CullingResults cullingResults, const DrawingSettings& drawingSettings, const FilteringSettings& filteringSettings)
    {
        DrawRenderers(cullingResults, drawingSettings, filteringSettings, RenderStateBlock());
    }

    void ScriptableRenderContext::DrawRenderers(CullingResults cullingResults, const DrawingSettings& drawingSettings, const FilteringSettings& filteringSettings,
                                                const RenderStateBlock& stateBlock)
    {
        if (!cullingResults.IsValid())
        {
            HX_LOG_ERROR(ContextLog, "ScriptableRenderContext.DrawRenderers: the culling results are not valid.");
            return;
        }
        Command& c = list_.Add(Op::DrawRenderers);
        c.index = u32(list_.draws.Size());
        DrawRequest& r = list_.draws.Emplace();
        r.culling = cullingResults.data;
        r.drawing = drawingSettings;
        r.overrideMaterial = drawingSettings.overrideMaterial ? drawingSettings.overrideMaterial->GetEntityId() : EntityId();
        r.overrideShader = drawingSettings.overrideShader ? drawingSettings.overrideShader->GetEntityId() : EntityId();
        r.fallbackMaterial = drawingSettings.fallbackMaterial ? drawingSettings.fallbackMaterial->GetEntityId() : EntityId();
        // Pointers do not stay: the ids do.
        r.drawing.overrideMaterial = nullptr;
        r.drawing.overrideShader = nullptr;
        r.drawing.fallbackMaterial = nullptr;
        r.filtering = filteringSettings;
        r.state = stateBlock;
    }

    void ScriptableRenderContext::SetupCameraProperties(Camera* camera, bool stereoSetup)
    {
        if (!camera)
        {
            HX_LOG_ERROR(ContextLog, "ScriptableRenderContext.SetupCameraProperties: the camera is null.");
            return;
        }
        Command& c = list_.Add(Op::SetupCamera);
        c.object = camera->GetEntityId();
        c.flag = stereoSetup;
    }

    void ScriptableRenderContext::ExecuteCommandBuffer(const CommandBuffer& commandBuffer) { list_.Append(commandBuffer.List()); }

    void ScriptableRenderContext::InvokeOnRenderObjectCallback() { list_.Add(Op::RenderObjects); }

    void ScriptableRenderContext::Submit()
    {
        if (!state_)
            state_ = new ContextState(WholeTarget());
        Execute(list_, *state_);
        list_.Clear();
    }

    namespace render
    {
        void ExecuteNow(const CommandList& list, bool intoActive)
        {
            ContextState state = WholeTarget();
            // The active render texture is the target of commands run at once.
            RenderTexture* active = intoActive ? RenderTexture::active() : nullptr;
            if (active)
            {
                BoundTarget b = TextureTarget(*active, 0, 0, true, true);
                if (b.width > 0)
                {
                    state.bound = b;
                    state.viewport = rhi::Viewport();
                    state.viewport.width = f32(b.width), state.viewport.height = f32(b.height);
                    state.cameraTexture = active->GetEntityId();
                    state.cameraWidth = i32(b.width), state.cameraHeight = i32(b.height);
                }
            }
            Execute(list, state);
            ReleaseTemporaries(state);
        }

        void ReleaseSwappedMaterials()
        {
            for (auto e : SwappedMaterials())
                if (Material* m = EntityIdToObject<Material>(e.value))
                    Object::DestroyImmediate(m);
            SwappedMaterials() = HashMap<u64, EntityId>();
        }
    }

    // ---------------------------------------------------------------------------------------------- values

    SortingSettings::SortingSettings(const Camera* camera)
    {
        if (!camera)
            return;
        worldToCameraMatrix = camera->worldToCameraMatrix();
        cameraPosition = camera->transform() ? camera->transform()->position() : Vector3();
        distanceMetric = camera->orthographic() ? DistanceMetric::Orthographic : DistanceMetric::Perspective;
    }

    Light* VisibleLight::light() const { return EntityIdToObject<Light>(EntityId::FromULong(lightId)); }

    Span<const VisibleLight> CullingResults::visibleLights() const
    {
        if (!IsValid())
            return {};
        return Span<const VisibleLight>(data->lights.Data(), data->lights.Size());
    }

    bool CullingResults::IsValid() const { return ScriptableRenderContext::IsLive(data); }

    bool Camera::TryGetCullingParameters(ScriptableCullingParameters& out) const
    {
        Rect pr = pixelRect();
        if (pr.width <= 0 || pr.height <= 0 || nearClipPlane() >= farClipPlane())
            return false;
        out = ScriptableCullingParameters();
        out.cullingMask = i32(cullingMask());
        out.cullingMatrix = cullingMatrix();
        Plane planes[6];
        CalculateFrustumPlanes(out.cullingMatrix, planes);
        for (u32 i = 0; i < 6; i++)
            out.planes[i] = planes[i];
        out.cullingPlaneCount = 6;
        out.origin = transform() ? transform()->position() : Vector3();
        out.isOrthographic = orthographic();
        out.camera = GetEntityId();
        return true;
    }
}
