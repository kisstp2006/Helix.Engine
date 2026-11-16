#include <Helix/Engine/Rendering/Pipeline/ScriptableRenderContext.h>
#include <Helix/Engine/Rendering/RenderSettings.h>
#include <Helix/Engine/Rendering/Pipeline/Shadows.h>
#include <Helix/Engine/Rendering/Pipeline/RenderPipeline.h>
#include <Helix/Engine/Rendering/Implementation/RenderCore.h>
#include <Helix/Engine/Rendering/Implementation/RendererManager.h>
#include <Helix/Engine/Rendering/CullingGroup.h>
#include <Helix/Engine/Rendering/LODGroup.h>
#include <Helix/Engine/Rendering/Light.h>
#include <Helix/Engine/Texture/Texture.h>

#include <Helix/Core/Hashing/Hash.h>
#include <Helix/Core/Logging/Log.h>
#include <Helix/Jobs/Jobs.h>
#include <Helix/Platform/Time/Time.h>
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
            // Culling data of ended contexts, reused (its arrays keep their memory).
            Array<CullingData*> spareCulling = Array<CullingData*>();
            // The renderer lists of live contexts by id.
            HashMap<u64, DrawRequest*> lists;
            u64 next = 0;

            ~Live()
            {
                for (CullingData* c : spareCulling)
                    delete c;
            }
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
                Numbers,
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
                Refers::Matrices, // DrawMeshInstanced
                Refers::Nothing,  // InvertCulling
                Refers::Numbers,  // GlobalArray
                Refers::Nothing,  // DrawRendererList
                Refers::Matrices, // DrawProcedural
                Refers::Nothing,  // Readback
                Refers::Nothing,  // GlobalDepthBias
                Refers::Nothing,  // GlobalBuffer
                Refers::Numbers,  // ComputeValue
                Refers::Targets,  // ComputeTexture
                Refers::Nothing,  // ComputeBuffer
                Refers::Texts,    // ComputeKeyword
                Refers::Nothing,  // DispatchCompute
                Refers::Numbers,  // BufferData
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
            const u32 base[] = {0, u32(matrices.Size()), u32(texts.Size()), u32(draws.Size()), u32(targets.Size()), u32(descriptors.Size()), u32(numbers.Size())};
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
            numbers.Append(other.numbers.Data(), other.numbers.Size());
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
            numbers.Clear();
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

    void CommandBuffer::GlobalArray(i32 nameID, i32 kind, const f32* values, u32 count, u32 width)
    {
        Command& c = list_.Add(Op::GlobalArray);
        c.a = nameID, c.b = kind, c.c = i32(count);
        c.index = u32(list_.numbers.Size());
        if (count)
            list_.numbers.Append(values, usize(count) * width);
    }

    void CommandBuffer::SetGlobalBuffer(i32 nameID, u64 buffer)
    {
        Command& c = list_.Add(Op::GlobalBuffer);
        c.a = nameID;
        c.object = EntityId::FromULong(buffer);
    }

    void CommandBuffer::ComputeValue(ComputeShader* shader, i32 nameID, i32 kind, const f32* values, u32 count, u32 width)
    {
        Command& c = list_.Add(Op::ComputeValue);
        c.object = shader ? shader->GetEntityId() : EntityId();
        c.a = nameID, c.b = kind, c.c = i32(count);
        c.index = u32(list_.numbers.Size());
        if (count)
            list_.numbers.Append(values, usize(count) * width);
    }

    void CommandBuffer::SetComputeIntParams(ComputeShader* shader, i32 nameID, Span<const i32> values)
    {
        Array<f32> f;
        for (i32 v : values)
            f.Push(f32(v));
        ComputeValue(shader, nameID, 5, f.Data(), u32(f.Size()), 1);
    }

    void CommandBuffer::SetComputeTextureParam(ComputeShader* shader, i32 kernelIndex, i32 nameID, const RenderTargetIdentifier& rt, i32 mipLevel)
    {
        Command& c = list_.Add(Op::ComputeTexture);
        c.object = shader ? shader->GetEntityId() : EntityId();
        c.a = kernelIndex, c.b = nameID, c.c = mipLevel;
        c.index = u32(list_.targets.Size());
        list_.targets.Push(rt);
    }

    void CommandBuffer::SetComputeBufferParam(ComputeShader* shader, i32 kernelIndex, i32 nameID, u64 buffer)
    {
        Command& c = list_.Add(Op::ComputeBuffer);
        c.object = shader ? shader->GetEntityId() : EntityId();
        c.a = kernelIndex, c.b = nameID;
        c.object2 = EntityId::FromULong(buffer);
    }

    void CommandBuffer::SetKeyword(ComputeShader* shader, const LocalKeyword& keyword, bool value)
    {
        Command& c = list_.Add(Op::ComputeKeyword);
        c.object = shader ? shader->GetEntityId() : EntityId();
        c.flag = value;
        c.index = u32(list_.texts.Size());
        list_.texts.Push(keyword.name());
    }

    void CommandBuffer::DispatchCompute(ComputeShader* shader, i32 kernelIndex, i32 threadGroupsX, i32 threadGroupsY, i32 threadGroupsZ)
    {
        Command& c = list_.Add(Op::DispatchCompute);
        c.object = shader ? shader->GetEntityId() : EntityId();
        c.a = kernelIndex, c.b = threadGroupsX, c.c = threadGroupsY, c.d = threadGroupsZ;
    }

    void CommandBuffer::DispatchCompute(ComputeShader* shader, i32 kernelIndex, u64 indirectBuffer, u32 argsOffset)
    {
        Command& c = list_.Add(Op::DispatchCompute);
        c.object = shader ? shader->GetEntityId() : EntityId();
        c.a = kernelIndex;
        c.flag = true;
        c.object2 = EntityId::FromULong(indirectBuffer);
        c.index = argsOffset;
    }

    void CommandBuffer::SetBufferData(u64 buffer, const void* data, u32 bytes, u32 bufferOffset)
    {
        Command& c = list_.Add(Op::BufferData);
        c.object = EntityId::FromULong(buffer);
        c.c = i32(bytes), c.d = i32(bufferOffset);
        c.index = u32(list_.numbers.Size());
        // The bytes ride in the number array (rounded up to whole numbers).
        usize at = list_.numbers.Size();
        list_.numbers.Resize(at + (usize(bytes) + 3) / 4);
        if (bytes)
            std::memcpy(list_.numbers.Data() + at, data, bytes);
    }

    void CommandBuffer::SetInvertCulling(bool invertCulling) { list_.Add(Op::InvertCulling).flag = invertCulling; }

    void CommandBuffer::SetGlobalDepthBias(f32 bias, f32 slopeBias)
    {
        Command& c = list_.Add(Op::GlobalDepthBias);
        c.v[0] = bias;
        c.v[1] = slopeBias;
    }

    void CommandBuffer::DrawProcedural(const Matrix4x4& matrix, Material* material, i32 shaderPass, MeshTopology topology, i32 vertexCount, i32 instanceCount,
                                       const MaterialPropertyBlock* properties)
    {
        if (vertexCount <= 0 || instanceCount <= 0)
            return;
        Command& c = list_.Add(Op::DrawProcedural);
        c.object = render::ProceduralMesh(topology, u32(vertexCount))->GetEntityId();
        c.object2 = material ? material->GetEntityId() : EntityId();
        c.b = shaderPass, c.c = instanceCount;
        c.index = u32(list_.matrices.Size());
        list_.matrices.Push(matrix);
        if (properties && !properties->isEmpty())
        {
            c.sheet = u32(list_.sheets.Size());
            list_.sheets.Push(properties->Sheet());
        }
    }

    AsyncGPUReadbackRequest CommandBuffer::RequestAsyncReadback(Texture* source, i32 mipIndex, AsyncGPUReadback::Callback callback, void* user)
    {
        AsyncGPUReadbackRequest r = render::RequestReadbackAfterSubmit(source, mipIndex, callback, user);
        Command& c = list_.Add(Op::Readback);
        c.a = i32(u32(r.Id()));
        c.b = i32(u32(r.Id() >> 32));
        return r;
    }

    void CommandBuffer::DrawRendererList(RendererUtils::RendererList rendererList)
    {
        Command& c = list_.Add(Op::DrawRendererList);
        c.a = i32(u32(rendererList.id));
        c.b = i32(u32(rendererList.id >> 32));
    }

    void CommandBuffer::DrawMeshInstanced(Mesh* mesh, i32 submeshIndex, Material* material, i32 shaderPass, Span<const Matrix4x4> matrices,
                                          const MaterialPropertyBlock* properties)
    {
        if (!mesh || !material)
        {
            HX_LOG_ERROR(ContextLog, "CommandBuffer.DrawMeshInstanced: the {} is null.", !mesh ? "mesh" : "material");
            return;
        }
        if (!material->enableInstancing())
        {
            HX_LOG_ERROR(ContextLog, "CommandBuffer.DrawMeshInstanced: the material '{}' needs to enable instancing.", material->Name());
            return;
        }
        Command& c = list_.Add(Op::DrawMeshInstanced);
        c.object = mesh->GetEntityId();
        c.object2 = material->GetEntityId();
        c.a = submeshIndex, c.b = shaderPass, c.c = i32(matrices.Size());
        c.index = u32(list_.matrices.Size());
        for (const Matrix4x4& m : matrices)
            list_.matrices.Push(m);
        if (properties && !properties->isEmpty())
        {
            c.sheet = u32(list_.sheets.Size());
            list_.sheets.Push(properties->Sheet());
        }
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
            // x: -1 when the bound target is drawn flipped (a texture), else 1; y near, z far, w 1 / far.
            f32 projectionParams[4];
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
            // SetInvertCulling: front faces turned around for the draws.
            bool invertCulling = false;
            // SetGlobalDepthBias.
            i32 depthBias = 0;
            f32 depthBiasSlope = 0;
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
        // What the command list bound last, so repeated bindings are left out (forgotten at each render pass).
        struct Binds
        {
            bool pipelineKnown = false;
            rhi::PipelineHandle pipeline;
            u32 vertexKnown = 0;
            rhi::BufferHandle vertex[rhi::kMaxVertexBuffers];
            u32 vertexOffset[rhi::kMaxVertexBuffers] = {};
            bool indexKnown = false;
            rhi::BufferHandle index;
            u32 uniformKnown = 0;
            rhi::BufferHandle uniform[rhi::kMaxUniformSlots];
            u32 uniformOffset[rhi::kMaxUniformSlots] = {};
            u32 uniformSize[rhi::kMaxUniformSlots] = {};
            u32 textureKnown = 0;
            rhi::TextureHandle texture[rhi::kMaxTextureSlots];
            rhi::SamplerHandle sampler[rhi::kMaxTextureSlots];
        };


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
            // A pass data's uploaded blocks in this submit: the index of the first in `uploaded` (low half) and the pass
            // data's write count they hold (high half; a pass data written again, as by a global set between draws, uploads anew).
            HashMap<const MaterialPassData*, u64> materialBlocks = HashMap<const MaterialPassData*, u64>();
            Array<rhi::TransientAllocation> uploaded = Array<rhi::TransientAllocation>();
            u64 lastWorld = ~0ull;
            ObjectLights lastLights;
            rhi::TransientAllocation drawBlock = rhi::TransientAllocation();
            Array<Array<u8>> blocks = Array<Array<u8>>();
            Array<MaterialPassData::TextureSlot> textures = Array<MaterialPassData::TextureSlot>();
            u64 drawSerial = 0;
            Binds binds;
            // The dynamic keyword block of each variant drawn in this submit.
            HashMap<const ShaderVariant*, rhi::TransientAllocation> keywordBlocks = HashMap<const ShaderVariant*, rhi::TransientAllocation>();
            // The stencil reference set last (the device starts each submit at 0; render passes keep it).
            u32 stencilReference = 0;
            // The target's formats with the facing turned around (SetInvertCulling) and the global depth bias.
            TargetFormats inverted;
        };

        // The formats pipelines of draws are made for: the target's, with the facing turned when culling is inverted.
        const TargetFormats* DrawFormats(Executor& e)
        {
            if (!e.state.invertCulling && e.state.depthBias == 0 && e.state.depthBiasSlope == 0)
                return &e.state.bound.formats;
            e.inverted = e.state.bound.formats;
            e.inverted.flipped = e.inverted.flipped != e.state.invertCulling;
            e.inverted.depthBias = e.state.depthBias;
            e.inverted.depthBiasSlope = e.state.depthBiasSlope;
            return &e.inverted;
        }

        void BindPipeline(Executor& e, rhi::PipelineHandle p)
        {
            if (e.binds.pipelineKnown && e.binds.pipeline == p)
                return;
            e.cl.SetPipeline(p);
            e.binds.pipelineKnown = true;
            e.binds.pipeline = p;
            Stats().setPassCalls++;
        }

        void BindVertex(Executor& e, u32 slot, rhi::BufferHandle b, u32 offset)
        {
            Binds& k = e.binds;
            if ((k.vertexKnown >> slot & 1) && k.vertex[slot] == b && k.vertexOffset[slot] == offset)
                return;
            e.cl.SetVertexBuffer(slot, b, offset);
            k.vertexKnown |= 1u << slot;
            k.vertex[slot] = b;
            k.vertexOffset[slot] = offset;
        }

        void BindIndex(Executor& e, rhi::BufferHandle b)
        {
            if (e.binds.indexKnown && e.binds.index == b)
                return;
            e.cl.SetIndexBuffer(b, rhi::IndexType::U32);
            e.binds.indexKnown = true;
            e.binds.index = b;
        }

        bool UniformBound(Executor& e, u32 slot, rhi::BufferHandle b, u32 offset, u32 size)
        {
            Binds& k = e.binds;
            if ((k.uniformKnown >> slot & 1) && k.uniform[slot] == b && k.uniformOffset[slot] == offset && k.uniformSize[slot] == size)
                return true;
            k.uniformKnown |= 1u << slot;
            k.uniform[slot] = b;
            k.uniformOffset[slot] = offset;
            k.uniformSize[slot] = size;
            return false;
        }

        void BindUniform(Executor& e, u32 slot, const rhi::TransientAllocation& a)
        {
            if (!UniformBound(e, slot, a.buffer, a.offset, a.size))
                e.cl.SetUniformBuffer(slot, a);
        }

        void BindUniform(Executor& e, u32 slot, const UniformPart& p, u32 size)
        {
            if (!UniformBound(e, slot, p.buffer, p.offset, size))
                e.cl.SetUniformBuffer(slot, p.buffer, p.offset, size);
        }

        // The stencil reference of a draw: the override's where it sets stencil, else the pass's.
        void BindStencilReference(Executor& e, const MaterialPassData& data, const StateOverride* state)
        {
            const shadercompiler::RenderState& pass = data.state ? *data.state : *data.variant->state;
            u32 reference = state && HasFlag(state->mask, RenderStateMask::Stencil) ? state->stencilReference : u32(pass.stencilReference);
            if (reference == e.stencilReference)
                return;
            e.cl.SetStencilReference(reference);
            e.stencilReference = reference;
        }

        void BindTexture(Executor& e, u32 slot, rhi::TextureHandle t, rhi::SamplerHandle s)
        {
            Binds& k = e.binds;
            if ((k.textureKnown >> slot & 1) && k.texture[slot] == t && k.sampler[slot] == s)
                return;
            e.cl.SetTexture(slot, t, s);
            k.textureKnown |= 1u << slot;
            k.texture[slot] = t;
            k.sampler[slot] = s;
        }

        void ApplyViewport(Executor& e)
        {
            const rhi::Viewport& vp = e.state.viewport;
            e.cl.SetViewport(vp);
            e.cl.SetScissor(e.state.scissorOn ? e.state.scissor : rhi::ScissorRect{i32(vp.x), i32(vp.y), u32(vp.width), u32(vp.height)});
        }

        void BeginPass(Executor& e, rhi::LoadOp color, rhi::LoadOp depth, const f32* clearColor, f32 clearDepth)
        {
            e.binds = Binds();
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
            cam.projectionParams[0] = e.state.bound.formats.flipped ? -1.0f : 1.0f;
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
        void BindProbe(Executor& e, const ShaderVariant& v, u64 probeTexture);

        // One submesh with one pass's data: the camera block, its matrices, the material's blocks (once per pass data
        // in a Submit; a property block makes its own), textures and dynamic keywords.
        void DrawOne(Executor& e, Mesh& mesh, u32 submesh, const MaterialPassData& data, const Matrix4x4& world, u64 worldKey,
                     const PropertySheet* block, const StateOverride* state, i32 renderIndex = -1, u32 instances = 1,
                     const ObjectLights* objectLights = nullptr, u64 probeTexture = 0)
        {
            static const ObjectLights kNoLights;
            const ObjectLights& lights = objectLights ? *objectLights : kNoLights;
            const ShaderVariant& v = *data.variant;
            const GpuMesh* gm = MeshOf(mesh);
            if (!gm->vertices.IsValid() || !gm->indices.IsValid() || submesh >= gm->subs.Size() || gm->subs[submesh].count == 0)
                return;
            EnsurePass(e);
            UploadCamera(e);
            const GpuMesh::Sub& sub = gm->subs[submesh];
            BindPipeline(e, PipelineOf(v, *gm, sub.topology, state, DrawFormats(e), data.state, data.stateKey));
            BindStencilReference(e, data, state);
            u32 k = 0;
            for (const shadercompiler::ReflectedValue& in : v.reflection.vertexInputs)
            {
                if (k >= rhi::kMaxVertexBuffers)
                    break;
                u32 a = in.index < kVertexAttributeCount ? in.index : kNone;
                if (a != kNone && gm->offsets[a] != kNone)
                    BindVertex(e, k, gm->vertices, gm->offsets[a]);
                else
                    BindVertex(e, k, gm->zeros, 0);
                k++;
            }
            BindIndex(e, gm->indices);
            BindUniform(e, 9, e.cameraBlock);
            bool persistent = PersistentBlocksEnabled();
            if (persistent && renderIndex >= 0)
                BindUniform(e, 10, ObjectBlockPart(e.cl, u32(renderIndex), lights), kDrawBlockBytes);
            else
            {
                if (worldKey != e.lastWorld || !(lights == e.lastLights))
                {
                    struct
                    {
                        Matrix4x4 m[2];
                        ObjectLights lights;
                    } drawBlock{{world, world.inverse()}, lights};
                    Upload(&drawBlock, sizeof(drawBlock), e.drawBlock);
                    e.lastWorld = worldKey;
                    e.lastLights = lights;
                }
                BindUniform(e, 10, e.drawBlock);
            }
            BindMaterial(e, data, block);
            BindProbe(e, v, probeTexture);
            e.cl.DrawIndexed(sub.count, instances, sub.start, sub.baseVertex);
            RenderStats& stats = Stats();
            stats.drawCalls++;
            stats.triangles += u64(sub.count / 3) * instances;
            if (persistent && renderIndex >= 0 && !block)
                stats.persistentDraws++;
        }

        // An object's reflection probe cube in the variant's unity_SpecCube0 (0: the material bound the scene's, the
        // global).
        void BindProbe(Executor& e, const ShaderVariant& v, u64 probeTexture)
        {
            if (!probeTexture)
                return;
            for (const shadercompiler::ReflectedResource& r : v.reflection.resources)
                if (r.kind == shadercompiler::ResourceKind::Texture && r.name == StringView("unity_SpecCube0"))
                {
                    Texture* tex = EntityIdToObject<Texture>(EntityId::FromULong(probeTexture));
                    const GpuTexture* gt = tex ? TextureOf(*tex) : nullptr;
                    if (gt && gt->texture.IsValid())
                        BindTexture(e, r.slot, gt->texture, gt->sampler);
                }
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
            else if (PersistentBlocksEnabled())
            {
                Span<const UniformPart> parts = MaterialBlockParts(e.cl, data);
                for (u32 b = 0; b < v.layout.blocks.Size() && b < parts.Size(); b++)
                    BindUniform(e, v.layout.blocks[b].slot, parts[b], u32(data.blocks[b].Size()));
                firstUpload = ~usize(0);
            }
            else if (const u64* at = e.materialBlocks.Find(&data); at && u32(*at >> 32) == data.writes)
                firstUpload = usize(u32(*at));
            else
            {
                firstUpload = e.uploaded.Size();
                e.materialBlocks.Set(&data, u64(data.writes) << 32 | u64(firstUpload));
                for (const Array<u8>& b : *bytes)
                    Upload(b.Data(), u32(b.Size()), e.uploaded.Emplace());
            }
            if (firstUpload != ~usize(0))
                for (u32 b = 0; b < v.layout.blocks.Size(); b++)
                    BindUniform(e, v.layout.blocks[b].slot, e.uploaded[firstUpload + b]);
            for (const MaterialPassData::TextureSlot& t : *slots)
            {
                Texture* tex = EntityIdToObject<Texture>(t.texture);
                const GpuTexture* gt = tex ? TextureOf(*tex) : nullptr;
                if (!gt || !gt->texture.IsValid())
                    gt = TextureOf(*Texture2D::whiteTexture());
                BindTexture(e, t.slot, gt->texture, gt->sampler);
            }
            // Graphics buffers by name: the property block's, else the globals (Shader.SetGlobalBuffer).
            for (const shadercompiler::ReflectedResource& r : v.reflection.resources)
            {
                if (r.kind != shadercompiler::ResourceKind::StorageBuffer)
                    continue;
                i32 id = PropertyToID(r.name);
                u64 bufferId = block ? block->GetBuffer(id) : 0;
                if (!bufferId && data.values)
                    bufferId = data.values->GetBuffer(id);
                if (!bufferId)
                    bufferId = Shader::Globals().GetBuffer(id);
                GraphicsBuffer* buffer = GraphicsBuffer::Find(bufferId);
                if (!buffer)
                    continue;
                e.cl.SetStorageBuffer(r.slot, buffer->Gpu(), r.writable ? rhi::StorageAccess::ReadWrite : rhi::StorageAccess::ReadOnly);
            }
            // The dynamic keywords of the variant (one block per variant in a submit).
            for (const shadercompiler::ReflectedBlock& rb : v.reflection.blocks)
                if (rb.name == StringView(shadercompiler::kKeywordBlockName))
                {
                    if (const rhi::TransientAllocation* made = e.keywordBlocks.Find(&v))
                    {
                        BindUniform(e, rb.slot, *made);
                        continue;
                    }
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
                    e.keywordBlocks.Set(&v, keywordBlock);
                    BindUniform(e, rb.slot, keywordBlock);
                }
        }

        // The most instances of one draw (ShaderVariables.MaxInstances) and the size of each in their block, which
        // starts with the lights the instances share (within the 16 KiB a uniform block may always have).
        constexpr u32 kMaxInstances = 127;
        constexpr u32 kInstanceBytes = 2 * sizeof(Matrix4x4);
        static_assert(sizeof(ObjectLights) + kMaxInstances * kInstanceBytes <= 16384);

        bool TakesInstancing(const Material& m)
        {
            const Shader* shader = m.shader();
            const shadercompiler::ShaderDescription* d = shader ? shader->Description() : nullptr;
            return m.enableInstancing() && d && d->FindKeyword("INSTANCING_ON");
        }

        // One draw of a submesh for several objects: the INSTANCING_ON variant reads each one's matrices by instance id.
        bool DrawInstanced(Executor& e, Mesh& mesh, u32 submesh, Material& material, const MaterialPassData& data, Span<const Matrix4x4* const> worlds,
                           const StateOverride* state, const ObjectLights* lights = nullptr, u64 probeTexture = 0)
        {
            const MaterialPassData* instanced = material.GetPassData(data.pass, e.shaderTarget, true);
            const GpuMesh* gm = MeshOf(mesh);
            if (!instanced || !gm->vertices.IsValid() || !gm->indices.IsValid() || submesh >= gm->subs.Size() || gm->subs[submesh].count == 0)
                return false;
            const ShaderVariant& v = *instanced->variant;
            EnsurePass(e);
            UploadCamera(e);
            const GpuMesh::Sub& sub = gm->subs[submesh];
            BindPipeline(e, PipelineOf(v, *gm, sub.topology, state, DrawFormats(e), instanced->state, instanced->stateKey));
            BindStencilReference(e, *instanced, state);
            u32 k = 0;
            for (const shadercompiler::ReflectedValue& in : v.reflection.vertexInputs)
            {
                if (k >= rhi::kMaxVertexBuffers)
                    break;
                u32 a = in.index < kVertexAttributeCount ? in.index : kNone;
                if (a != kNone && gm->offsets[a] != kNone)
                    BindVertex(e, k, gm->vertices, gm->offsets[a]);
                else
                    BindVertex(e, k, gm->zeros, 0);
                k++;
            }
            BindIndex(e, gm->indices);
            BindUniform(e, 9, e.cameraBlock);
            // The whole block is bound (its declared size), the instances fill its start.
            rhi::TransientAllocation block = e.device.AllocateTransient(u32(sizeof(ObjectLights)) + kMaxInstances * kInstanceBytes);
            if (!block.data)
                return false;
            u8* out = static_cast<u8*>(block.data);
            ObjectLights shared = lights ? *lights : ObjectLights();
            std::memcpy(out, &shared, sizeof(shared));
            out += sizeof(shared);
            for (const Matrix4x4* w : worlds)
            {
                Matrix4x4 m[2] = {*w, w->inverse()};
                std::memcpy(out, m, kInstanceBytes);
                out += kInstanceBytes;
            }
            BindUniform(e, 10, block);
            e.lastWorld = ~0ull;
            BindMaterial(e, *instanced, nullptr);
            BindProbe(e, v, probeTexture);
            e.cl.DrawIndexed(sub.count, u32(worlds.Size()), sub.start, sub.baseVertex);
            RenderStats& stats = Stats();
            stats.drawCalls++;
            stats.instancedBatches++;
            stats.instances += u32(worlds.Size());
            stats.triangles += u64(sub.count / 3) * worlds.Size();
            return true;
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

        // A draw of the list: its sort key (the criteria's fields, then the distance and the pass name's index) and what
        // it draws.
        struct Item
        {
            u64 hi;
            u64 lo;
            Material* material;
            const MaterialPassData* data;
            // A renderer index of the render data, or a queued mesh's index with the top bit.
            u32 source;
            u32 submesh;
        };

        // What a material draws for a request: its passes of the request's names, with the key bits they bring.
        struct PassTemplate
        {
            Material* material;
            const MaterialPassData* data;
            u64 key;
            u32 tag;
        };

        constexpr u32 kQueuedBit = 0x80000000u;
        // Light modes the fallback pipeline does not draw.
        constexpr StringView kNotForward[] = {"ShadowCaster", "DepthOnly", "Meta", "MotionVectors"};

        // The pass templates of each material a request meets, made once per material.
        struct MaterialDraws
        {
            Executor& e;
            const DrawRequest& request;
            Array<PassTemplate>& templates;
            // Materials made for an override shader, by the material and the shader.
            HashMap<u64, EntityId>& swapped;
            // Material id to its templates' range (begin, end) in `templates`.
            HashMap<u64, u64>& ranges;

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

            void Push(Material* m, i32 shaderPass, u32 tag)
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
                u64 key = 0;
                if (HasFlag(c, SortingCriteria::RenderQueue))
                    key |= u64(u32(queue) & 0xFFFF) << 48;
                if (HasFlag(c, SortingCriteria::OptimizeStateChanges) && !HasFlag(c, SortingCriteria::BackToFront))
                    key |= u64(data->variant->serial & 0xFFFFFF) << 8;
                templates.Push({m, data, key, tag});
            }

            // The material's passes of the request's names (or the fallback pipeline's forward pass).
            void Make(Material* m)
            {
                if (!m || !m->shader())
                    m = ErrorMaterial();
                if (!m)
                    return;
                Shader* shader = m->shader();
                const shadercompiler::ShaderDescription* d = shader->Description();
                if (!d)
                    return;
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
                            Push(m, p, 0);
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
                            Push(over, request.drawing.overrideMaterialPassIndex, t);
                        else if (Shader* overShader = EntityIdToObject<Shader>(request.overrideShader))
                            Push(WithOverrideShader(*m, *overShader), request.drawing.overrideShaderPassIndex, t);
                        else
                            Push(m, p, t);
                        break;
                    }
                }
                if (!matched)
                    if (Material* fallback = EntityIdToObject<Material>(request.fallbackMaterial))
                        Push(fallback, 0, 0);
            }

            // `key`: the material id the range is kept under (a destroyed material's id draws the error material).
            Span<const PassTemplate> Of(Material* m, u64 key = ~0ull)
            {
                u64 id = key != ~0ull ? key : m ? EntityId::ToULong(m->GetEntityId()) : 0;
                u64 range;
                if (const u64* found = ranges.Find(id))
                    range = *found;
                else
                {
                    u64 begin = templates.Size();
                    Make(m);
                    range = begin << 32 | u64(templates.Size());
                    ranges.Set(id, range);
                }
                return Span<const PassTemplate>(templates.Data() + (range >> 32), usize(u32(range) - u32(range >> 32)));
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

        // A float as an unsigned integer of the same order.
        u32 SortableBits(f32 v)
        {
            u32 bits;
            std::memcpy(&bits, &v, 4);
            return bits & 0x80000000u ? ~bits : bits | 0x80000000u;
        }

        // ------------------------------------------------------------------------------------------ radix sort

        using render::SortEntry;

        constexpr u32 kSortChunk = 8192;
        // Below this the sort runs on the calling thread.
        constexpr u32 kParallelSort = 4 * kSortChunk;

        // LSD radix sort by (hi, lo), 8 bits a pass; passes whose digit is the same everywhere are skipped. Large lists
        // count and scatter per chunk on the jobs (each chunk's place within a digit comes from the counts before it).
        void RadixSortEntries(Array<SortEntry>& a, Array<SortEntry>& scratch)
        {
            u32 n = u32(a.Size());
            if (n < 2)
                return;
            scratch.Resize(n);
            bool parallel = n >= kParallelSort && jobs::IsInitialized();
            if (!parallel)
            {
                // Every digit's counts in one pass over the entries, then a scatter per digit that is not the same everywhere.
                static u32 histogram[16][256];
                std::memset(histogram, 0, sizeof(histogram));
                constexpr u32 firstDigit = SortEntry::kIndexBits / 8;
                for (u32 i = 0; i < n; i++)
                {
                    const SortEntry& e = a[i];
                    for (u32 d = firstDigit; d < 8; d++)
                        histogram[d][(e.lo >> (d * 8)) & 0xFF]++;
                    for (u32 d = 0; d < 8; d++)
                        histogram[8 + d][(e.hi >> (d * 8)) & 0xFF]++;
                }
                SortEntry* from = a.Data();
                SortEntry* to = scratch.Data();
                for (u32 d = firstDigit; d < 16; d++)
                {
                    u32* h = histogram[d];
                    bool trivial = false;
                    for (u32 k = 0; k < 256 && !trivial; k++)
                        trivial = h[k] == n;
                    if (trivial)
                        continue;
                    u32 at = 0;
                    for (u32 k = 0; k < 256; k++)
                    {
                        u32 v = h[k];
                        h[k] = at;
                        at += v;
                    }
                    u32 shift = (d & 7) * 8;
                    bool high = d >= 8;
                    for (u32 i = 0; i < n; i++)
                    {
                        const SortEntry& e = from[i];
                        to[h[((high ? e.hi : e.lo) >> shift) & 0xFF]++] = e;
                    }
                    SortEntry* t = from;
                    from = to;
                    to = t;
                }
                if (from != a.Data())
                    std::memcpy(a.Data(), from, usize(n) * sizeof(SortEntry));
                return;
            }
            u32 chunks = parallel ? (n + kSortChunk - 1) / kSortChunk : 1;
            static Array<u32> counts;
            counts.Resize(usize(chunks) * 256);
            SortEntry* from = a.Data();
            SortEntry* to = scratch.Data();
            for (u32 word = 0; word < 2; word++)
                for (u32 shift = word ? 0 : SortEntry::kIndexBits; shift < 64; shift += 8)
                {
                    struct Pass
                    {
                        const SortEntry* from;
                        SortEntry* to;
                        u32* counts;
                        u32 n, word, shift, chunkSize;
                    } pass{from, to, counts.Data(), n, word, shift, parallel ? kSortChunk : n};
                    auto digit = [](const Pass& p, const SortEntry& e) { return u32(((p.word ? e.hi : e.lo) >> p.shift) & 0xFF); };
                    auto count = [digit](const Pass& p, u32 c) {
                        u32* h = p.counts + c * 256;
                        std::memset(h, 0, 256 * sizeof(u32));
                        for (u32 i = c * p.chunkSize, end = Min(p.n, i + p.chunkSize); i < end; i++)
                            h[digit(p, p.from[i])]++;
                    };
                    if (parallel)
                    {
                        const Pass* pp = &pass;
                        jobs::Complete(jobs::ParallelFor(chunks, 1, [pp, count](u32 c) { count(*pp, c); }));
                    }
                    else
                        count(pass, 0);
                    // Skipped when one digit holds every entry.
                    bool trivial = false;
                    for (u32 d = 0; d < 256 && !trivial; d++)
                    {
                        u32 total = 0;
                        for (u32 c = 0; c < chunks; c++)
                            total += counts[c * 256 + d];
                        trivial = total == n;
                    }
                    if (trivial)
                        continue;
                    // Offsets: by digit, then by chunk.
                    u32 at = 0;
                    for (u32 d = 0; d < 256; d++)
                        for (u32 c = 0; c < chunks; c++)
                        {
                            u32 v = counts[c * 256 + d];
                            counts[c * 256 + d] = at;
                            at += v;
                        }
                    auto scatter = [digit](const Pass& p, u32 c) {
                        u32* h = p.counts + c * 256;
                        for (u32 i = c * p.chunkSize, end = Min(p.n, i + p.chunkSize); i < end; i++)
                            p.to[h[digit(p, p.from[i])]++] = p.from[i];
                    };
                    if (parallel)
                    {
                        const Pass* pp = &pass;
                        jobs::Complete(jobs::ParallelFor(chunks, 1, [pp, scatter](u32 c) { scatter(*pp, c); }));
                    }
                    else
                        scatter(pass, 0);
                    SortEntry* t = from;
                    from = to;
                    to = t;
                }
            if (from != a.Data())
                std::memcpy(a.Data(), from, usize(n) * sizeof(SortEntry));
        }

        void RunDrawRenderers(Executor& e, const DrawRequest& request)
        {
            if (!ScriptableRenderContext::IsLive(request.culling))
            {
                HX_LOG_ERROR(ContextLog, "DrawRenderers: the culling results are not of a context that renders.");
                return;
            }
            platform::Stopwatch listTime;
            const CullingData& cull = *request.culling;
            RenderData& d = RendererManager::Get().RenderMutable();
            Span<const Queued> queued = QueuedMeshes();
            // Kept from call to call: their memory is reused.
            static Array<PassTemplate> templates;
            static Array<Item> items;
            static Array<SortEntry> order, scratch;
            static HashMap<u64, u64> ranges;
            templates.Clear();
            items.Clear();
            ranges.Clear();
            MaterialDraws draws{e, request, templates, SwappedMaterials(), ranges};
            u32 layers = u32(request.filtering.layerMask);
            u32 renderingLayers = request.filtering.renderingLayerMask;
            SortingCriteria criteria = request.drawing.sortingSettings.criteria;
            bool backToFront = HasFlag(criteria, SortingCriteria::BackToFront);
            bool byDistance = backToFront || HasFlag(criteria, SortingCriteria::QuantizedFrontToBack);
            bool byOrder = HasFlag(criteria, SortingCriteria::SortingLayer);
            auto add = [&](Material* m, i32 sortingOrder, f32 distance, u32 source, u32 submesh) {
                u64 orderBits = byOrder ? u64(u32(sortingOrder + 0x8000) & 0xFFFF) << 32 : 0;
                u64 distanceBits = byDistance ? u64(SortableBits(backToFront ? -distance : distance)) << 32 : 0;
                for (const PassTemplate& t : draws.Of(m))
                    items.Push({t.key | orderBits, distanceBits | u64(t.tag) << SortEntry::kIndexBits, t.material, t.data, source, submesh});
            };
            // The visible renderers in chunks on the jobs, from the render side data only: first the materials they use
            // (made into pass templates here, where shaders may compile), then their items.
            struct Build
            {
                const RenderData* d;
                const u32* visible;
                u32 count;
                u32 layers, renderingLayers;
                const SortingSettings* sorting;
                bool byOrder, byDistance, backToFront;
                const HashMap<u64, u64>* ranges;
                const PassTemplate* templates;
                Array<u64>* found;
                Array<Item>* out;
                Array<u32>* more;
            };
            constexpr u32 kBuildChunk = 2048;
            const Array<u32>& visible = request.shadow ? request.casters : cull.renderers;
            u32 chunks = (u32(visible.Size()) + kBuildChunk - 1) / kBuildChunk;
            static Array<Array<u64>> found;
            static Array<Array<Item>> chunkItems;
            static Array<Array<u32>> chunkMore;
            while (found.Size() < chunks)
            {
                found.Emplace();
                chunkItems.Emplace();
                chunkMore.Emplace();
            }
            Build build{&d, visible.Data(), u32(visible.Size()), layers, renderingLayers, &request.drawing.sortingSettings, byOrder, byDistance,
                        backToFront, &ranges, nullptr, found.Data(), chunkItems.Data(), chunkMore.Data()};
            auto passes = [](const Build& b, u32 i) {
                const RenderData& rd = *b.d;
                return (rd.layerMask[i] & b.layers) && (rd.renderingLayerMask[i] & b.renderingLayers) && rd.subMeshes[i] > 0 && rd.mesh[i].IsValid();
            };
            auto gather = [passes](const Build& b, u32 c) {
                Array<u64>& ids = b.found[c];
                ids.Clear();
                // A small open addressing set of the ids seen (id + 1, 0 is empty); when it fills, ids may repeat.
                constexpr u32 kSlots = 256;
                u64 seenIds[kSlots] = {};
                u32 used = 0;
                u64 last = ~0ull;
                for (u32 k = c * kBuildChunk, end = Min(b.count, k + kBuildChunk); k < end; k++)
                {
                    u32 i = b.visible[k];
                    if (!passes(b, i))
                        continue;
                    const RenderMaterials& mats = b.d->materials[i];
                    for (u32 m = 0; m < mats.count; m++)
                    {
                        u64 id = EntityId::ToULong(mats.ids[m]);
                        if (id == last)
                            continue;
                        last = id;
                        bool seen = false;
                        if (used < kSlots / 2)
                        {
                            u32 slot = u32(MixHash(id)) & (kSlots - 1);
                            while (seenIds[slot] && seenIds[slot] != id + 1)
                                slot = (slot + 1) & (kSlots - 1);
                            seen = seenIds[slot] != 0;
                            if (!seen)
                            {
                                seenIds[slot] = id + 1;
                                used++;
                            }
                        }
                        if (!seen)
                            ids.Push(id);
                    }
                }
            };
            auto make = [passes](const Build& b, u32 c) {
                Array<Item>& out = b.out[c];
                Array<u32>& more = b.more[c];
                out.Clear();
                more.Clear();
                constexpr u32 kCacheSlots = 64;
                u64 cacheIds[kCacheSlots] = {};
                u64 cacheRanges[kCacheSlots] = {};
                const RenderData& rd = *b.d;
                for (u32 k = c * kBuildChunk, end = Min(b.count, k + kBuildChunk); k < end; k++)
                {
                    u32 i = b.visible[k];
                    if (!passes(b, i))
                        continue;
                    const RenderMaterials& mats = rd.materials[i];
                    if (mats.more)
                    {
                        more.Push(i);
                        continue;
                    }
                    f32 distance = b.byDistance ? SortDistance(*b.sorting, Vector3(rd.centerX[i], rd.centerY[i], rd.centerZ[i])) : 0.0f;
                    u64 orderBits = b.byOrder ? u64(u32(rd.sortingOrder[i] + 0x8000) & 0xFFFF) << 32 : 0;
                    u64 distanceBits = b.byDistance ? u64(SortableBits(b.backToFront ? -distance : distance)) << 32 : 0;
                    u32 subs = rd.subMeshes[i];
                    // Extra materials draw the last submesh again.
                    for (u32 m = 0; m < mats.count; m++)
                    {
                        // The ranges of the materials met in this chunk, direct mapped (id + 1, 0 is empty).
                        u64 id = EntityId::ToULong(mats.ids[m]);
                        u32 slot = u32(MixHash(id)) & (kCacheSlots - 1);
                        if (cacheIds[slot] != id + 1)
                        {
                            const u64* known = b.ranges->Find(id);
                            cacheIds[slot] = id + 1;
                            cacheRanges[slot] = known ? *known : 0;
                        }
                        u64 range = cacheRanges[slot];
                        for (u32 t = u32(range >> 32); t < u32(range); t++)
                        {
                            const PassTemplate& pt = b.templates[t];
                            out.Push({pt.key | orderBits, distanceBits | u64(pt.tag) << SortEntry::kIndexBits, pt.material, pt.data, i, m < subs ? m : subs - 1});
                        }
                    }
                }
            };
            bool parallel = chunks > 1 && jobs::IsInitialized();
            const Build* bp = &build;
            if (parallel)
                jobs::Complete(jobs::ParallelFor(chunks, 1, [bp, gather](u32 c) { gather(*bp, c); }));
            else
                for (u32 c = 0; c < chunks; c++)
                    gather(build, c);
            for (u32 c = 0; c < chunks; c++)
                for (u64 id : found[c])
                    if (!ranges.Find(id))
                        draws.Of(EntityIdToObject<Material>(EntityId::FromULong(id)), id);
            build.templates = templates.Data();
            if (parallel)
                jobs::Complete(jobs::ParallelFor(chunks, 1, [bp, make](u32 c) { make(*bp, c); }));
            else
                for (u32 c = 0; c < chunks; c++)
                    make(build, c);
            for (u32 c = 0; c < chunks; c++)
            {
                items.Append(chunkItems[c].Data(), chunkItems[c].Size());
                // Renderers with more materials than the render side keeps.
                for (u32 i : chunkMore[c])
                    if (Renderer* r = EntityIdToObject<Renderer>(d.renderer[i]))
                    {
                        f32 distance = byDistance ? SortDistance(request.drawing.sortingSettings, Vector3(d.centerX[i], d.centerY[i], d.centerZ[i])) : 0.0f;
                        Span<const PPtr<Material>> materials = r->Materials();
                        u32 subs = d.subMeshes[i];
                        for (u32 k = 0; k < materials.Size(); k++)
                            add(materials[k].Get(), d.sortingOrder[i], distance, i, k < subs ? k : subs - 1);
                    }
            }
            static const Array<u32> noQueued;
            for (u32 q : request.shadow ? noQueued : cull.queued)
            {
                if (q >= queued.Size())
                    continue;
                const Queued& item = queued[q];
                if (!(item.layerMask & layers) || !(item.renderingLayerMask & renderingLayers))
                    continue;
                Mesh* mesh = EntityIdToObject<Mesh>(item.mesh);
                if (!mesh || item.submesh >= u32(mesh->subMeshCount()))
                    continue;
                add(EntityIdToObject<Material>(item.material), 0, SortDistance(request.drawing.sortingSettings, item.bounds.center), kQueuedBit | q, item.submesh);
            }
            order.Resize(items.Size());
            for (u32 i = 0; i < items.Size(); i++)
                order[i] = {items[i].hi, items[i].lo | i};
            RadixSortEntries(order, scratch);
            Stats().drawListTime += listTime.ElapsedMilliseconds();
            StateOverride state;
            bool overridden = request.state.mask != RenderStateMask::Nothing;
            if (overridden)
                state = MakeOverride(request.state);
            static Array<StateOverride> taggedStates;
            taggedStates.Clear();
            for (const TaggedState& t : request.tagged)
                taggedStates.Push(MakeOverride(t.state));
            // The override of an item: the state of its pass's tag value, else the request's.
            auto stateOf = [&](const Item& item) -> const StateOverride* {
                if (!request.tagged.Empty() && item.material && item.material->shader())
                    if (const shadercompiler::ShaderDescription* desc = item.material->shader()->Description())
                    {
                        ShaderTagId value = PassTagValue(*desc, item.data->pass, request.tagName);
                        for (u32 k = 0; k < request.tagged.Size(); k++)
                            if (request.tagged[k].tagValue == value)
                                return &taggedStates[k];
                    }
                return overridden ? &state : nullptr;
            };
            // An item's object: its mesh, world matrix, property block and whether it may join an instanced draw.
            struct Source
            {
                EntityId mesh;
                const Matrix4x4* world;
                const PropertySheet* block;
                u32 layerMask;
            };
            auto sourceOf = [&](const Item& item) {
                u32 index = item.source & ~kQueuedBit;
                if (item.source & kQueuedBit)
                {
                    const Queued& q = queued[index];
                    return Source{q.mesh, &q.world, q.hasBlock ? &q.block : nullptr, q.layerMask};
                }
                Renderer* r = EntityIdToObject<Renderer>(d.renderer[index]);
                return Source{d.mesh[index], &d.world[index], r ? r->PropertyBlock(i32(item.submesh)) : nullptr, d.layerMask[index]};
            };
            bool instancing = request.drawing.enableInstancing;
            u32 batchLayers = request.filtering.batchLayerMask;
            static Array<const Matrix4x4*> worlds;
            // The lights of an item's object when the draw asks for them (none in shadow lists).
            bool wantsLights = !request.shadow && (HasFlag(request.drawing.perObjectData, PerObjectData::LightData) ||
                                                   HasFlag(request.drawing.perObjectData, PerObjectData::LightIndices));
            // And its reflection probe (the cube in probeTexture, 0: the scene's reflection).
            bool wantsProbes = !request.shadow && HasFlag(request.drawing.perObjectData, PerObjectData::ReflectionProbes);
            ObjectLights queuedLights, maskedLights;
            u64 probeTexture = 0;
            auto lightsOf = [&](const Item& item) -> const ObjectLights* {
                probeTexture = 0;
                if (!wantsLights && !wantsProbes)
                    return nullptr;
                u32 index = item.source & ~kQueuedBit;
                const ObjectLights* full;
                if (item.source & kQueuedBit)
                {
                    const Bounds& b = queued[index].bounds;
                    queuedLights = LightsAt(cull, b.center, b.extents);
                    probeTexture = ProbeAt(cull, b.center, b.extents, queuedLights);
                    full = &queuedLights;
                }
                else
                {
                    full = &ObjectLightsOf(cull, index);
                    probeTexture = ObjectProbeTextureOf(cull, index);
                }
                if (wantsLights && wantsProbes)
                    return full;
                maskedLights = *full;
                if (!wantsLights)
                {
                    std::memset(maskedLights.data, 0, sizeof(maskedLights.data));
                    std::memset(maskedLights.indices, 0, sizeof(maskedLights.indices));
                }
                if (!wantsProbes)
                {
                    ObjectLights none;
                    std::memcpy(maskedLights.probe, none.probe, sizeof(none.probe));
                    maskedLights.probe[0] = cull.reflectionIntensity;
                    probeTexture = 0;
                }
                return &maskedLights;
            };
            for (u32 at = 0; at < order.Size();)
            {
                const Item& item = items[order[at].item()];
                Source src = sourceOf(item);
                Mesh* mesh = EntityIdToObject<Mesh>(src.mesh);
                if (!mesh)
                {
                    at++;
                    continue;
                }
                // Consecutive draws of the same mesh, submesh and pass without property blocks share one instanced draw.
                u32 run = 1;
                const ObjectLights* lights = lightsOf(item);
                ObjectLights runLights = lights ? *lights : ObjectLights();
                u64 runProbe = probeTexture;
                if (instancing && !src.block && (src.layerMask & batchLayers) && TakesInstancing(*item.material))
                {
                    worlds.Clear();
                    worlds.Push(src.world);
                    while (at + run < order.Size() && run < kMaxInstances)
                    {
                        const Item& next = items[order[at + run].item()];
                        if (next.data != item.data || next.submesh != item.submesh)
                            break;
                        Source s2 = sourceOf(next);
                        if (s2.mesh != src.mesh || s2.block || !(s2.layerMask & batchLayers))
                            break;
                        // Instances share their lights and probe.
                        if (const ObjectLights* l2 = lightsOf(next); l2 && (!(*l2 == runLights) || probeTexture != runProbe))
                            break;
                        worlds.Push(s2.world);
                        run++;
                    }
                }
                if (run > 1 && DrawInstanced(e, *mesh, item.submesh, *item.material, *item.data, worlds, stateOf(item), &runLights, runProbe))
                {
                    at += run;
                    continue;
                }
                bool isQueued = (item.source & kQueuedBit) != 0;
                u32 index = item.source & ~kQueuedBit;
                DrawOne(e, *mesh, item.submesh, *item.data, *src.world, u64(item.source), src.block, stateOf(item), isQueued ? -1 : i32(index),
                        isQueued ? queued[index].instances : 1u, &runLights, runProbe);
                at++;
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
            e.binds = Binds();
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

        void RunDrawMeshInstanced(Executor& e, const Command& c)
        {
            Mesh* mesh = EntityIdToObject<Mesh>(c.object);
            Material* material = EntityIdToObject<Material>(c.object2);
            if (!mesh || !material || !material->shader() || c.a < 0 || c.a >= mesh->subMeshCount())
                return;
            const PropertySheet* block = c.sheet != kNone ? &e.list.sheets[c.sheet] : nullptr;
            Shader* shader = material->shader();
            i32 first = c.b < 0 ? 0 : c.b, last = c.b < 0 ? shader->passCount() - 1 : Min(c.b, shader->passCount() - 1);
            bool instanced = !block && TakesInstancing(*material);
            static Array<const Matrix4x4*> worlds;
            for (i32 p = first; p <= last; p++)
            {
                const MaterialPassData* data = material->GetPassData(u32(shader->PassIndex(p)), e.shaderTarget);
                if (!data)
                    continue;
                for (u32 at = 0; at < u32(c.c);)
                {
                    u32 n = instanced ? Min(u32(c.c) - at, kMaxInstances) : 1u;
                    worlds.Clear();
                    for (u32 i = 0; i < n; i++)
                        worlds.Push(&e.list.matrices[c.index + at + i]);
                    if (n < 2 || !DrawInstanced(e, *mesh, u32(c.a), *material, *data, worlds, nullptr))
                        for (u32 i = 0; i < n; i++)
                            DrawOne(e, *mesh, u32(c.a), *data, *worlds[i], (1ull << 63) | ++e.drawSerial, block, nullptr);
                    at += n;
                }
            }
        }

        void RunInvertCulling(Executor& e, const Command& c) { e.state.invertCulling = c.flag; }

        void RunGlobalDepthBias(Executor& e, const Command& c)
        {
            e.state.depthBias = i32(std::lround(c.v[0]));
            e.state.depthBiasSlope = c.v[1];
        }

        void RunDrawProcedural(Executor& e, const Command& c)
        {
            Mesh* mesh = EntityIdToObject<Mesh>(c.object);
            Material* material = EntityIdToObject<Material>(c.object2);
            if (!mesh)
                return;
            if (!material || !material->shader())
                material = ErrorMaterial();
            if (!material)
                return;
            Shader* shader = material->shader();
            const PropertySheet* block = c.sheet != kNone ? &e.list.sheets[c.sheet] : nullptr;
            i32 first = c.b < 0 ? 0 : c.b, last = c.b < 0 ? shader->passCount() - 1 : Min(c.b, shader->passCount() - 1);
            for (i32 p = first; p <= last; p++)
                if (const MaterialPassData* data = material->GetPassData(u32(shader->PassIndex(p)), e.shaderTarget))
                    DrawOne(e, *mesh, 0, *data, e.list.matrices[c.index], (1ull << 63) | ++e.drawSerial, block, nullptr, -1, u32(c.c));
        }

        void RunReadback(Executor& e, const Command& c)
        {
            // What is recorded so far must run before the texture is read.
            EndPass(e);
            FinishTarget(e);
            render::ArmReadback(u64(u32(c.a)) | u64(u32(c.b)) << 32);
        }

        // A cube around the origin seen from inside (its faces wound for a camera within it).
        Mesh* SkyboxCube()
        {
            static EntityId id;
            if (Mesh* m = EntityIdToObject<Mesh>(id))
                return m;
            Mesh* m = Mesh::Create();
            m->SetName("SkyboxCube");
            m->SetPersistent(true);
            m->hideFlags = HideFlags::HideAndDontSave;
            const Vector3 v[] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}};
            // Each face's two triangles, clockwise seen from inside.
            const i32 t[] = {0, 3, 2, 0, 2, 1, 4, 5, 6, 4, 6, 7, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5, 3, 7, 6, 3, 6, 2, 0, 1, 5, 0, 5, 4};
            m->SetVertices(Span<const Vector3>(v, 8));
            m->SetTriangles(Span<const i32>(t, 36));
            m->RecalculateBounds();
            id = m->GetEntityId();
            return m;
        }

        // The skybox material on the cube around the camera, sized within its clip planes.
        void RunDrawSkybox(Executor& e, const DrawRequest& request)
        {
            Camera* camera = EntityIdToObject<Camera>(request.skyboxCamera);
            Material* material = EntityIdToObject<Material>(request.skyboxMaterial);
            if (!camera || !material)
                return;
            f32 n = camera->nearClipPlane(), f = camera->farClipPlane();
            f32 scale = (n + f) * 0.5f / 1.7320508f;
            Matrix4x4 world = Matrix4x4::TRS(camera->transform()->position(), Quaternion::identity, Vector3(scale, scale, scale));
            DrawPasses(e, *SkyboxCube(), 0, material, -1, world, (1ull << 63) | ++e.drawSerial, nullptr);
        }

        void RunDrawRendererList(Executor& e, const Command& c)
        {
            u64 id = u64(u32(c.a)) | u64(u32(c.b)) << 32;
            DrawRequest* request = nullptr;
            {
                std::lock_guard<std::mutex> lock(L().mutex);
                if (DrawRequest** r = L().lists.Find(id))
                    request = *r;
            }
            if (!request)
            {
                HX_LOG_ERROR(ContextLog, "DrawRendererList: the renderer list is not valid (its context ended, or it was never made).");
                return;
            }
            if (request->skyboxCamera.IsValid())
                RunDrawSkybox(e, *request);
            else
                RunDrawRenderers(e, *request);
        }

        void RunGlobalArray(Executor& e, const Command& c)
        {
            const f32* v = e.list.numbers.Data() + c.index;
            u32 n = u32(c.c);
            if (c.b == 0)
                Shader::SetGlobalFloatArray(c.a, Span<const f32>(v, n));
            else if (c.b == 1)
                Shader::SetGlobalVectorArray(c.a, Span<const Vector4>(reinterpret_cast<const Vector4*>(v), n));
            else
                Shader::SetGlobalMatrixArray(c.a, Span<const Matrix4x4>(reinterpret_cast<const Matrix4x4*>(v), n));
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
            cam.projectionParams[1] = camera->nearClipPlane(), cam.projectionParams[2] = camera->farClipPlane();
            cam.projectionParams[3] = camera->farClipPlane() > 0 ? 1.0f / camera->farClipPlane() : 0.0f;
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
            {
                platform::Stopwatch submit;
                e.device.Submit(e.cl);
                Stats().submitTime += submit.ElapsedMilliseconds();
            }
            e.cl = rhi::CommandList();
            e.materialBlocks = HashMap<const MaterialPassData*, u64>();
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
            e.cl.SetPipeline(PipelineOf(*data->variant, kNoVertices, rhi::PrimitiveTopology::TriangleList, nullptr, &e.state.bound.formats, data->state, data->stateKey));
            e.cl.SetUniformBuffer(9, e.cameraBlock);
            BindMaterial(e, *data, &block);
            e.cl.Draw(3);
            e.binds = Binds();
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

        void RunGlobalBuffer(Executor&, const Command& c) { Shader::Globals().SetBuffer(c.a, c.object.value); }

        void RunComputeValue(Executor& e, const Command& c)
        {
            ComputeShader* cs = EntityIdToObject<ComputeShader>(c.object);
            if (!cs)
                return;
            const f32* v = e.list.numbers.Data() + c.index;
            u32 n = u32(c.c);
            switch (c.b)
            {
            case 0: cs->SetFloat(c.a, v[0]); break;
            case 1: cs->SetInt(c.a, i32(v[0])); break;
            case 2: cs->SetVector(c.a, Vector4(v[0], v[1], v[2], v[3])); break;
            case 3: cs->SetMatrix(c.a, *reinterpret_cast<const Matrix4x4*>(v)); break;
            case 4:
            case 5: cs->SetFloats(c.a, Span<const f32>(v, n)); break;
            case 6: cs->SetVectorArray(c.a, Span<const Vector4>(reinterpret_cast<const Vector4*>(v), n)); break;
            default: cs->SetMatrixArray(c.a, Span<const Matrix4x4>(reinterpret_cast<const Matrix4x4*>(v), n)); break;
            }
        }

        void RunComputeTexture(Executor& e, const Command& c)
        {
            if (ComputeShader* cs = EntityIdToObject<ComputeShader>(c.object))
                cs->SetTexture(c.a, c.b, SourceOf(e, e.list.targets[c.index]), c.c);
        }

        void RunComputeBuffer(Executor&, const Command& c)
        {
            if (ComputeShader* cs = EntityIdToObject<ComputeShader>(c.object))
                cs->SetBufferId(c.a, c.b, c.object2.value);
        }

        void RunComputeKeyword(Executor& e, const Command& c)
        {
            ComputeShader* cs = EntityIdToObject<ComputeShader>(c.object);
            if (!cs)
                return;
            if (c.flag)
                cs->EnableKeyword(e.list.texts[c.index]);
            else
                cs->DisableKeyword(e.list.texts[c.index]);
        }

        // Compute work runs between render passes; the draws after it bind everything again.
        void RunDispatchCompute(Executor& e, const Command& c)
        {
            ComputeShader* cs = EntityIdToObject<ComputeShader>(c.object);
            if (!cs)
            {
                HX_LOG_ERROR(ContextLog, "DispatchCompute: the compute shader is null.");
                return;
            }
            EndPass(e);
            if (c.flag)
            {
                GraphicsBuffer* args = GraphicsBuffer::Find(c.object2.value);
                if (!args)
                {
                    HX_LOG_ERROR(ContextLog, "DispatchCompute: the arguments buffer is not valid.");
                    return;
                }
                cs->Record(e.cl, c.a, 0, 0, 0, args, c.index);
            }
            else if (c.b > 0 && c.c > 0 && c.d > 0)
                cs->Record(e.cl, c.a, u32(c.b), u32(c.c), u32(c.d));
            e.binds = Binds();
            e.lastWorld = ~0ull;
        }

        void RunBufferData(Executor& e, const Command& c)
        {
            GraphicsBuffer* b = GraphicsBuffer::Find(c.object.value);
            if (!b)
            {
                HX_LOG_ERROR(ContextLog, "SetBufferData: the buffer is not valid.");
                return;
            }
            EndPass(e);
            b->RecordData(e.cl, e.list.numbers.Data() + c.index, u32(c.c), u32(c.d));
        }

        constexpr Run kRun[] = {
            RunClear,         RunViewport,     RunScissor,     RunNoScissor,    RunViewProjection, RunGlobalFloat,          RunGlobalInteger,
            RunGlobalVector,  RunGlobalMatrix, RunGlobalTexture, RunKeyword,    RunDrawMesh,       RunDrawRenderer,         RunBeginSample,
            RunEndSample,     RunSetTarget,    RunBlit,        RunCopyTexture,  RunResolveTarget,  RunGenerateMips,         RunGetTemporary,
            RunReleaseTemporary, RunGlobalTextureTarget, RunDrawRenderersCommand, RunSetupCamera, RunRenderObjects, RunDrawMeshInstanced,
            RunInvertCulling, RunGlobalArray, RunDrawRendererList, RunDrawProcedural, RunReadback, RunGlobalDepthBias,
            RunGlobalBuffer,  RunComputeValue, RunComputeTexture, RunComputeBuffer, RunComputeKeyword, RunDispatchCompute, RunBufferData,
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
            {
                platform::Stopwatch submit;
                device->Submit(e.cl);
                Stats().submitTime += submit.ElapsedMilliseconds();
            }
            render::ReadPendingReadbacks(true);
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
            L().spareCulling.Push(c);
        }
        for (u64 id : lists_)
            if (DrawRequest** r = L().lists.Find(id))
            {
                delete *r;
                L().lists.Remove(id);
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

    namespace
    {
        constexpr u32 kCullChunk = 4096;

        // One culling pass over the render data: six planes through the batched test, the rest per renderer.
        struct CullPass
        {
            const RenderData* d = nullptr;
            u32 count = 0;
            Plane six[6];
            const Plane* extra = nullptr;
            i32 extraCount = 0;
            u32 layerMask = ~0u;
            // Shadow casters instead of drawn renderers.
            bool casters = false;
            const u8* lodLevels = nullptr;
            u32 lodSlots = 0;
            const f32* layerDistances = nullptr;
            bool spherical = false;
            Vector3 origin;
            Vector3 forward;
            // Point and spot lights: casters within the sphere.
            bool sphere = false;
            Vector3 sphereCenter;
            f32 sphereRadius = 0;
            // Per chunk: its indices at out + chunk * kCullChunk, their count in counts[chunk].
            u32* out = nullptr;
            u32* counts = nullptr;
            u8* inside = nullptr;
        };

        void CullChunk(const CullPass& p, u32 chunk)
        {
            const RenderData& d = *p.d;
            u32 begin = chunk * kCullChunk, end = Min(p.count, begin + kCullChunk);
            BoundsSoA bounds{d.centerX.Data() + begin, d.centerY.Data() + begin, d.centerZ.Data() + begin, d.extentX.Data() + begin,
                             d.extentY.Data() + begin, d.extentZ.Data() + begin, end - begin};
            CullBoundsSoA(p.six, bounds, p.inside + begin);
            u32* out = p.out + begin;
            u32 n = 0;
            for (u32 i = begin; i < end; i++)
            {
                if (!p.inside[i] || !(p.casters ? d.castShadows[i] : d.drawn[i]) || !(d.layerMask[i] & p.layerMask))
                    continue;
                if (u32 slot = d.lodSlot[i]; slot != ~0u)
                {
                    u8 level = slot < p.lodSlots ? p.lodLevels[slot] : u8(0xFF);
                    if (level == 0xFF || !((d.lodMask[i] >> level) & 1))
                        continue;
                }
                Vector3 center(d.centerX[i], d.centerY[i], d.centerZ[i]), extent(d.extentX[i], d.extentY[i], d.extentZ[i]);
                bool in = true;
                for (i32 k = 0; in && k < p.extraCount; k++)
                    in = Inside(p.extra[k], center, extent);
                if (!in)
                    continue;
                if (p.layerDistances)
                {
                    u32 layer = 0;
                    for (u32 m = d.layerMask[i]; m > 1; m >>= 1)
                        layer++;
                    f32 limit = p.layerDistances[layer];
                    if (limit > 0)
                    {
                        f32 distance;
                        if (p.spherical)
                            distance = (center - p.origin).magnitude() - extent.magnitude();
                        else
                            distance = Vector3::Dot(p.forward, center - p.origin) -
                                       (std::fabs(p.forward.x) * extent.x + std::fabs(p.forward.y) * extent.y + std::fabs(p.forward.z) * extent.z);
                        if (distance > limit)
                            continue;
                    }
                }
                if (p.sphere)
                {
                    Vector3 q = center - p.sphereCenter;
                    Vector3 c(std::fmax(std::fabs(q.x) - extent.x, 0.0f), std::fmax(std::fabs(q.y) - extent.y, 0.0f), std::fmax(std::fabs(q.z) - extent.z, 0.0f));
                    if (Vector3::Dot(c, c) > p.sphereRadius * p.sphereRadius)
                        continue;
                }
                out[n++] = i;
            }
            p.counts[chunk] = n;
        }

        // Runs a pass over every chunk (on the jobs when there are several) and appends what passed to `result`.
        void RunCullPass(CullPass& p, Array<u32>& result)
        {
            static Array<u32> out, counts;
            static Array<u8> inside;
            u32 chunks = (p.count + kCullChunk - 1) / kCullChunk;
            out.Resize(p.count);
            counts.Resize(chunks);
            inside.Resize(p.count);
            p.out = out.Data();
            p.counts = counts.Data();
            p.inside = inside.Data();
            if (chunks > 1 && jobs::IsInitialized())
            {
                const CullPass* pass = &p;
                jobs::Complete(jobs::ParallelFor(chunks, 1, [pass](u32 chunk) { CullChunk(*pass, chunk); }));
            }
            else
                for (u32 c = 0; c < chunks; c++)
                    CullChunk(p, c);
            for (u32 c = 0; c < chunks; c++)
                result.Append(out.Data() + c * kCullChunk, counts[c]);
        }
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
        CullingData* data;
        {
            std::lock_guard<std::mutex> lock(L().mutex);
            if (!L().spareCulling.Empty())
            {
                data = L().spareCulling.Back();
                L().spareCulling.Pop();
            }
            else
                data = new CullingData();
            L().culling.Set(u64(reinterpret_cast<uintptr_t>(data)), 1);
        }
        data->renderers.Clear();
        data->queued.Clear();
        data->lights.Clear();
        data->probes.Clear();
        data->objectProbeTextures.Clear();
        data->reflectionIntensity = RenderSettings::reflectionIntensity();
        data->casters.Clear();
        data->casterStart.Clear();
        data->lightSplits.Clear();
        for (Array<u32>& split : data->splitCasters)
            split.Clear();
        data->lightIndexMap.Clear();
        data->objectLightSlot.Clear();
        data->objectLights.Clear();
        culling_.Push(data);
        data->camera = parameters.camera;
        data->origin = parameters.origin;
        data->shadowDistance = parameters.shadowDistance;
        data->shadowNearPlaneOffset = parameters.shadowNearPlaneOffset;
        data->conservativeEnclosingSphere = parameters.conservativeEnclosingSphere;
        data->numIterationsEnclosingSphere = parameters.numIterationsEnclosingSphere;
        RenderData& d = RendererManager::Get().RenderMutable();
        // LOD levels of the groups for this camera.
        static Array<u8> lodLevels;
        render::LODSelection lod;
        lod.cameraPosition = parameters.lodParameters.cameraPosition;
        lod.fieldOfView = parameters.lodParameters.fieldOfView;
        lod.orthoSize = parameters.lodParameters.orthoSize;
        lod.orthographic = parameters.lodParameters.isOrthographic;
        lod.bias = QualitySettings::lodBias();
        lod.maximumLevel = QualitySettings::maximumLODLevel();
        render::SelectLODs(lod, lodLevels);
        // The first six planes through the batched test (missing ones let everything through), the rest one by one.
        CullPass pass;
        pass.d = &d;
        pass.count = d.Count();
        for (u32 i = 0; i < 6; i++)
        {
            if (i32(i) < planeCount)
                pass.six[i] = parameters.planes[i];
            else
                pass.six[i].distance = 1e30f;
        }
        pass.extra = parameters.planes + 6;
        pass.extraCount = Max(planeCount - 6, 0);
        pass.layerMask = u32(parameters.cullingMask);
        pass.lodLevels = lodLevels.Data();
        pass.lodSlots = u32(lodLevels.Size());
        bool layerCulling = false;
        for (f32 v : parameters.layerCullDistances)
            layerCulling |= v > 0;
        if (layerCulling)
        {
            pass.layerDistances = parameters.layerCullDistances;
            pass.spherical = parameters.layerCullSpherical;
            pass.origin = parameters.origin;
            // A camera's near plane (the fifth) faces along its view.
            pass.forward = planeCount >= 5 ? parameters.planes[4].normal : Vector3(0, 0, 1);
        }
        RunCullPass(pass, data->renderers);
        for (u32 i : data->renderers)
            d.visible[i] = 1;
        Stats().visible += u32(data->renderers.Size());
        u32 mask = u32(parameters.cullingMask);
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
        // Reflection probes whose box is inside the planes, on the culled layers, in the parameters' order.
        if (HasFlag(parameters.cullingOptions, CullingOptions::NeedsReflectionProbes))
        {
            for (ReflectionProbe* p : ReflectionProbe::EnabledProbes())
            {
                GameObject* go = p->gameObject();
                Transform* t = p->transform();
                if (!go || !t || !((1u << u32(go->layer())) & mask))
                    continue;
                Bounds box = p->bounds();
                bool in = true;
                for (i32 k = 0; in && k < planeCount; k++)
                    in = Inside(parameters.planes[k], box.center, box.extents);
                if (!in)
                    continue;
                VisibleReflectionProbe& v = data->probes.Emplace();
                v.bounds = box;
                v.localToWorldMatrix = t->localToWorldMatrix();
                v.hdrData = p->textureHDRDecodeValues();
                v.center = box.center;
                v.blendDistance = p->blendDistance();
                v.importance = p->importance();
                v.boxProjection = p->boxProjection() ? 1 : 0;
                v.textureId = p->texture() ? EntityId::ToULong(p->texture()->GetEntityId()) : 0;
                v.probeId = EntityId::ToULong(p->GetEntityId());
            }
            ReflectionProbeSortingCriteria criteria = parameters.reflectionProbeSortingCriteria;
            auto volume = [](const VisibleReflectionProbe& v) {
                Vector3 s = v.bounds.size();
                return s.x * s.y * s.z;
            };
            if (criteria != ReflectionProbeSortingCriteria::None)
                std::stable_sort(data->probes.begin(), data->probes.end(), [&](const VisibleReflectionProbe& a, const VisibleReflectionProbe& b) {
                    bool byImportance = criteria != ReflectionProbeSortingCriteria::Size;
                    bool bySize = criteria != ReflectionProbeSortingCriteria::Importance;
                    if (byImportance && a.importance != b.importance)
                        return a.importance > b.importance;
                    return bySize && volume(a) < volume(b);
                });
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
        // At most maximumVisibleLights, directional ones first.
        if (parameters.maximumVisibleLights >= 0 && data->lights.Size() > usize(parameters.maximumVisibleLights))
        {
            std::stable_sort(data->lights.begin(), data->lights.end(), [](const VisibleLight& a, const VisibleLight& b) {
                return (a.lightType == LightType::Directional) > (b.lightType == LightType::Directional);
            });
            data->lights.Resize(usize(parameters.maximumVisibleLights));
        }
        // The shadow casters of each light with shadows: the camera's volume extruded toward a directional light (its
        // planes the light goes through are dropped), a point or spot light's range.
        bool wantCasters = (i32(parameters.cullingOptions) & i32(CullingOptions::ShadowCasters)) != 0;
        for (const VisibleLight& v : data->lights)
        {
            data->casterStart.Push(u32(data->casters.Size()));
            Light* l = v.light();
            if (!wantCasters || !l || l->shadows() == LightShadows::None)
                continue;
            CullPass caster;
            caster.d = &d;
            caster.count = d.Count();
            caster.casters = true;
            caster.layerMask = mask & l->cullingMask();
            caster.lodLevels = lodLevels.Data();
            caster.lodSlots = u32(lodLevels.Size());
            for (Plane& p : caster.six)
                p.distance = 1e30f;
            if (v.lightType == LightType::Directional)
            {
                Vector3 direction = v.localToWorldMatrix.MultiplyVector(Vector3(0, 0, 1));
                u32 kept = 0;
                for (i32 k = 0; k < planeCount && kept < 6; k++)
                    if (Vector3::Dot(parameters.planes[k].normal, direction) <= 0)
                        caster.six[kept++] = parameters.planes[k];
            }
            else
            {
                caster.sphere = true;
                caster.sphereCenter = v.localToWorldMatrix.GetPosition();
                caster.sphereRadius = v.range;
            }
            RunCullPass(caster, data->casters);
        }
        data->casterStart.Push(u32(data->casters.Size()));
        // Culling groups that watch this camera.
        render::EvaluateCullingGroups(parameters.camera, parameters.planes, planeCount);
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

    namespace
    {
        // A draw request from the settings (the ids of override materials and shaders, not their pointers).
        void FillRequest(DrawRequest& r, CullingData* culling, const DrawingSettings& drawingSettings, const FilteringSettings& filteringSettings,
                         const RenderStateBlock& stateBlock)
        {
            r.culling = culling;
            r.drawing = drawingSettings;
            r.overrideMaterial = drawingSettings.overrideMaterial ? drawingSettings.overrideMaterial->GetEntityId() : EntityId();
            r.overrideShader = drawingSettings.overrideShader ? drawingSettings.overrideShader->GetEntityId() : EntityId();
            r.fallbackMaterial = drawingSettings.fallbackMaterial ? drawingSettings.fallbackMaterial->GetEntityId() : EntityId();
            r.drawing.overrideMaterial = nullptr;
            r.drawing.overrideShader = nullptr;
            r.drawing.fallbackMaterial = nullptr;
            r.filtering = filteringSettings;
            r.state = stateBlock;
        }
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
        FillRequest(list_.draws.Emplace(), cullingResults.data, drawingSettings, filteringSettings, stateBlock);
    }

    namespace
    {
        // The target of a render pass attachment: its texture, else the pass's temporary of that index.
        RenderTargetIdentifier AttachmentTarget(const AttachmentDescriptor& a, u32 index)
        {
            if (a.loadStoreTarget.type != BuiltinRenderTextureType::None)
                return a.loadStoreTarget;
            return RenderTargetIdentifier(PropertyToID(Format("_HelixPassAttachment{}", index)));
        }
    }

    void ScriptableRenderContext::BeginRenderPass(i32 width, i32 height, i32 samples, Span<const AttachmentDescriptor> attachments, i32 depthAttachmentIndex)
    {
        if (inRenderPass_)
        {
            HX_LOG_ERROR(ContextLog, "ScriptableRenderContext.BeginRenderPass: a render pass is already open.");
            return;
        }
        if (attachments.Size() > 8 || depthAttachmentIndex >= i32(attachments.Size()))
        {
            HX_LOG_ERROR(ContextLog, "ScriptableRenderContext.BeginRenderPass: {} attachments and the depth at {} (at most 8, the depth among them).",
                         attachments.Size(), depthAttachmentIndex);
            return;
        }
        inRenderPass_ = true;
        passAttachments_.Clear();
        passAttachments_.Append(attachments.Data(), attachments.Size());
        passStarted_.Clear();
        passStarted_.Resize(attachments.Size());
        for (u8& b : passStarted_)
            b = 0;
        passWidth_ = width, passHeight_ = height, passSamples_ = Max(samples, 1), passDepth_ = depthAttachmentIndex;
        // Attachments without a texture of their own: temporaries of the pass.
        CommandBuffer b;
        for (u32 i = 0; i < attachments.Size(); i++)
            if (attachments[i].loadStoreTarget.type == BuiltinRenderTextureType::None)
            {
                RenderTextureDescriptor d(width, height, attachments[i].graphicsFormat, i32(i) == depthAttachmentIndex ? 24 : 0);
                if (i32(i) == depthAttachmentIndex)
                    d.graphicsFormat = GraphicsFormat::None;
                d.msaaSamples = passSamples_;
                b.GetTemporaryRT(AttachmentTarget(attachments[i], i).nameID, d);
            }
        ExecuteCommandBuffer(b);
    }

    void ScriptableRenderContext::BeginSubPass(Span<const i32> colors, Span<const i32> inputs, bool)
    {
        if (!inRenderPass_ || inSubPass_)
        {
            HX_LOG_ERROR(ContextLog, "ScriptableRenderContext.BeginSubPass: {}.", inSubPass_ ? "a subpass is already open" : "no render pass is open");
            return;
        }
        inSubPass_ = true;
        CommandBuffer b;
        // Inputs: the textures of earlier subpasses.
        for (u32 k = 0; k < inputs.Size(); k++)
            if (inputs[k] >= 0 && u32(inputs[k]) < passAttachments_.Size())
                b.SetGlobalTexture(PropertyToID(Format("_HelixInput{}", k)), AttachmentTarget(passAttachments_[u32(inputs[k])], u32(inputs[k])));
        RenderTargetIdentifier targets[8];
        u32 count = 0;
        for (i32 c : colors)
            if (c >= 0 && u32(c) < passAttachments_.Size() && count < 8)
                targets[count++] = AttachmentTarget(passAttachments_[u32(c)], u32(c));
        RenderTargetIdentifier depth = passDepth_ >= 0 ? AttachmentTarget(passAttachments_[u32(passDepth_)], u32(passDepth_))
                                                       : RenderTargetIdentifier(BuiltinRenderTextureType::None);
        if (count)
            b.SetRenderTarget(Span<const RenderTargetIdentifier>(targets, count), depth);
        // The first use of an attachment that clears.
        bool clearColor = false, clearDepth = false;
        Color color;
        f32 depthValue = 1;
        for (i32 c : colors)
            if (c >= 0 && u32(c) < passAttachments_.Size() && !passStarted_[u32(c)])
            {
                passStarted_[u32(c)] = 1;
                if (passAttachments_[u32(c)].loadAction == RenderBufferLoadAction::Clear)
                {
                    clearColor = true;
                    color = passAttachments_[u32(c)].clearColor;
                }
            }
        if (passDepth_ >= 0 && !passStarted_[u32(passDepth_)])
        {
            passStarted_[u32(passDepth_)] = 1;
            if (passAttachments_[u32(passDepth_)].loadAction == RenderBufferLoadAction::Clear)
            {
                clearDepth = true;
                depthValue = passAttachments_[u32(passDepth_)].clearDepth;
            }
        }
        if (clearColor || clearDepth)
            b.ClearRenderTarget(clearDepth, clearColor, color, depthValue);
        ExecuteCommandBuffer(b);
    }

    void ScriptableRenderContext::EndSubPass()
    {
        if (!inSubPass_)
            HX_LOG_ERROR(ContextLog, "ScriptableRenderContext.EndSubPass: no subpass is open.");
        inSubPass_ = false;
    }

    void ScriptableRenderContext::EndRenderPass()
    {
        if (!inRenderPass_)
        {
            HX_LOG_ERROR(ContextLog, "ScriptableRenderContext.EndRenderPass: no render pass is open.");
            return;
        }
        if (inSubPass_)
            EndSubPass();
        CommandBuffer b;
        for (u32 i = 0; i < passAttachments_.Size(); i++)
        {
            const AttachmentDescriptor& a = passAttachments_[i];
            // Resolves into their resolve target.
            if (a.resolveTarget.type != BuiltinRenderTextureType::None &&
                (a.storeAction == RenderBufferStoreAction::Resolve || a.storeAction == RenderBufferStoreAction::StoreAndResolve))
                b.Blit(AttachmentTarget(a, i), a.resolveTarget);
            if (a.loadStoreTarget.type == BuiltinRenderTextureType::None)
                b.ReleaseTemporaryRT(AttachmentTarget(a, i).nameID);
        }
        ExecuteCommandBuffer(b);
        inRenderPass_ = false;
        passAttachments_.Clear();
    }

    RendererUtils::RendererList ScriptableRenderContext::CreateRendererList(const RendererListParams& p)
    {
        if (!p.cullingResults.IsValid())
        {
            HX_LOG_ERROR(ContextLog, "ScriptableRenderContext.CreateRendererList: the culling results are not valid.");
            return {};
        }
        auto* r = new DrawRequest();
        RenderStateBlock everyPass;
        for (const TaggedState& t : p.taggedStates)
            if (t.tagValue.id == 0)
                everyPass = t.state;
        FillRequest(*r, p.cullingResults.data, p.drawSettings, p.filteringSettings, everyPass);
        r->tagName = p.isPassTagName ? ShaderTagId("LightMode") : p.tagName;
        for (const TaggedState& t : p.taggedStates)
            if (t.tagValue.id != 0)
                r->tagged.Push(t);
        std::lock_guard<std::mutex> lock(L().mutex);
        u64 id = NextId();
        L().lists.Set(id, r);
        lists_.Push(id);
        return {id};
    }

    RendererUtils::RendererList ScriptableRenderContext::CreateRendererList(const RendererUtils::RendererListDesc& desc)
    {
        if (!desc.IsValid())
        {
            HX_LOG_ERROR(ContextLog, "ScriptableRenderContext.CreateRendererList: the descriptor needs culling results, a camera and pass names.");
            return {};
        }
        return CreateRendererList(desc.ConvertToParameters());
    }

    void ScriptableRenderContext::AddRendererListState(RendererUtils::RendererList list, const TaggedState& state)
    {
        std::lock_guard<std::mutex> lock(L().mutex);
        DrawRequest** r = L().lists.Find(list.id);
        if (!r)
            return;
        if (state.tagValue.id == 0)
            (*r)->state = state.state;
        else
            (*r)->tagged.Push(state);
    }

    void ScriptableRenderContext::DrawSkybox(Camera* camera)
    {
        RendererUtils::RendererList list = CreateSkyboxRendererList(camera);
        if (!list.isValid())
            return;
        CommandBuffer buffer;
        buffer.DrawRendererList(list);
        ExecuteCommandBuffer(buffer);
    }

    RendererUtils::RendererList ScriptableRenderContext::CreateSkyboxRendererList(Camera* camera)
    {
        if (!camera)
        {
            HX_LOG_ERROR(ContextLog, "ScriptableRenderContext.CreateSkyboxRendererList: the camera is null.");
            return {};
        }
        auto* r = new DrawRequest();
        r->skyboxCamera = camera->GetEntityId();
        if (Material* sky = RenderSettings::skybox())
            r->skyboxMaterial = sky->GetEntityId();
        std::lock_guard<std::mutex> lock(L().mutex);
        u64 id = NextId();
        L().lists.Set(id, r);
        lists_.Push(id);
        return {id};
    }

    void ScriptableRenderContext::CullShadowCasters(CullingResults cullingResults, const ShadowCastersCullingInfos& infos)
    {
        if (!cullingResults.IsValid())
        {
            HX_LOG_ERROR(ContextLog, "ScriptableRenderContext.CullShadowCasters: the culling results are not valid.");
            return;
        }
        CullingData& c = *cullingResults.data;
        const RenderData& d = RendererManager::Get().Render();
        usize lights = c.lights.Size();
        c.lightSplits.Clear();
        c.lightSplits.Resize(lights);
        u32 used = 0;
        for (usize l = 0; l < lights; l++)
        {
            c.lightSplits[l] = -1;
            if (l >= infos.perLightInfos.Size())
                continue;
            const LightShadowCasterCullingInfo& info = infos.perLightInfos[l];
            if (info.splitLength <= 0 || info.splitStart < 0 || usize(info.splitStart + info.splitLength) > infos.splitBuffer.Size())
                continue;
            c.lightSplits[l] = i32(used);
            Span<const u32> casters = cullingResults.ShadowCasters(i32(l));
            for (i32 s = 0; s < info.splitLength; s++, used++)
            {
                while (c.splitCasters.Size() <= used)
                    c.splitCasters.Emplace();
                Array<u32>& kept = c.splitCasters[used];
                kept.Clear();
                const ShadowSplitData& split = infos.splitBuffer[usize(info.splitStart + s)];
                bool all = (info.splitExclusionMask >> s) & 1u;
                Span<const Plane> planes(split.planes, usize(Clamp(split.cullingPlaneCount, 0, ShadowSplitData::maximumCullingPlaneCount)));
                for (u32 i : casters)
                {
                    Bounds b(Vector3(d.centerX[i], d.centerY[i], d.centerZ[i]), Vector3(d.extentX[i], d.extentY[i], d.extentZ[i]) * 2.0f);
                    if (all || planes.Empty() || GeometryUtility::TestPlanes(planes, b))
                        kept.Push(i);
                }
            }
        }
    }

    RendererUtils::RendererList ScriptableRenderContext::CreateShadowRendererList(const ShadowDrawingSettings& settings)
    {
        const CullingResults& cull = settings.cullingResults;
        if (!cull.IsValid() || settings.lightIndex < 0 || usize(settings.lightIndex) >= cull.data->lights.Size())
        {
            HX_LOG_ERROR(ContextLog, "ScriptableRenderContext.CreateShadowRendererList: the culling results or the light index are not valid.");
            return {};
        }
        auto* r = new DrawRequest();
        r->culling = cull.data;
        r->drawing = DrawingSettings(ShaderTagId("ShadowCaster"), SortingSettings());
        r->filtering = FilteringSettings(RenderQueueRange::all());
        r->tagName = ShaderTagId("LightMode");
        r->shadow = true;
        const CullingData& c = *cull.data;
        i32 first = usize(settings.lightIndex) < c.lightSplits.Size() ? c.lightSplits[usize(settings.lightIndex)] : -1;
        if (first >= 0 && usize(first + settings.splitIndex) < c.splitCasters.Size())
            r->casters = c.splitCasters[usize(first + settings.splitIndex)];
        else
            for (u32 i : cull.ShadowCasters(settings.lightIndex))
                r->casters.Push(i);
        std::lock_guard<std::mutex> lock(L().mutex);
        u64 id = NextId();
        L().lists.Set(id, r);
        lists_.Push(id);
        return {id};
    }

    bool RendererUtils::RendererList::isValid() const
    {
        std::lock_guard<std::mutex> lock(L().mutex);
        return id != 0 && L().lists.Find(id) != nullptr;
    }

    RendererUtils::RendererListDesc::RendererListDesc(ShaderTagId passName, CullingResults culling, Camera* c) : cullingResults(culling), camera(c)
    {
        passNames.Push(passName);
    }

    RendererUtils::RendererListDesc::RendererListDesc(Span<const ShaderTagId> names, CullingResults culling, Camera* c) : cullingResults(culling), camera(c)
    {
        passNames.Append(names.Data(), names.Size());
    }

    RendererListParams RendererUtils::RendererListDesc::ConvertToParameters() const
    {
        SortingSettings sorting(camera);
        sorting.criteria = sortingCriteria;
        DrawingSettings drawing(passNames.Empty() ? ShaderTagId() : passNames[0], sorting);
        for (u32 i = 1; i < passNames.Size() && i < u32(DrawingSettings::maxShaderPasses); i++)
            drawing.SetShaderPassName(i32(i), passNames[i]);
        drawing.perObjectData = rendererConfiguration;
        drawing.overrideMaterial = overrideMaterial;
        drawing.overrideMaterialPassIndex = overrideMaterialPassIndex;
        drawing.overrideShader = overrideShader;
        drawing.overrideShaderPassIndex = overrideShaderPassIndex;
        FilteringSettings filtering(renderQueueRange, layerMask, renderingLayerMask, excludeObjectMotionVectors);
        RendererListParams p(cullingResults, drawing, filtering);
        if (hasStateBlock)
            p.taggedStates.Push({ShaderTagId(), stateBlock});
        return p;
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
        switch (GraphicsSettings::transparencySortMode())
        {
        case TransparencySortMode::Perspective: distanceMetric = DistanceMetric::Perspective; break;
        case TransparencySortMode::Orthographic: distanceMetric = DistanceMetric::Orthographic; break;
        case TransparencySortMode::CustomAxis:
            distanceMetric = DistanceMetric::CustomAxis;
            customAxis = GraphicsSettings::transparencySortAxis();
            break;
        default: distanceMetric = camera->orthographic() ? DistanceMetric::Orthographic : DistanceMetric::Perspective; break;
        }
    }

    render::ObjectLights render::LightsAt(const CullingData& culling, const Vector3& center, const Vector3& extent)
    {
        // The best lights so far, by importance (higher first; equal ones keep the visible lights' order).
        struct Candidate
        {
            f32 importance;
            i32 index;
        };
        Candidate best[ObjectLights::kMax];
        u32 count = 0;
        f32 radius = extent.magnitude();
        for (u32 l = 0; l < culling.lights.Size(); l++)
        {
            i32 mapped = culling.lightIndexMap.Empty() ? i32(l) : l < culling.lightIndexMap.Size() ? culling.lightIndexMap[l] : -1;
            if (mapped < 0)
                continue;
            const VisibleLight& v = culling.lights[l];
            f32 brightness = v.finalColor.grayscale();
            f32 importance;
            if (v.lightType == LightType::Directional)
                importance = 1e30f;
            else if (v.lightType == LightType::Point || v.lightType == LightType::Spot)
            {
                // The distance from the light to the bounds, within its range.
                Vector3 position = v.localToWorldMatrix.GetPosition();
                Vector3 q = position - center;
                Vector3 outside(std::fmax(std::fabs(q.x) - extent.x, 0.0f), std::fmax(std::fabs(q.y) - extent.y, 0.0f),
                                std::fmax(std::fabs(q.z) - extent.z, 0.0f));
                f32 distance2 = Vector3::Dot(outside, outside);
                f32 range2 = v.range * v.range;
                if (v.range <= 0 || distance2 > range2)
                    continue;
                if (v.lightType == LightType::Spot)
                {
                    // The bounds' sphere against the cone.
                    Vector3 axis = Vector3(v.localToWorldMatrix.GetColumn(2)).normalized();
                    Vector3 toCenter = center - position;
                    f32 along = Vector3::Dot(toCenter, axis);
                    f32 across = std::sqrt(std::fmax(Vector3::Dot(toCenter, toCenter) - along * along, 0.0f));
                    f32 half = v.spotAngle * 0.5f * mathf::Deg2Rad;
                    if (std::cos(half) * across - along * std::sin(half) > radius || along < -radius)
                        continue;
                }
                // The reference's attenuation of the light at the bounds.
                importance = brightness / (1.0f + 25.0f * distance2 / range2);
            }
            else
                continue;
            if (v.lightType == LightType::Directional)
                importance += brightness;
            u32 at = count;
            while (at > 0 && best[at - 1].importance < importance)
                at--;
            if (at >= ObjectLights::kMax)
                continue;
            for (u32 k = Min(count, ObjectLights::kMax - 1); k > at; k--)
                best[k] = best[k - 1];
            best[at] = {importance, mapped};
            count = Min(count + 1, ObjectLights::kMax);
        }
        ObjectLights out;
        out.data[1] = f32(count);
        for (u32 k = 0; k < count; k++)
            out.indices[k] = f32(best[k].index);
        return out;
    }

    const render::ObjectLights& render::ObjectLightsOf(const CullingData& culling, u32 renderIndex)
    {
        const RenderData& d = RendererManager::Get().Render();
        if (renderIndex >= d.Count())
        {
            static const ObjectLights kNoLights;
            return kNoLights;
        }
        if (culling.objectLightSlot.Size() < d.Count())
        {
            usize from = culling.objectLightSlot.Size();
            culling.objectLightSlot.Resize(d.Count());
            for (usize i = from; i < d.Count(); i++)
                culling.objectLightSlot[i] = ~0u;
        }
        u32& slot = culling.objectLightSlot[renderIndex];
        if (slot == ~0u)
        {
            slot = u32(culling.objectLights.Size());
            Vector3 center(d.centerX[renderIndex], d.centerY[renderIndex], d.centerZ[renderIndex]);
            Vector3 extent(d.extentX[renderIndex], d.extentY[renderIndex], d.extentZ[renderIndex]);
            ObjectLights lights = LightsAt(culling, center, extent);
            u64 texture = ProbeAt(culling, center, extent, lights);
            culling.objectLights.Push(lights);
            culling.objectProbeTextures.Push(texture);
        }
        return culling.objectLights[slot];
    }

    u64 render::ProbeAt(const CullingData& culling, const Vector3& center, const Vector3& extent, ObjectLights& lights)
    {
        f32* p = lights.probe;
        const f32 none[16] = {culling.reflectionIntensity, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0};
        std::memcpy(p, none, sizeof(none));
        for (const VisibleReflectionProbe& v : culling.probes)
        {
            Vector3 lo = v.bounds.min(), hi = v.bounds.max();
            if (center.x + extent.x < lo.x || center.x - extent.x > hi.x || center.y + extent.y < lo.y || center.y - extent.y > hi.y ||
                center.z + extent.z < lo.z || center.z - extent.z > hi.z || !v.textureId)
                continue;
            Vector3 position = v.localToWorldMatrix.GetPosition();
            const f32 found[16] = {v.hdrData.x, v.hdrData.y, v.hdrData.z, v.hdrData.w, hi.x, hi.y, hi.z, v.blendDistance, lo.x, lo.y, lo.z, 1,
                                   position.x, position.y, position.z, v.isBoxProjection() ? 1.0f : 0.0f};
            std::memcpy(p, found, sizeof(found));
            return v.textureId;
        }
        return 0;
    }

    u64 render::ObjectProbeTextureOf(const CullingData& culling, u32 renderIndex)
    {
        if (renderIndex >= culling.objectLightSlot.Size())
            return 0;
        u32 slot = culling.objectLightSlot[renderIndex];
        return slot < culling.objectProbeTextures.Size() ? culling.objectProbeTextures[slot] : 0;
    }

    Array<i32> CullingResults::GetLightIndexMap() const
    {
        Array<i32> map;
        if (!IsValid())
            return map;
        map.Resize(data->lights.Size());
        for (u32 l = 0; l < map.Size(); l++)
            map[l] = data->lightIndexMap.Empty() ? i32(l) : l < data->lightIndexMap.Size() ? data->lightIndexMap[l] : -1;
        return map;
    }

    void CullingResults::SetLightIndexMap(Span<const i32> lightIndexMap)
    {
        if (!IsValid())
            return;
        if (lightIndexMap.Size() != data->lights.Size())
        {
            HX_LOG_ERROR(ContextLog, "CullingResults.SetLightIndexMap: the map has {} entries, there are {} visible lights.", lightIndexMap.Size(),
                         data->lights.Size());
            return;
        }
        data->lightIndexMap.Clear();
        data->lightIndexMap.Append(lightIndexMap.Data(), lightIndexMap.Size());
        data->objectLightSlot.Clear();
        data->objectLights.Clear();
    }

    i32 CullingResults::lightIndexCount() const
    {
        if (!IsValid())
            return 0;
        i32 total = 0;
        for (u32 i : data->renderers)
            total += i32(render::ObjectLightsOf(*data, i).data[1]);
        return total;
    }

    Light* VisibleLight::light() const { return EntityIdToObject<Light>(EntityId::FromULong(lightId)); }

    Texture* VisibleReflectionProbe::texture() const { return EntityIdToObject<Texture>(EntityId::FromULong(textureId)); }

    ReflectionProbe* VisibleReflectionProbe::reflectionProbe() const { return EntityIdToObject<ReflectionProbe>(EntityId::FromULong(probeId)); }

    Span<const VisibleReflectionProbe> CullingResults::visibleReflectionProbes() const
    {
        if (!IsValid())
            return {};
        return Span<const VisibleReflectionProbe>(data->probes.Data(), data->probes.Size());
    }

    i32 CullingResults::lightAndReflectionProbeIndexCount() const
    {
        if (!IsValid())
            return 0;
        Array<ReflectionProbe*> visible;
        for (const VisibleReflectionProbe& v : data->probes)
            if (ReflectionProbe* p = v.reflectionProbe())
                visible.Push(p);
        const RenderData& d = RendererManager::Get().Render();
        i32 total = lightIndexCount();
        for (u32 i : data->renderers)
        {
            Renderer* r = EntityIdToObject<Renderer>(d.renderer[i]);
            if (!r || visible.Empty())
                continue;
            Bounds b(Vector3(d.centerX[i], d.centerY[i], d.centerZ[i]), Vector3(d.extentX[i], d.extentY[i], d.extentZ[i]) * 2.0f);
            ReflectionProbeBlendInfo found[2];
            total += i32(ReflectionProbe::ClosestProbes(b, r->reflectionProbeUsage(), visible, found));
        }
        return total;
    }

    bool CullingResults::FillLightAndReflectionProbeIndices(GraphicsBuffer* buffer) const
    {
        if (!IsValid() || !buffer)
        {
            HX_LOG_ERROR(ContextLog, "FillLightAndReflectionProbeIndices: the culling results or the buffer are not valid.");
            return false;
        }
        Array<ReflectionProbe*> visible;
        for (const VisibleReflectionProbe& v : data->probes)
            if (ReflectionProbe* p = v.reflectionProbe())
                visible.Push(p);
        const RenderData& d = RendererManager::Get().Render();
        Array<i32> indices;
        for (u32 i : data->renderers)
        {
            const render::ObjectLights& lights = render::ObjectLightsOf(*data, i);
            for (u32 k = 0; k < u32(lights.data[1]) && k < render::ObjectLights::kMax; k++)
                indices.Push(i32(lights.indices[k]));
            Renderer* r = EntityIdToObject<Renderer>(d.renderer[i]);
            if (!r || visible.Empty())
                continue;
            Bounds b(Vector3(d.centerX[i], d.centerY[i], d.centerZ[i]), Vector3(d.extentX[i], d.extentY[i], d.extentZ[i]) * 2.0f);
            ReflectionProbeBlendInfo found[2];
            u32 n = ReflectionProbe::ClosestProbes(b, r->reflectionProbeUsage(), visible, found);
            for (u32 k = 0; k < n; k++)
                for (u32 v = 0; v < visible.Size(); v++)
                    if (visible[v] == found[k].probe)
                        indices.Push(i32(v));
        }
        u32 bytes = u32(indices.Size() * 4);
        if (bytes > buffer->sizeInBytes())
        {
            HX_LOG_ERROR(ContextLog, "FillLightAndReflectionProbeIndices: the buffer holds {} bytes, the indices need {}.", buffer->sizeInBytes(), bytes);
            return false;
        }
        return bytes == 0 || buffer->SetData(indices.Data(), bytes, 0);
    }

    void Renderer::GetClosestReflectionProbes(Array<ReflectionProbeBlendInfo>& result) const
    {
        result.Clear();
        ReflectionProbeBlendInfo found[2];
        u32 n = ReflectionProbe::ClosestProbes(bounds(), reflectionProbeUsage_, ReflectionProbe::EnabledProbes(), found);
        for (u32 k = 0; k < n; k++)
            result.Push(found[k]);
    }

    Span<const VisibleLight> CullingResults::visibleLights() const
    {
        if (!IsValid())
            return {};
        return Span<const VisibleLight>(data->lights.Data(), data->lights.Size());
    }

    bool CullingResults::IsValid() const { return ScriptableRenderContext::IsLive(data); }

    Span<const u32> CullingResults::ShadowCasters(i32 visibleLightIndex) const
    {
        if (!IsValid() || visibleLightIndex < 0 || usize(visibleLightIndex) + 1 >= data->casterStart.Size())
            return {};
        u32 begin = data->casterStart[u32(visibleLightIndex)], end = data->casterStart[u32(visibleLightIndex) + 1];
        return Span<const u32>(data->casters.Data() + begin, end - begin);
    }

    bool CullingResults::GetShadowCasterBounds(i32 lightIndex, Bounds& outBounds) const
    {
        Span<const u32> casters = ShadowCasters(lightIndex);
        if (casters.Empty())
            return false;
        const RenderData& d = RendererManager::Get().Render();
        Vector3 lo(3.4e38f, 3.4e38f, 3.4e38f), hi(-3.4e38f, -3.4e38f, -3.4e38f);
        for (u32 i : casters)
        {
            Vector3 c(d.centerX[i], d.centerY[i], d.centerZ[i]), e(d.extentX[i], d.extentY[i], d.extentZ[i]);
            lo = Vector3::Min(lo, c - e);
            hi = Vector3::Max(hi, c + e);
        }
        outBounds = Bounds((lo + hi) * 0.5f, hi - lo);
        return true;
    }

    bool CullingResults::ComputeDirectionalShadowMatricesAndCullingPrimitives(i32 activeLightIndex, i32 splitIndex, i32 splitCount, Vector3 splitRatio,
                                                                              i32 shadowResolution, f32 shadowNearPlaneOffset, Matrix4x4& viewMatrix,
                                                                              Matrix4x4& projMatrix, ShadowSplitData& shadowSplitData) const
    {
        if (!IsValid() || activeLightIndex < 0 || usize(activeLightIndex) >= data->lights.Size() || splitCount < 1 || splitCount > 4 || splitIndex < 0 ||
            splitIndex >= splitCount || shadowResolution <= 0)
            return false;
        const VisibleLight& light = data->lights[usize(activeLightIndex)];
        Camera* camera = EntityIdToObject<Camera>(data->camera);
        if (light.lightType != LightType::Directional || !camera)
            return false;
        // The split of the camera's view between the near plane and the shadow distance.
        f32 n = camera->nearClipPlane();
        f32 far = data->shadowDistance > 0 ? Min(data->shadowDistance, camera->farClipPlane()) : camera->farClipPlane();
        const f32 ratios[3] = {splitRatio.x, splitRatio.y, splitRatio.z};
        f32 from = splitIndex == 0 ? n : Max(n, ratios[splitIndex - 1] * far);
        f32 to = splitIndex == splitCount - 1 ? far : Max(from, ratios[splitIndex] * far);
        Matrix4x4 cameraToWorld = camera->cameraToWorldMatrix();
        Vector3 corners[8];
        for (u32 k = 0; k < 2; k++)
        {
            f32 z = k == 0 ? from : to;
            f32 h = camera->orthographic() ? camera->orthographicSize() : z * std::tan(camera->fieldOfView() * 0.5f * 0.017453293f);
            f32 w = h * camera->aspect();
            for (u32 c = 0; c < 4; c++)
                corners[k * 4 + c] = cameraToWorld.MultiplyPoint3x4(Vector3((c & 1) ? w : -w, (c & 2) ? h : -h, -z));
        }
        // Its bounding sphere: the center on the view axis between the ends (the same for every rotation of the camera),
        // or, conservative, the smallest found by stepping 1 / (i + 2) toward the farthest corner (Badoiu and Clarkson).
        Vector3 nearCenter = cameraToWorld.MultiplyPoint3x4(Vector3(0, 0, -from)), farCenter = cameraToWorld.MultiplyPoint3x4(Vector3(0, 0, -to));
        Vector3 center = (nearCenter + farCenter) * 0.5f;
        auto reach = [&](const Vector3& c) {
            f32 r = 0;
            for (const Vector3& p : corners)
                r = Max(r, Vector3::Distance(c, p));
            return r;
        };
        f32 radius = reach(center);
        if (data->conservativeEnclosingSphere)
        {
            Vector3 at = center;
            for (i32 i = 0; i < data->numIterationsEnclosingSphere; i++)
            {
                Vector3 farthest = corners[0];
                for (const Vector3& p : corners)
                    if (Vector3::Distance(at, p) > Vector3::Distance(at, farthest))
                        farthest = p;
                at = at + (farthest - at) * (1.0f / f32(i + 2));
                f32 r = reach(at);
                if (r < radius)
                    center = at, radius = r;
            }
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;
        // The light's frame: its z axis is the light's direction.
        Vector3 forward = Vector3::Normalize(Vector3(light.localToWorldMatrix.GetColumn(2)));
        Vector3 up = std::abs(forward.y) > 0.99f ? Vector3(0, 0, 1) : Vector3(0, 1, 0);
        Vector3 right = Vector3::Normalize(Vector3::Cross(up, forward));
        up = Vector3::Cross(forward, right);
        // Snapped to whole texels of the map in the light's plane, so the shadow does not crawl when the camera moves.
        f32 texel = 2.0f * radius / f32(shadowResolution);
        f32 cx = std::floor(Vector3::Dot(center, right) / texel) * texel, cy = std::floor(Vector3::Dot(center, up) / texel) * texel;
        f32 cz = Vector3::Dot(center, forward);
        Vector3 snapped = right * cx + up * cy + forward * cz;
        f32 back = radius + shadowNearPlaneOffset;
        Vector3 eye = snapped - forward * back;
        viewMatrix = render::LightView(eye, right, up, forward);
        projMatrix = Matrix4x4::Ortho(-radius, radius, -radius, radius, 0.0f, back + radius);
        shadowSplitData = ShadowSplitData();
        shadowSplitData.cullingSphere = Vector4(snapped.x, snapped.y, snapped.z, radius);
        shadowSplitData.cullingMatrix = projMatrix * viewMatrix;
        shadowSplitData.cullingNearPlane = shadowNearPlaneOffset;
        // The sides and the far end of the light's box (the near side open: casters toward the light still cast).
        shadowSplitData.SetCullingPlane(0, Plane(right, snapped - right * radius));
        shadowSplitData.SetCullingPlane(1, Plane(-right, snapped + right * radius));
        shadowSplitData.SetCullingPlane(2, Plane(up, snapped - up * radius));
        shadowSplitData.SetCullingPlane(3, Plane(-up, snapped + up * radius));
        shadowSplitData.SetCullingPlane(4, Plane(-forward, snapped + forward * radius));
        shadowSplitData.cullingPlaneCount = 5;
        return true;
    }

    namespace
    {
        // The light's frame around an axis: right and up across it.
        void LightAxes(const Vector3& forward, const Vector3& upHint, Vector3& right, Vector3& up)
        {
            right = Vector3::Normalize(Vector3::Cross(upHint, forward));
            up = Vector3::Cross(forward, right);
        }

        // A split's culling from its world to projection matrix: the sides and the far end (the near side open: casters
        // between the light and the near plane still cast).
        void SidePlanes(const Matrix4x4& cullingMatrix, ShadowSplitData& split)
        {
            Plane planes[6];
            CalculateFrustumPlanes(cullingMatrix, planes);
            const u32 used[5] = {0, 1, 2, 3, 5};
            for (u32 i = 0; i < 5; i++)
                split.SetCullingPlane(i32(i), planes[used[i]]);
            split.cullingPlaneCount = 5;
        }

        bool PerspectiveShadow(const VisibleLight& light, const Vector3& forward, const Vector3& upHint, f32 fov, Matrix4x4& viewMatrix,
                               Matrix4x4& projMatrix, ShadowSplitData& split)
        {
            Light* l = light.light();
            f32 nearPlane = Max(l ? l->shadowNearPlane() : 0.2f, 0.01f);
            f32 range = Max(light.range, nearPlane * 2.0f);
            Vector3 position = light.localToWorldMatrix.GetPosition();
            Vector3 right, up;
            LightAxes(forward, upHint, right, up);
            viewMatrix = render::LightView(position, right, up, forward);
            projMatrix = Matrix4x4::Perspective(fov, 1.0f, nearPlane, range);
            split = ShadowSplitData();
            split.cullingMatrix = projMatrix * viewMatrix;
            split.cullingNearPlane = nearPlane;
            SidePlanes(split.cullingMatrix, split);
            return true;
        }
    }

    Matrix4x4 render::LightView(const Vector3& eye, const Vector3& right, const Vector3& up, const Vector3& forward)
    {
        // The view along -z (as the camera's): rows right, up and -forward.
        Matrix4x4 view = Matrix4x4::identity;
        const Vector3 rows[3] = {right, up, -forward};
        for (i32 r = 0; r < 3; r++)
        {
            view(r, 0) = rows[r].x;
            view(r, 1) = rows[r].y;
            view(r, 2) = rows[r].z;
            view(r, 3) = -Vector3::Dot(rows[r], eye);
        }
        return view;
    }

    bool CullingResults::ComputeSpotShadowMatricesAndCullingPrimitives(i32 activeLightIndex, Matrix4x4& viewMatrix, Matrix4x4& projMatrix,
                                                                       ShadowSplitData& shadowSplitData) const
    {
        if (!IsValid() || activeLightIndex < 0 || usize(activeLightIndex) >= data->lights.Size())
            return false;
        const VisibleLight& light = data->lights[usize(activeLightIndex)];
        if (light.lightType != LightType::Spot)
            return false;
        Vector3 forward = Vector3::Normalize(Vector3(light.localToWorldMatrix.GetColumn(2)));
        Vector3 upHint = std::abs(forward.y) > 0.99f ? Vector3(0, 0, 1) : Vector3(0, 1, 0);
        f32 angle = mathf::Clamp(light.spotAngle, 1.0f, 179.0f);
        if (!PerspectiveShadow(light, forward, upHint, angle, viewMatrix, projMatrix, shadowSplitData))
            return false;
        // The cone's sphere: around the middle of its axis, through the rim at the far end.
        f32 range = light.range;
        f32 rim = range * std::tan(angle * 0.5f * mathf::Deg2Rad);
        Vector3 center = light.localToWorldMatrix.GetPosition() + forward * (range * 0.5f);
        shadowSplitData.cullingSphere = Vector4(center.x, center.y, center.z, std::sqrt(range * range * 0.25f + rim * rim));
        return true;
    }

    bool CullingResults::ComputePointShadowMatricesAndCullingPrimitives(i32 activeLightIndex, CubemapFace cubemapFace, f32 fovBias, Matrix4x4& viewMatrix,
                                                                        Matrix4x4& projMatrix, ShadowSplitData& shadowSplitData) const
    {
        if (!IsValid() || activeLightIndex < 0 || usize(activeLightIndex) >= data->lights.Size() || i32(cubemapFace) < 0 || i32(cubemapFace) > 5)
            return false;
        const VisibleLight& light = data->lights[usize(activeLightIndex)];
        if (light.lightType != LightType::Point)
            return false;
        // The faces' axes and the cube map's up vectors (+X, -X, +Y, -Y, +Z, -Z).
        static const Vector3 kAxes[6] = {Vector3(1, 0, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0), Vector3(0, -1, 0), Vector3(0, 0, 1), Vector3(0, 0, -1)};
        static const Vector3 kUps[6] = {Vector3(0, -1, 0), Vector3(0, -1, 0), Vector3(0, 0, 1), Vector3(0, 0, -1), Vector3(0, -1, 0), Vector3(0, -1, 0)};
        u32 f = u32(cubemapFace);
        if (!PerspectiveShadow(light, kAxes[f], kUps[f], mathf::Clamp(90.0f + fovBias, 1.0f, 179.0f), viewMatrix, projMatrix, shadowSplitData))
            return false;
        Vector3 position = light.localToWorldMatrix.GetPosition();
        shadowSplitData.cullingSphere = Vector4(position.x, position.y, position.z, light.range);
        return true;
    }

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
        out.cullingOptions = CullingOptions(i32(CullingOptions::NeedsLighting) | i32(CullingOptions::NeedsReflectionProbes) |
                                            i32(CullingOptions::ShadowCasters) | (useOcclusionCulling() ? i32(CullingOptions::OcclusionCull) : 0));
        out.lodParameters = {orthographic(), out.origin, fieldOfView(), orthographicSize(), i32(pr.height)};
        out.shadowDistance = farClipPlane();
        Span<const f32> distances = layerCullDistances();
        for (u32 i = 0; i < 32; i++)
            out.layerCullDistances[i] = distances[i];
        out.layerCullSpherical = layerCullSpherical();
        return true;
    }

    namespace render
    {
        void RadixSort(Array<SortEntry>& entries, Array<SortEntry>& scratch) { RadixSortEntries(entries, scratch); }
    }
}
