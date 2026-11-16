#pragma once

#include <Helix/Engine/Rendering/Graphics.h>
#include <Helix/Engine/Rendering/GraphicsBuffer.h>
#include <Helix/Engine/Rendering/Pipeline/RenderPipeline.h>

// What the C# scriptable pipeline API calls (internal in C#): settings, contexts and command buffers by id, culling
// results by their address, the values of C#'s structs in one plain struct.
namespace helix::engine
{
    namespace detail
    {
        Span<Camera* const> PipelineEventCameras();
        Camera* PipelineEventCamera();
        ScriptableObject* PipelineEventAsset(bool to);
        const StandardRequest& PipelineEventRequest();
    }

    // DrawingSettings, FilteringSettings and RenderStateBlock of C# as plain values (the first target's blend state).
    HX_STRUCT(Namespace = "HelixEngine.Internal", Internal)
    struct DrawRendererData
    {
        HX_FIELD() Matrix4x4 worldToCameraMatrix;
        HX_FIELD() Vector3 cameraPosition;
        HX_FIELD() Vector3 customAxis;
        HX_FIELD() i32 criteria = 0;
        HX_FIELD() i32 distanceMetric = 0;
        HX_FIELD() i32 perObjectData = 0;
        // Bit 0: dynamic batching, bit 1: instancing.
        HX_FIELD() i32 flags = 0;
        HX_FIELD() i32 overrideMaterialPassIndex = 0;
        HX_FIELD() i32 overrideShaderPassIndex = 0;
        HX_FIELD() i32 mainLightIndex = -1;
        HX_FIELD() i32 queueLower = 0;
        HX_FIELD() i32 queueUpper = 5000;
        HX_FIELD() i32 layerMask = -1;
        HX_FIELD() u32 renderingLayerMask = ~0u;
        HX_FIELD() u32 batchLayerMask = ~0u;
        HX_FIELD() i32 excludeMotionVectorObjects = 0;
        HX_FIELD() i32 stateMask = 0;
        // Bit 0: separate MRT blend states, bit 1: alpha to mask.
        HX_FIELD() i32 blendFlags = 0;
        HX_FIELD() i32 writeMask = 15;
        HX_FIELD() i32 sourceColor = 1;
        HX_FIELD() i32 destinationColor = 0;
        HX_FIELD() i32 sourceAlpha = 1;
        HX_FIELD() i32 destinationAlpha = 0;
        HX_FIELD() i32 colorOperation = 0;
        HX_FIELD() i32 alphaOperation = 0;
        HX_FIELD() i32 depthWrite = 1;
        HX_FIELD() i32 depthCompare = 2;
        HX_FIELD() i32 cullingMode = 2;
        HX_FIELD() i32 offsetUnits = 0;
        HX_FIELD() f32 offsetFactor = 0;
        // Bit 0: depth clip, bit 1: conservative.
        HX_FIELD() i32 rasterFlags = 1;
        HX_FIELD() i32 stencilEnabled = 0;
        HX_FIELD() i32 stencilReadMask = 255;
        HX_FIELD() i32 stencilWriteMask = 255;
        HX_FIELD() i32 stencilCompareFront = 8;
        HX_FIELD() i32 stencilPassFront = 0;
        HX_FIELD() i32 stencilFailFront = 0;
        HX_FIELD() i32 stencilZFailFront = 0;
        HX_FIELD() i32 stencilCompareBack = 8;
        HX_FIELD() i32 stencilPassBack = 0;
        HX_FIELD() i32 stencilFailBack = 0;
        HX_FIELD() i32 stencilZFailBack = 0;
        HX_FIELD() i32 stencilReference = 0;
    };

    // A render pass attachment as plain values for C# (internal).
    HX_STRUCT(Namespace = "HelixEngine.Internal", Internal)
    struct AttachmentData
    {
        HX_FIELD() i32 graphicsFormat = 0;
        HX_FIELD() i32 loadAction = 2;
        HX_FIELD() i32 storeAction = 3;
        HX_FIELD() RenderTargetData loadStoreTarget;
        HX_FIELD() RenderTargetData resolveTarget;
        HX_FIELD() Color clearColor;
        HX_FIELD() f32 clearDepth = 1;
        HX_FIELD() u32 clearStencil = 0;
    };

