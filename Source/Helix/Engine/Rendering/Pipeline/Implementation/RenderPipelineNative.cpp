#include <Helix/Engine/Rendering/Pipeline/RenderPipelineNative.h>
#include <Helix/Engine/Rendering/Implementation/RenderCore.h>

namespace helix::engine
{
    u64 RenderPipelineNative::Cull(u64 context, Camera* camera, i32 cullingMask, Matrix4x4 cullingMatrix, Span<const Plane> planes, i32 options,
                                   Vector3 origin, bool orthographic)
    {
        ScriptableRenderContext* c = ScriptableRenderContext::Find(context);
        if (!c)
            return 0;
        ScriptableCullingParameters p;
        p.cullingMask = cullingMask;
        p.cullingMatrix = cullingMatrix;
        p.cullingPlaneCount = i32(planes.Size());
        for (usize i = 0; i < planes.Size() && i < usize(ScriptableCullingParameters::maximumCullingPlanes); i++)
            p.planes[i] = planes[i];
        p.cullingOptions = CullingOptions(options);
        p.origin = origin;
        p.isOrthographic = orthographic;
        p.camera = camera ? camera->GetEntityId() : EntityId();
        CullingResults r = c->Cull(p);
        return u64(reinterpret_cast<uintptr_t>(r.data));
    }

    void RenderPipelineNative::DrawRenderers(u64 context, u64 culling, const DrawRendererData& d, Span<const ShaderTagId> passNames, Material* overrideMaterial,
                                             Shader* overrideShader, Material* fallbackMaterial, Span<const i32> blends)
    {
        ScriptableRenderContext* c = ScriptableRenderContext::Find(context);
        if (!c)
            return;
        DrawingSettings drawing;
        drawing.sortingSettings.worldToCameraMatrix = d.worldToCameraMatrix;
        drawing.sortingSettings.cameraPosition = d.cameraPosition;
        drawing.sortingSettings.customAxis = d.customAxis;
        drawing.sortingSettings.criteria = SortingCriteria(d.criteria);
        drawing.sortingSettings.distanceMetric = DistanceMetric(d.distanceMetric);
        drawing.perObjectData = PerObjectData(d.perObjectData);
        drawing.enableDynamicBatching = (d.flags & 1) != 0;
        drawing.enableInstancing = (d.flags & 2) != 0;
        drawing.overrideMaterial = overrideMaterial;
        drawing.overrideMaterialPassIndex = d.overrideMaterialPassIndex;
        drawing.overrideShader = overrideShader;
        drawing.overrideShaderPassIndex = d.overrideShaderPassIndex;
        drawing.fallbackMaterial = fallbackMaterial;
        drawing.mainLightIndex = d.mainLightIndex;
        for (usize i = 0; i < passNames.Size() && i < usize(DrawingSettings::maxShaderPasses); i++)
            drawing.passNames[i] = passNames[i];
        FilteringSettings filtering;
        filtering.renderQueueRange = {d.queueLower, d.queueUpper};
        filtering.layerMask = d.layerMask;
        filtering.renderingLayerMask = d.renderingLayerMask;
        filtering.batchLayerMask = d.batchLayerMask;
        filtering.excludeMotionVectorObjects = d.excludeMotionVectorObjects != 0;
        RenderStateBlock state(RenderStateMask(d.stateMask));
        state.blendState.separateMRTBlendStates = (d.blendFlags & 1) != 0;
        state.blendState.alphaToMask = (d.blendFlags & 2) != 0;
        RenderTargetBlendState& t = state.blendState.targets[0];
        t.writeMask = ColorWriteMask(d.writeMask);
        t.sourceColorBlendMode = BlendMode(d.sourceColor);
        t.destinationColorBlendMode = BlendMode(d.destinationColor);
        t.sourceAlphaBlendMode = BlendMode(d.sourceAlpha);
        t.destinationAlphaBlendMode = BlendMode(d.destinationAlpha);
        t.colorBlendOperation = BlendOp(d.colorOperation);
        t.alphaBlendOperation = BlendOp(d.alphaOperation);
        for (usize i = 0; i + 7 <= blends.Size() && i / 7 + 1 < rhi::kMaxColorTargets; i += 7)
        {
            RenderTargetBlendState& b = state.blendState.targets[i / 7 + 1];
            b.writeMask = ColorWriteMask(blends[i]);
            b.sourceColorBlendMode = BlendMode(blends[i + 1]);
            b.destinationColorBlendMode = BlendMode(blends[i + 2]);
            b.sourceAlphaBlendMode = BlendMode(blends[i + 3]);
            b.destinationAlphaBlendMode = BlendMode(blends[i + 4]);
            b.colorBlendOperation = BlendOp(blends[i + 5]);
            b.alphaBlendOperation = BlendOp(blends[i + 6]);
        }
        state.depthState.writeEnabled = d.depthWrite != 0;
        state.depthState.compareFunction = CompareFunction(d.depthCompare);
        state.rasterState.cullingMode = CullMode(d.cullingMode);
        state.rasterState.offsetUnits = d.offsetUnits;
        state.rasterState.offsetFactor = d.offsetFactor;
        state.rasterState.depthClip = (d.rasterFlags & 1) != 0;
        state.rasterState.conservative = (d.rasterFlags & 2) != 0;
        StencilState& s = state.stencilState;
        s.enabled = d.stencilEnabled != 0;
        s.readMask = u8(d.stencilReadMask);
        s.writeMask = u8(d.stencilWriteMask);
        s.compareFunctionFront = CompareFunction(d.stencilCompareFront);
        s.passOperationFront = StencilOp(d.stencilPassFront);
        s.failOperationFront = StencilOp(d.stencilFailFront);
        s.zFailOperationFront = StencilOp(d.stencilZFailFront);
        s.compareFunctionBack = CompareFunction(d.stencilCompareBack);
        s.passOperationBack = StencilOp(d.stencilPassBack);
        s.failOperationBack = StencilOp(d.stencilFailBack);
        s.zFailOperationBack = StencilOp(d.stencilZFailBack);
        state.stencilReference = d.stencilReference;
        c->DrawRenderers(CullingResults{reinterpret_cast<render::CullingData*>(uintptr_t(culling))}, drawing, filtering, state);
    }

    u64 RenderPipelineNative::VisibleLights(u64 culling, i32& count)
    {
        CullingResults r{reinterpret_cast<render::CullingData*>(uintptr_t(culling))};
        Span<const VisibleLight> lights = r.visibleLights();
        count = i32(lights.Size());
        return u64(reinterpret_cast<uintptr_t>(lights.Data()));
    }
}
