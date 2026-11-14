#pragma once

#include <Helix/Engine/Rendering/Pipeline/RenderStates.h>

// Renderer lists: what DrawRenderers draws, made once by the context and drawn by command buffers
// (CommandBuffer::DrawRendererList). A list lives as long as the context that made it.
namespace helix::engine
{
    class Camera;

    // A render state for the passes whose tag (RendererListParams::tagName) has a value.
    struct TaggedState
    {
        ShaderTagId tagValue;
        RenderStateBlock state;
    };

    struct RendererListParams
    {
        CullingResults cullingResults;
        DrawingSettings drawSettings;
        FilteringSettings filteringSettings;
        // With tag values and state blocks: each pass whose tag of this name has one of the values takes its state.
        ShaderTagId tagName;
        bool isPassTagName = false;
        Array<TaggedState> taggedStates = Array<TaggedState>();

        RendererListParams() = default;
        RendererListParams(CullingResults culling, const DrawingSettings& drawing, const FilteringSettings& filtering)
            : cullingResults(culling), drawSettings(drawing), filteringSettings(filtering)
        {
        }
    };

    namespace RendererUtils
    {
        struct RendererList
        {
            u64 id = 0;
            bool isValid() const;
            static RendererList nullRendererList() { return {}; }
        };

        // The reference's descriptor: pass names, the culling results and the camera with the filter's and the
        // sorting's values in one place.
        struct RendererListDesc
        {
            Array<ShaderTagId> passNames = Array<ShaderTagId>();
            CullingResults cullingResults;
            Camera* camera = nullptr;
            SortingCriteria sortingCriteria = SortingCriteria::None;
            RenderQueueRange renderQueueRange = RenderQueueRange::all();
            i32 layerMask = -1;
            u32 renderingLayerMask = ~0u;
            Material* overrideMaterial = nullptr;
            i32 overrideMaterialPassIndex = 0;
            Shader* overrideShader = nullptr;
            i32 overrideShaderPassIndex = 0;
            bool hasStateBlock = false;
            RenderStateBlock stateBlock;
            bool excludeObjectMotionVectors = false;
            PerObjectData rendererConfiguration = PerObjectData::None;

            RendererListDesc() = default;
            RendererListDesc(ShaderTagId passName, CullingResults culling, Camera* camera);
            RendererListDesc(Span<const ShaderTagId> names, CullingResults culling, Camera* camera);
            bool IsValid() const { return cullingResults.IsValid() && camera != nullptr && !passNames.Empty(); }
            RendererListParams ConvertToParameters() const;
        };
    }
}