    HX_CLASS(Namespace = "HelixEngine.Internal", Static, Internal)
    class RenderPipelineNative
    {
    public:
        // ---------------------------------------------------------------------------------------------- settings
        HX_FUNCTION() static ScriptableObject* DefaultRenderPipeline() { return GraphicsSettings::defaultRenderPipeline(); }
        HX_FUNCTION() static void SetDefaultRenderPipeline(ScriptableObject* asset) { GraphicsSettings::SetDefaultRenderPipeline(asset); }
        HX_FUNCTION() static ScriptableObject* CurrentRenderPipeline() { return GraphicsSettings::currentRenderPipeline(); }
        HX_FUNCTION() static u32 DefaultRenderingLayerMask() { return GraphicsSettings::defaultRenderingLayerMask(); }
        HX_FUNCTION() static void SetDefaultRenderingLayerMask(u32 mask) { GraphicsSettings::SetDefaultRenderingLayerMask(mask); }
        HX_FUNCTION() static ScriptableObject* QualityRenderPipeline() { return QualitySettings::renderPipeline(); }
        HX_FUNCTION() static void SetQualityRenderPipeline(ScriptableObject* asset) { QualitySettings::SetRenderPipeline(asset); }
        HX_FUNCTION() static ScriptableObject* GetRenderPipelineAssetAt(i32 index) { return QualitySettings::GetRenderPipelineAssetAt(index); }
        HX_FUNCTION() static void ResetPipeline(ScriptableObject* asset) { ResetRenderPipeline(asset); }
        HX_FUNCTION() static ScriptableObject* CurrentAsset() { return RenderPipelineManager::currentAsset(); }
        HX_FUNCTION() static bool HasPipeline() { return RenderPipelineManager::currentPipeline() != nullptr; }

        // ---------------------------------------------------------------------------------------------- events
        HX_FUNCTION() static Array<Camera*> EventCameras()
        {
            Array<Camera*> out;
            for (Camera* c : detail::PipelineEventCameras())
                out.Push(c);
            return out;
        }
        HX_FUNCTION() static Camera* EventCamera() { return detail::PipelineEventCamera(); }
        HX_FUNCTION() static ScriptableObject* EventAsset(bool to) { return detail::PipelineEventAsset(to); }
        // The standard request of the request the C# side hears of.
        HX_FUNCTION() static RenderTexture* EventRequestDestination() { return detail::PipelineEventRequest().destination; }
        HX_FUNCTION() static i32 EventRequestMip() { return detail::PipelineEventRequest().mipLevel; }
        HX_FUNCTION() static i32 EventRequestFace() { return i32(detail::PipelineEventRequest().face); }
        HX_FUNCTION() static i32 EventRequestSlice() { return detail::PipelineEventRequest().slice; }
        // A request through the active pipeline: a C# request object (its GC handle; 0 for a standard request) or the
        // standard request's values; `process` false only asks whether the pipeline takes it.
        HX_FUNCTION() static bool RenderRequest(Camera* camera, u64 managed, RenderTexture* destination, i32 mipLevel, i32 face, i32 slice, bool process)
        {
            engine::RenderRequest r;
            r.standard = managed == 0;
            r.managed = managed;
            r.request = {destination, mipLevel, CubemapFace(face), slice};
            return render::RenderRequestThroughPipeline(camera, r, process);
        }
        HX_FUNCTION() static void BeginContextRendering(u64 context, const Array<Camera*>& cameras)
        {
            if (ScriptableRenderContext* c = ScriptableRenderContext::Find(context))
                RenderPipeline::BeginContextRendering(*c, Span<Camera* const>(cameras.Data(), cameras.Size()));
        }
        HX_FUNCTION() static void EndContextRendering(u64 context, const Array<Camera*>& cameras)
        {
            if (ScriptableRenderContext* c = ScriptableRenderContext::Find(context))
                RenderPipeline::EndContextRendering(*c, Span<Camera* const>(cameras.Data(), cameras.Size()));
        }
        HX_FUNCTION() static void BeginCameraRendering(u64 context, Camera* camera)
        {
            if (ScriptableRenderContext* c = ScriptableRenderContext::Find(context))
                RenderPipeline::BeginCameraRendering(*c, camera);
        }
        HX_FUNCTION() static void EndCameraRendering(u64 context, Camera* camera)
        {
            if (ScriptableRenderContext* c = ScriptableRenderContext::Find(context))
                RenderPipeline::EndCameraRendering(*c, camera);
        }

