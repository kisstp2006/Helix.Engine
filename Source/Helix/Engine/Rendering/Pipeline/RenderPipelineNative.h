#pragma once

#include <Helix/Engine/Rendering/Graphics.h>
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
        HX_FUNCTION() static Array<Camera*> ContextCameras(u64 context)
        {
            Array<Camera*> out;
            if (ScriptableRenderContext* c = ScriptableRenderContext::Find(context))
                for (Camera* camera : c->cameras())
                    out.Push(camera);
            return out;
        }
        // The culling results' address (0 when invalid).
        HX_FUNCTION() static u64 Cull(u64 context, Camera* camera, i32 cullingMask, Matrix4x4 cullingMatrix, Span<const Plane> planes, i32 options,
                                      Vector3 origin, bool orthographic);
        HX_FUNCTION() static void DrawRenderers(u64 context, u64 culling, const DrawRendererData& data, Span<const ShaderTagId> passNames,
                                                Material* overrideMaterial, Shader* overrideShader, Material* fallbackMaterial);
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
        HX_FUNCTION() static void SetKeyword(u64 buffer, StringView keyword, bool value)
        {
            With(buffer, [&](CommandBuffer& b) { b.SetKeyword(GlobalKeyword(keyword), value); });
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

    private:
        template <class F>
        static void With(u64 buffer, F&& f)
        {
            if (CommandBuffer* b = CommandBuffer::Find(buffer))
                f(*b);
        }
    };
}
