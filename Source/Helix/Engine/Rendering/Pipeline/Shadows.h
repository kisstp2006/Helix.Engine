#pragma once

#include <Helix/Engine/Rendering/Pipeline/RenderStates.h>
#include <Helix/Math/Geometry.h>

// Shadows of the scriptable pipeline API: the split data of a light's shadow map (a cascade), the matrices a
// directional light's cascade is drawn with, the shadow casters culled per split, and the settings of a renderer list
// of a light's shadow casters (their ShadowCaster passes). C# reads the structs' memory as they are.
namespace helix::engine
{
    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class BatchCullingProjectionType : i32
    {
        Unknown = 0,
        Perspective = 1,
        Orthographic = 2,
    };

    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class ShadowObjectsFilter : i32
    {
        AllObjects = 0,
        DynamicOnly = 1,
        StaticOnly = 2,
    };

    // The culling of a split: its sphere (xyz the center, w the radius), its planes (the positive side inside), the
    // matrix and near plane of its view.
    struct ShadowSplitData
    {
        static constexpr i32 maximumCullingPlaneCount = 10;
        Vector4 cullingSphere;
        Matrix4x4 cullingMatrix = Matrix4x4::identity;
        f32 cullingNearPlane = 0;
        f32 shadowCascadeBlendCullingFactor = 1;
        i32 cullingPlaneCount = 0;
        i32 reserved = 0;
        Plane planes[maximumCullingPlaneCount]{};

        Plane GetCullingPlane(i32 index) const { return index >= 0 && index < cullingPlaneCount ? planes[index] : Plane(); }
        void SetCullingPlane(i32 index, const Plane& plane)
        {
            if (index >= 0 && index < maximumCullingPlaneCount)
                planes[index] = plane;
        }
    };

    // The splits of a light in a ShadowCastersCullingInfos' split buffer; a split whose bit is set is not culled.
    struct LightShadowCasterCullingInfo
    {
        // RangeInt: start and length.
        i32 splitStart = 0;
        i32 splitLength = 0;
        BatchCullingProjectionType projectionType = BatchCullingProjectionType::Unknown;
        u16 splitExclusionMask = 0;
        u16 reserved = 0;
    };

    // Per visible light (by index) its splits.
    struct ShadowCastersCullingInfos
    {
        Span<const LightShadowCasterCullingInfo> perLightInfos;
        Span<const ShadowSplitData> splitBuffer;
    };

    // A renderer list of a visible light's shadow casters (the casters CullShadowCasters kept for the split).
    struct ShadowDrawingSettings
    {
        CullingResults cullingResults;
        i32 lightIndex = 0;
        i32 splitIndex = 0;
        ShadowObjectsFilter objectsFilter = ShadowObjectsFilter::AllObjects;
        u32 batchLayerMask = ~0u;
        bool useRenderingLayerMaskTest = false;
        BatchCullingProjectionType projectionType = BatchCullingProjectionType::Unknown;

        ShadowDrawingSettings() = default;
        ShadowDrawingSettings(CullingResults culling, i32 light, BatchCullingProjectionType projection = BatchCullingProjectionType::Unknown)
            : cullingResults(culling), lightIndex(light), projectionType(projection)
        {
        }
    };
}