        // ---------------------------------------------------------------------------------------------- context
        // False for a context that no longer renders (its calls do nothing).
        HX_FUNCTION() static bool IsContextLive(u64 context) { return ScriptableRenderContext::Find(context) != nullptr; }
        // The size of the engine's ScriptableCullingParameters (C# reads and writes it at native addresses).
        HX_FUNCTION() static i32 CullingParametersSize() { return i32(sizeof(ScriptableCullingParameters)); }
        HX_FUNCTION() static Array<Camera*> ContextCameras(u64 context)
        {
            Array<Camera*> out;
            if (ScriptableRenderContext* c = ScriptableRenderContext::Find(context))
                for (Camera* camera : c->cameras())
                    out.Push(camera);
            return out;
        }
        // The culling results' address (0 when invalid).
        // `lod`: field of view, orthographic size, pixel height, orthographic (0 or 1); `lodPosition`: the camera's;
        // `shadows`: the shadow distance, near plane offset, conservative enclosing sphere (0 or 1) and its iterations.
        HX_FUNCTION() static u64 Cull(u64 context, Camera* camera, i32 cullingMask, Matrix4x4 cullingMatrix, Span<const Plane> planes, i32 options,
                                      Vector3 origin, bool orthographic, Vector4 lod, Vector3 lodPosition, Span<const f32> layerCullDistances,
                                      bool layerCullSpherical, i32 maximumVisibleLights, Vector4 shadows, i32 probeSorting);
        // Shadows (the split data, culling infos and splits as their memory: C# lays them out as C++ does).
        HX_FUNCTION() static bool ShadowCasterBounds(u64 culling, i32 lightIndex, Bounds& bounds)
        {
            return CullingResults{reinterpret_cast<render::CullingData*>(uintptr_t(culling))}.GetShadowCasterBounds(lightIndex, bounds);
        }
        // The light index map (as many as the visible lights) into `out`, and from `map`; the per object light indices.
        HX_FUNCTION() static void GetLightIndexMap(u64 culling, u64 out, i32 count)
        {
            Array<i32> map = CullingResults{reinterpret_cast<render::CullingData*>(uintptr_t(culling))}.GetLightIndexMap();
            for (u32 i = 0; i < map.Size() && i32(i) < count; i++)
                reinterpret_cast<i32*>(uintptr_t(out))[i] = map[i];
        }
        HX_FUNCTION() static void SetLightIndexMap(u64 culling, u64 map, i32 count)
        {
            CullingResults{reinterpret_cast<render::CullingData*>(uintptr_t(culling))}.SetLightIndexMap(
                Span<const i32>(reinterpret_cast<const i32*>(uintptr_t(map)), usize(count > 0 ? count : 0)));
        }
        HX_FUNCTION() static i32 LightIndexCount(u64 culling)
        {
            return CullingResults{reinterpret_cast<render::CullingData*>(uintptr_t(culling))}.lightIndexCount();
        }
        HX_FUNCTION() static bool DirectionalShadowMatrices(u64 culling, i32 lightIndex, i32 splitIndex, i32 splitCount, Vector3 splitRatio, i32 resolution,
                                                            f32 nearPlaneOffset, Matrix4x4& view, Matrix4x4& projection, u64 splitData)
        {
            return CullingResults{reinterpret_cast<render::CullingData*>(uintptr_t(culling))}.ComputeDirectionalShadowMatricesAndCullingPrimitives(
                lightIndex, splitIndex, splitCount, splitRatio, resolution, nearPlaneOffset, view, projection,
                *reinterpret_cast<ShadowSplitData*>(uintptr_t(splitData)));
        }
        HX_FUNCTION() static bool SpotShadowMatrices(u64 culling, i32 lightIndex, Matrix4x4& view, Matrix4x4& projection, u64 splitData)
        {
            return CullingResults{reinterpret_cast<render::CullingData*>(uintptr_t(culling))}.ComputeSpotShadowMatricesAndCullingPrimitives(
                lightIndex, view, projection, *reinterpret_cast<ShadowSplitData*>(uintptr_t(splitData)));
        }
        HX_FUNCTION() static bool PointShadowMatrices(u64 culling, i32 lightIndex, i32 face, f32 fovBias, Matrix4x4& view, Matrix4x4& projection, u64 splitData)
        {
            return CullingResults{reinterpret_cast<render::CullingData*>(uintptr_t(culling))}.ComputePointShadowMatricesAndCullingPrimitives(
                lightIndex, CubemapFace(face), fovBias, view, projection, *reinterpret_cast<ShadowSplitData*>(uintptr_t(splitData)));
        }
        HX_FUNCTION() static void CullShadowCasters(u64 context, u64 culling, u64 lightInfos, i32 lightCount, u64 splits, i32 splitCount)
        {
            if (ScriptableRenderContext* c = ScriptableRenderContext::Find(context))
                c->CullShadowCasters(CullingResults{reinterpret_cast<render::CullingData*>(uintptr_t(culling))},
                                     ShadowCastersCullingInfos{
                                         Span<const LightShadowCasterCullingInfo>(reinterpret_cast<const LightShadowCasterCullingInfo*>(uintptr_t(lightInfos)),
                                                                                  usize(lightCount > 0 ? lightCount : 0)),
                                         Span<const ShadowSplitData>(reinterpret_cast<const ShadowSplitData*>(uintptr_t(splits)), usize(splitCount > 0 ? splitCount : 0))});
        }
        HX_FUNCTION() static u64 CreateShadowRendererList(u64 context, u64 culling, i32 lightIndex, i32 splitIndex, i32 objectsFilter, u32 batchLayerMask,
                                                          i32 projectionType)
        {
            ScriptableRenderContext* c = ScriptableRenderContext::Find(context);
            if (!c)
                return 0;
            ShadowDrawingSettings s(CullingResults{reinterpret_cast<render::CullingData*>(uintptr_t(culling))}, lightIndex,
                                    BatchCullingProjectionType(projectionType));
            s.splitIndex = splitIndex;
            s.objectsFilter = ShadowObjectsFilter(objectsFilter);
            s.batchLayerMask = batchLayerMask;
            return c->CreateShadowRendererList(s).id;
        }
        // `blends`: seven values per target after the first (write mask, the four factors, the two operations).
        HX_FUNCTION() static void DrawRenderers(u64 context, u64 culling, const DrawRendererData& data, Span<const ShaderTagId> passNames,
                                                Material* overrideMaterial, Shader* overrideShader, Material* fallbackMaterial, Span<const i32> blends);
        // Renderer lists: the list's id (0 when it could not be made); the states of tag values added after.
        HX_FUNCTION() static u64 CreateRendererList(u64 context, u64 culling, const DrawRendererData& data, Span<const ShaderTagId> passNames,
                                                    Material* overrideMaterial, Shader* overrideShader, Material* fallbackMaterial, Span<const i32> blends,
                                                    ShaderTagId tagName, bool isPassTagName);
        HX_FUNCTION() static u64 CreateSkyboxRendererList(u64 context, Camera* camera)
        {
            ScriptableRenderContext* c = ScriptableRenderContext::Find(context);
            return c ? c->CreateSkyboxRendererList(camera).id : 0;
        }
        HX_FUNCTION() static void DrawSkybox(u64 context, Camera* camera)
        {
            if (ScriptableRenderContext* c = ScriptableRenderContext::Find(context))
                c->DrawSkybox(camera);
        }
        HX_FUNCTION() static void AddRendererListState(u64 context, u64 list, ShaderTagId tagValue, const DrawRendererData& data, Span<const i32> blends);
        HX_FUNCTION() static bool RendererListValid(u64 list) { return RendererUtils::RendererList{list}.isValid(); }
        HX_FUNCTION() static void DrawRendererList(u64 buffer, u64 list) { With(buffer, [&](CommandBuffer& b) { b.DrawRendererList(RendererUtils::RendererList{list}); }); }
        // Native render passes.
        HX_FUNCTION() static void BeginRenderPass(u64 context, i32 width, i32 height, i32 samples, Span<const AttachmentData> attachments, i32 depthIndex);
        HX_FUNCTION() static void BeginSubPass(u64 context, Span<const i32> colors, Span<const i32> inputs, bool depthReadOnly)
        {
            if (ScriptableRenderContext* c = ScriptableRenderContext::Find(context))
                c->BeginSubPass(colors, inputs, depthReadOnly);
        }
        HX_FUNCTION() static void EndSubPass(u64 context)
        {
            if (ScriptableRenderContext* c = ScriptableRenderContext::Find(context))
                c->EndSubPass();
        }
        HX_FUNCTION() static void EndRenderPass(u64 context)
        {
            if (ScriptableRenderContext* c = ScriptableRenderContext::Find(context))
                c->EndRenderPass();
        }
        HX_FUNCTION() static void DrawProcedural(u64 buffer, Matrix4x4 matrix, Material* material, i32 shaderPass, i32 topology, i32 vertexCount,
                                                 i32 instanceCount, PropertyBlockId properties)
        {
            With(buffer, [&](CommandBuffer& b) {
                b.DrawProcedural(matrix, material, shaderPass, MeshTopology(topology), vertexCount, instanceCount, MaterialPropertyBlock::Find(properties));
            });
        }
        HX_FUNCTION() static void SetupCameraProperties(u64 context, Camera* camera, bool stereoSetup)
        {
            if (ScriptableRenderContext* c = ScriptableRenderContext::Find(context))
                c->SetupCameraProperties(camera, stereoSetup);
        }
        HX_FUNCTION() static void ExecuteCommandBuffer(u64 context, u64 buffer)
        {
            ScriptableRenderContext* c = ScriptableRenderContext::Find(context);
            CommandBuffer* b = CommandBuffer::Find(buffer);
            if (c && b)
                c->ExecuteCommandBuffer(*b);
        }
        HX_FUNCTION() static void InvokeOnRenderObjectCallback(u64 context)
        {
            if (ScriptableRenderContext* c = ScriptableRenderContext::Find(context))
                c->InvokeOnRenderObjectCallback();
        }
        HX_FUNCTION() static void Submit(u64 context)
        {
            if (ScriptableRenderContext* c = ScriptableRenderContext::Find(context))
                c->Submit();
        }
        // The visible lights' memory (what a NativeArray<VisibleLight> of C# reads) and their count.
        HX_FUNCTION(Out = "count") static u64 VisibleLights(u64 culling, i32& count);
        HX_FUNCTION() static bool IsCullingLive(u64 culling) { return ScriptableRenderContext::IsLive(reinterpret_cast<const render::CullingData*>(uintptr_t(culling))); }
        HX_FUNCTION() static Light* LightOf(u64 id) { return EntityIdToObject<Light>(EntityId::FromULong(id)); }
        HX_FUNCTION() static i32 VisibleLightSize() { return i32(sizeof(VisibleLight)); }
        // The visible reflection probes' memory and their count; the objects of their ids.
        HX_FUNCTION(Out = "count") static u64 VisibleReflectionProbes(u64 culling, i32& count)
        {
            Span<const VisibleReflectionProbe> probes = CullingResults{reinterpret_cast<render::CullingData*>(uintptr_t(culling))}.visibleReflectionProbes();
            count = i32(probes.Size());
            return u64(reinterpret_cast<uintptr_t>(probes.Data()));
        }
        HX_FUNCTION() static i32 VisibleReflectionProbeSize() { return i32(sizeof(VisibleReflectionProbe)); }
        HX_FUNCTION() static ReflectionProbe* ReflectionProbeOf(u64 id) { return EntityIdToObject<ReflectionProbe>(EntityId::FromULong(id)); }
        HX_FUNCTION() static Texture* TextureOf(u64 id) { return EntityIdToObject<Texture>(EntityId::FromULong(id)); }
        HX_FUNCTION() static bool FillLightAndReflectionProbeIndices(u64 culling, u64 buffer)
        {
            return CullingResults{reinterpret_cast<render::CullingData*>(uintptr_t(culling))}.FillLightAndReflectionProbeIndices(GraphicsBuffer::Find(buffer));
        }
        HX_FUNCTION() static i32 LightAndReflectionProbeIndexCount(u64 culling)
        {
            return CullingResults{reinterpret_cast<render::CullingData*>(uintptr_t(culling))}.lightAndReflectionProbeIndexCount();
        }
        // Renderer.GetClosestReflectionProbes: the probes, and their weights in the same order.
        HX_FUNCTION() static Array<ReflectionProbe*> ClosestReflectionProbes(Renderer* renderer)
        {
            Array<ReflectionProbe*> out;
            Array<ReflectionProbeBlendInfo> found;
            if (renderer)
                renderer->GetClosestReflectionProbes(found);
            for (const ReflectionProbeBlendInfo& f : found)
                out.Push(f.probe);
            return out;
        }
        HX_FUNCTION() static Array<f32> ClosestReflectionProbeWeights(Renderer* renderer)
        {
            Array<f32> out;
            Array<ReflectionProbeBlendInfo> found;
            if (renderer)
                renderer->GetClosestReflectionProbes(found);
            for (const ReflectionProbeBlendInfo& f : found)
                out.Push(f.weight);
            return out;
        }

        // ---------------------------------------------------------------------------------------------- command buffers
        // A buffer owned by the caller (Destroy frees it).
        HX_FUNCTION() static u64 CreateBuffer() { return (new CommandBuffer())->Id(); }
        HX_FUNCTION() static void DestroyBuffer(u64 buffer) { delete CommandBuffer::Find(buffer); }
        HX_FUNCTION() static String BufferName(u64 buffer)
        {
            CommandBuffer* b = CommandBuffer::Find(buffer);
            return b ? b->name() : String();
        }
        HX_FUNCTION() static void SetBufferName(u64 buffer, StringView name) { With(buffer, [&](CommandBuffer& b) { b.SetName(name); }); }
        HX_FUNCTION() static i32 BufferSize(u64 buffer)
        {
            CommandBuffer* b = CommandBuffer::Find(buffer);
            return b ? b->sizeInBytes() : 0;
        }
        HX_FUNCTION() static void ClearBuffer(u64 buffer) { With(buffer, [](CommandBuffer& b) { b.Clear(); }); }
        HX_FUNCTION() static void ClearRenderTarget(u64 buffer, bool clearDepth, bool clearColor, Color backgroundColor, f32 depth)
        {
            With(buffer, [&](CommandBuffer& b) { b.ClearRenderTarget(clearDepth, clearColor, backgroundColor, depth); });
        }
        HX_FUNCTION() static void SetViewport(u64 buffer, Rect pixelRect) { With(buffer, [&](CommandBuffer& b) { b.SetViewport(pixelRect); }); }
        HX_FUNCTION() static void EnableScissorRect(u64 buffer, Rect scissor) { With(buffer, [&](CommandBuffer& b) { b.EnableScissorRect(scissor); }); }
        HX_FUNCTION() static void DisableScissorRect(u64 buffer) { With(buffer, [](CommandBuffer& b) { b.DisableScissorRect(); }); }
        HX_FUNCTION() static void SetViewProjectionMatrices(u64 buffer, Matrix4x4 view, Matrix4x4 proj)
        {
            With(buffer, [&](CommandBuffer& b) { b.SetViewProjectionMatrices(view, proj); });
        }
        HX_FUNCTION() static void SetGlobalFloat(u64 buffer, i32 nameID, f32 value) { With(buffer, [&](CommandBuffer& b) { b.SetGlobalFloat(nameID, value); }); }
        HX_FUNCTION() static void SetGlobalInteger(u64 buffer, i32 nameID, i32 value) { With(buffer, [&](CommandBuffer& b) { b.SetGlobalInteger(nameID, value); }); }
        HX_FUNCTION() static void SetGlobalVector(u64 buffer, i32 nameID, Vector4 value) { With(buffer, [&](CommandBuffer& b) { b.SetGlobalVector(nameID, value); }); }
        HX_FUNCTION() static void SetGlobalMatrix(u64 buffer, i32 nameID, Matrix4x4 value) { With(buffer, [&](CommandBuffer& b) { b.SetGlobalMatrix(nameID, value); }); }
        HX_FUNCTION() static void SetGlobalTexture(u64 buffer, i32 nameID, Texture* value) { With(buffer, [&](CommandBuffer& b) { b.SetGlobalTexture(nameID, value); }); }
        HX_FUNCTION() static void SetGlobalFloatArray(u64 buffer, i32 nameID, Span<const f32> values) { With(buffer, [&](CommandBuffer& b) { b.SetGlobalFloatArray(nameID, values); }); }
        HX_FUNCTION() static void SetGlobalVectorArray(u64 buffer, i32 nameID, Span<const Vector4> values) { With(buffer, [&](CommandBuffer& b) { b.SetGlobalVectorArray(nameID, values); }); }
        HX_FUNCTION() static void SetGlobalMatrixArray(u64 buffer, i32 nameID, Span<const Matrix4x4> values) { With(buffer, [&](CommandBuffer& b) { b.SetGlobalMatrixArray(nameID, values); }); }
        HX_FUNCTION() static void SetInvertCulling(u64 buffer, bool value) { With(buffer, [&](CommandBuffer& b) { b.SetInvertCulling(value); }); }
        HX_FUNCTION() static void SetGlobalBuffer(u64 buffer, i32 nameID, u64 graphicsBuffer)
        {
            With(buffer, [&](CommandBuffer& b) { b.SetGlobalBuffer(nameID, graphicsBuffer); });
        }
        // The compute members of CommandBuffer (kind: 0 float, 1 int, 2 vector, 3 matrix, 4 floats, 5 ints, 6 vectors,
        // 7 matrices; `width` numbers per value).
        HX_FUNCTION() static void SetComputeValue(u64 buffer, ComputeShader* shader, i32 nameID, i32 kind, Span<const f32> values)
        {
            static constexpr u32 kWidths[] = {1, 1, 4, 16, 1, 1, 4, 16};
            u32 width = kind >= 0 && kind < 8 ? kWidths[kind] : 1;
            With(buffer, [&](CommandBuffer& b) {
                if (kind == 5)
                {
                    Array<i32> ints;
                    for (f32 f : values)
                        ints.Push(i32(f));
                    b.SetComputeIntParams(shader, nameID, Span<const i32>(ints.Data(), ints.Size()));
                    return;
                }
                switch (kind)
                {
                case 0: b.SetComputeFloatParam(shader, nameID, values.Empty() ? 0.0f : values[0]); break;
                case 1: b.SetComputeIntParam(shader, nameID, values.Empty() ? 0 : i32(values[0])); break;
                case 2: b.SetComputeVectorParam(shader, nameID, values.Size() >= 4 ? Vector4(values[0], values[1], values[2], values[3]) : Vector4()); break;
                case 3:
                    if (values.Size() >= 16)
                        b.SetComputeMatrixParam(shader, nameID, *reinterpret_cast<const Matrix4x4*>(values.Data()));
                    break;
                case 4: b.SetComputeFloatParams(shader, nameID, values); break;
                case 6: b.SetComputeVectorArrayParam(shader, nameID, Span<const Vector4>(reinterpret_cast<const Vector4*>(values.Data()), values.Size() / width)); break;
                default:
                    b.SetComputeMatrixArrayParam(shader, nameID, Span<const Matrix4x4>(reinterpret_cast<const Matrix4x4*>(values.Data()), values.Size() / width));
                    break;
                }
            });
        }
        HX_FUNCTION() static void SetComputeTextureParam(u64 buffer, ComputeShader* shader, i32 kernelIndex, i32 nameID, RenderTargetData rt, i32 mipLevel)
        {
            With(buffer, [&](CommandBuffer& b) { b.SetComputeTextureParam(shader, kernelIndex, nameID, rt.ToIdentifier(), mipLevel); });
        }
        HX_FUNCTION() static void SetComputeBufferParam(u64 buffer, ComputeShader* shader, i32 kernelIndex, i32 nameID, u64 graphicsBuffer)
        {
            With(buffer, [&](CommandBuffer& b) { b.SetComputeBufferParam(shader, kernelIndex, nameID, graphicsBuffer); });
        }
        HX_FUNCTION() static void SetComputeKeyword(u64 buffer, ComputeShader* shader, LocalKeyword keyword, bool value)
        {
            With(buffer, [&](CommandBuffer& b) { b.SetKeyword(shader, keyword, value); });
        }
        HX_FUNCTION() static void DispatchCompute(u64 buffer, ComputeShader* shader, i32 kernelIndex, i32 x, i32 y, i32 z)
        {
            With(buffer, [&](CommandBuffer& b) { b.DispatchCompute(shader, kernelIndex, x, y, z); });
        }
        HX_FUNCTION() static void DispatchComputeIndirect(u64 buffer, ComputeShader* shader, i32 kernelIndex, u64 argsBuffer, u32 argsOffset)
        {
            With(buffer, [&](CommandBuffer& b) { b.DispatchCompute(shader, kernelIndex, argsBuffer, argsOffset); });
        }
        // Bytes at a pinned address into a graphics buffer in the commands' order.
        HX_FUNCTION() static void SetBufferData(u64 buffer, u64 graphicsBuffer, u64 address, u32 bytes, u32 bufferOffset)
        {
            With(buffer, [&](CommandBuffer& b) { b.SetBufferData(graphicsBuffer, reinterpret_cast<const void*>(uptr(address)), bytes, bufferOffset); });
        }
        HX_FUNCTION() static void SetGlobalDepthBias(u64 buffer, f32 bias, f32 slopeBias)
        {
            With(buffer, [&](CommandBuffer& b) { b.SetGlobalDepthBias(bias, slopeBias); });
        }
        HX_FUNCTION() static void SetKeyword(u64 buffer, StringView keyword, bool value)
        {
            With(buffer, [&](CommandBuffer& b) { b.SetKeyword(GlobalKeyword(keyword), value); });
        }
        HX_FUNCTION() static void DrawMeshInstanced(u64 buffer, Mesh* mesh, i32 submeshIndex, Material* material, i32 shaderPass,
                                                    Span<const Matrix4x4> matrices, PropertyBlockId properties)
        {
            With(buffer, [&](CommandBuffer& b) { b.DrawMeshInstanced(mesh, submeshIndex, material, shaderPass, matrices, MaterialPropertyBlock::Find(properties)); });
        }
        HX_FUNCTION() static void DrawMesh(u64 buffer, Mesh* mesh, Matrix4x4 matrix, Material* material, i32 submeshIndex, i32 shaderPass, PropertyBlockId properties)
        {
            With(buffer, [&](CommandBuffer& b) { b.DrawMesh(mesh, matrix, material, submeshIndex, shaderPass, MaterialPropertyBlock::Find(properties)); });
        }
        HX_FUNCTION() static void DrawRenderer(u64 buffer, Renderer* renderer, Material* material, i32 submeshIndex, i32 shaderPass)
        {
            With(buffer, [&](CommandBuffer& b) { b.DrawRenderer(renderer, material, submeshIndex, shaderPass); });
        }
        HX_FUNCTION() static void BeginSample(u64 buffer, StringView name) { With(buffer, [&](CommandBuffer& b) { b.BeginSample(name); }); }
        HX_FUNCTION() static void EndSample(u64 buffer, StringView name) { With(buffer, [&](CommandBuffer& b) { b.EndSample(name); }); }
        HX_FUNCTION() static void ExecuteNow(u64 buffer) { With(buffer, [](CommandBuffer& b) { Graphics::ExecuteCommandBuffer(b); }); }

        // `depth` is used when `separateDepth`; the colors with their mips, faces and slices.
        HX_FUNCTION() static void SetRenderTarget(u64 buffer, Span<const RenderTargetData> colors, RenderTargetData depth, bool separateDepth, i32 loadAction,
                                                  i32 storeAction)
        {
            With(buffer, [&](CommandBuffer& b) {
                RenderTargetIdentifier ids[rhi::kMaxColorTargets];
                u32 count = u32(colors.Size() < rhi::kMaxColorTargets ? colors.Size() : rhi::kMaxColorTargets);
                for (u32 i = 0; i < count; i++)
                    ids[i] = colors[i].ToIdentifier();
                RenderTargetIdentifier d = separateDepth ? depth.ToIdentifier() : (count ? ids[0] : RenderTargetIdentifier());
                if (separateDepth)
                    b.SetRenderTarget(Span<const RenderTargetIdentifier>(ids, count), d);
                else if (count)
                    b.SetRenderTarget(ids[0], RenderBufferLoadAction(loadAction), RenderBufferStoreAction(storeAction));
            });
        }
        HX_FUNCTION() static void GetTemporaryRT(u64 buffer, i32 nameID, RenderTextureDescriptorData descriptor, FilterMode filter)
        {
            With(buffer, [&](CommandBuffer& b) { b.GetTemporaryRT(nameID, descriptor.ToDescriptor(), filter); });
        }
        HX_FUNCTION() static void ReleaseTemporaryRT(u64 buffer, i32 nameID) { With(buffer, [&](CommandBuffer& b) { b.ReleaseTemporaryRT(nameID); }); }
        HX_FUNCTION() static void Blit(u64 buffer, RenderTargetData source, RenderTargetData dest, Material* material, i32 pass, Vector4 scaleOffset)
        {
            With(buffer, [&](CommandBuffer& b) {
                b.Blit(source.ToIdentifier(), dest.ToIdentifier(), material, pass);
                render::Command& c = b.MutableList().commands.Back();
                c.v[0] = scaleOffset.x, c.v[1] = scaleOffset.y, c.v[2] = scaleOffset.z, c.v[3] = scaleOffset.w;
            });
        }
        HX_FUNCTION() static void CopyTexture(u64 buffer, RenderTargetData source, i32 sourceElement, i32 sourceMip, RenderTargetData dest, i32 destElement,
                                              i32 destMip)
        {
            With(buffer, [&](CommandBuffer& b) { b.CopyTexture(source.ToIdentifier(), sourceElement, sourceMip, dest.ToIdentifier(), destElement, destMip); });
        }
        HX_FUNCTION() static void ResolveAntiAliasedSurface(u64 buffer, RenderTexture* rt, RenderTexture* target)
        {
            With(buffer, [&](CommandBuffer& b) { b.ResolveAntiAliasedSurface(rt, target); });
        }
        HX_FUNCTION() static void GenerateMips(u64 buffer, RenderTargetData rt) { With(buffer, [&](CommandBuffer& b) { b.GenerateMips(rt.ToIdentifier()); }); }
        HX_FUNCTION() static void SetGlobalTextureTarget(u64 buffer, i32 nameID, RenderTargetData value)
        {
            With(buffer, [&](CommandBuffer& b) { b.SetGlobalTexture(nameID, value.ToIdentifier()); });
        }

        // ---------------------------------------------------------------------------------------------- Graphics
        HX_FUNCTION() static void GraphicsBlit(Texture* source, RenderTexture* dest, Material* material, i32 pass, Vector4 scaleOffset)
        {
            if (scaleOffset.x == 1 && scaleOffset.y == 1 && scaleOffset.z == 0 && scaleOffset.w == 0)
                Graphics::Blit(source, dest, material, pass);
            else
                Graphics::Blit(source, dest, Vector2(scaleOffset.x, scaleOffset.y), Vector2(scaleOffset.z, scaleOffset.w));
        }
        HX_FUNCTION() static bool GraphicsConvertTexture(Texture* source, i32 sourceElement, Texture* dest, i32 destElement)
        {
            return Graphics::ConvertTexture(source, sourceElement, dest, destElement);
        }
        HX_FUNCTION() static void GraphicsCopyTexture(Texture* source, i32 sourceElement, i32 sourceMip, Texture* dest, i32 destElement, i32 destMip)
        {
            if (sourceElement < 0)
                Graphics::CopyTexture(source, dest);
            else
                Graphics::CopyTexture(source, sourceElement, sourceMip, dest, destElement, destMip);
        }

    private:
        template <class F>
        static void With(u64 buffer, F&& f)
        {
            if (CommandBuffer* b = CommandBuffer::Find(buffer))
                f(*b);
        }
    };
}
