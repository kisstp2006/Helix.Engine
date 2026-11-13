#pragma once

#include <Helix/Engine/Object/EntityId.h>
#include <Helix/Engine/Rendering/Light.h>
#include <Helix/Engine/Shader/ShaderKeywords.h>
#include <Helix/Math/Color.h>
#include <Helix/Math/Geometry.h>
#include <Helix/Math/Matrix4x4.h>
#include <Helix/Scripting/Bindings/Tags.h>

// The values of the scriptable pipeline API: how DrawRenderers sorts, filters and overrides render state, what culling
// takes and gives. C++ pipelines use these structs as they are; C# has the reference's structs (Helix.ScriptCore) and
// hands them over as these.
namespace helix::engine
{
    class Camera;
    class Material;
    class Shader;

    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class CompareFunction : i32
    {
        Disabled = 0,
        Never = 1,
        Less = 2,
        Equal = 3,
        LessEqual = 4,
        Greater = 5,
        NotEqual = 6,
        GreaterEqual = 7,
        Always = 8,
    };

    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class BlendMode : i32
    {
        Zero = 0,
        One = 1,
        DstColor = 2,
        SrcColor = 3,
        OneMinusDstColor = 4,
        SrcAlpha = 5,
        OneMinusSrcColor = 6,
        DstAlpha = 7,
        OneMinusDstAlpha = 8,
        SrcAlphaSaturate = 9,
        OneMinusSrcAlpha = 10,
    };

    // Add to Max reach the GPU; the logical and advanced operations draw as Add until the devices have them.
    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class BlendOp : i32
    {
        Add = 0,
        Subtract = 1,
        ReverseSubtract = 2,
        Min = 3,
        Max = 4,
        LogicalClear = 5,
        LogicalSet = 6,
        LogicalCopy = 7,
        LogicalCopyInverted = 8,
        LogicalNoop = 9,
        LogicalInvert = 10,
        LogicalAnd = 11,
        LogicalNand = 12,
        LogicalOr = 13,
        LogicalNor = 14,
        LogicalXor = 15,
        LogicalEquivalence = 16,
        LogicalAndReverse = 17,
        LogicalAndInverted = 18,
        LogicalOrReverse = 19,
        LogicalOrInverted = 20,
        Multiply = 21,
        Screen = 22,
        Overlay = 23,
        Darken = 24,
        Lighten = 25,
        ColorDodge = 26,
        ColorBurn = 27,
        HardLight = 28,
        SoftLight = 29,
        Difference = 30,
        Exclusion = 31,
        HSLHue = 32,
        HSLSaturation = 33,
        HSLColor = 34,
        HSLLuminosity = 35,
    };

    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class CullMode : i32
    {
        Off = 0,
        Front = 1,
        Back = 2,
    };

    HX_ENUM(Namespace = "HelixEngine.Rendering", Attributes = "[System.Flags]")
    enum class ColorWriteMask : i32
    {
        Alpha = 1,
        Blue = 2,
        Green = 4,
        Red = 8,
        All = 15,
    };

    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class StencilOp : i32
    {
        Keep = 0,
        Zero = 1,
        Replace = 2,
        IncrementSaturate = 3,
        DecrementSaturate = 4,
        Invert = 5,
        IncrementWrap = 6,
        DecrementWrap = 7,
    };

    HX_ENUM(Namespace = "HelixEngine.Rendering", Attributes = "[System.Flags]")
    enum class SortingCriteria : i32
    {
        None = 0,
        SortingLayer = 1,
        RenderQueue = 2,
        BackToFront = 4,
        QuantizedFrontToBack = 8,
        OptimizeStateChanges = 16,
        CanvasOrder = 32,
        RendererPriority = 64,
        CommonOpaque = 59,
        CommonTransparent = 23,
    };

    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class DistanceMetric : i32
    {
        Perspective = 0,
        Orthographic = 1,
        CustomAxis = 2,
    };

    // What each drawn object gets beyond its matrices: accepted, the data comes with the pipelines' lighting.
    HX_ENUM(Namespace = "HelixEngine.Rendering", Attributes = "[System.Flags]")
    enum class PerObjectData : i32
    {
        None = 0,
        LightProbe = 1,
        ReflectionProbes = 2,
        LightProbeProxyVolume = 4,
        Lightmaps = 8,
        LightData = 16,
        MotionVectors = 32,
        LightIndices = 64,
        ReflectionProbeData = 128,
        OcclusionProbe = 256,
        OcclusionProbeProxyVolume = 512,
        ShadowMask = 1024,
    };

    HX_ENUM(Namespace = "HelixEngine.Rendering", Attributes = "[System.Flags]")
    enum class RenderStateMask : i32
    {
        Nothing = 0,
        Blend = 1,
        Raster = 2,
        Depth = 4,
        Stencil = 8,
        Everything = 15,
    };

    HX_ENUM(Namespace = "HelixEngine.Rendering", Attributes = "[System.Flags]")
    enum class CullingOptions : i32
    {
        None = 0,
        ForceEvenIfCameraIsNotActive = 1,
        OcclusionCull = 2,
        NeedsLighting = 4,
        NeedsReflectionProbes = 8,
        Stereo = 16,
        DisablePerObjectCulling = 32,
        ShadowCasters = 64,
    };

    inline bool HasFlag(SortingCriteria all, SortingCriteria flag) { return (i32(all) & i32(flag)) != 0; }
    inline bool HasFlag(RenderStateMask all, RenderStateMask flag) { return (i32(all) & i32(flag)) != 0; }

    // ------------------------------------------------------------------------------------------------ settings

    struct RenderQueueRange
    {
        static constexpr i32 minimumBound = 0;
        static constexpr i32 maximumBound = 5000;
        i32 lowerBound = minimumBound;
        i32 upperBound = maximumBound;

        static RenderQueueRange all() { return {minimumBound, maximumBound}; }
        static RenderQueueRange opaque() { return {minimumBound, 2500}; }
        static RenderQueueRange transparent() { return {2501, maximumBound}; }
        bool Contains(i32 queue) const { return queue >= lowerBound && queue <= upperBound; }
    };

    struct SortingSettings
    {
        Matrix4x4 worldToCameraMatrix = Matrix4x4::identity;
        Vector3 cameraPosition;
        Vector3 customAxis;
        SortingCriteria criteria = SortingCriteria::None;
        DistanceMetric distanceMetric = DistanceMetric::Perspective;

        SortingSettings() = default;
        // The camera's matrix, position and metric (orthographic cameras sort along their view direction).
        explicit SortingSettings(const Camera* camera);
    };

    struct DrawingSettings
    {
        static constexpr i32 maxShaderPasses = 16;
        SortingSettings sortingSettings;
        PerObjectData perObjectData = PerObjectData::None;
        // Accepted: batching and instancing come with the render core.
        bool enableDynamicBatching = false;
        bool enableInstancing = true;
        // Every drawn object with this material's pass instead of its own (objects that have a pass of the names).
        Material* overrideMaterial = nullptr;
        i32 overrideMaterialPassIndex = 0;
        // Every drawn object with this shader's pass and its own material's values.
        Shader* overrideShader = nullptr;
        i32 overrideShaderPassIndex = 0;
        // Objects whose shader has no pass of the names draw with this material's first pass.
        Material* fallbackMaterial = nullptr;
        i32 mainLightIndex = -1;
        ShaderTagId passNames[maxShaderPasses]{};

        DrawingSettings() = default;
        DrawingSettings(ShaderTagId shaderPassName, const SortingSettings& sorting) : sortingSettings(sorting) { passNames[0] = shaderPassName; }
        ShaderTagId GetShaderPassName(i32 index) const { return index >= 0 && index < maxShaderPasses ? passNames[index] : ShaderTagId(); }
        void SetShaderPassName(i32 index, ShaderTagId name)
        {
            if (index >= 0 && index < maxShaderPasses)
                passNames[index] = name;
        }
    };

    struct FilteringSettings
    {
        RenderQueueRange renderQueueRange = RenderQueueRange::all();
        i32 layerMask = -1;
        u32 renderingLayerMask = ~0u;
        u32 batchLayerMask = ~0u;
        bool excludeMotionVectorObjects = false;

        FilteringSettings() = default;
        explicit FilteringSettings(RenderQueueRange range, i32 layers = -1, u32 renderingLayers = ~0u, bool excludeMotionVectors = false)
            : renderQueueRange(range), layerMask(layers), renderingLayerMask(renderingLayers), excludeMotionVectorObjects(excludeMotionVectors)
        {
        }
        static FilteringSettings defaultValue() { return FilteringSettings(); }
    };

    // ------------------------------------------------------------------------------------------------ render state

    struct RenderTargetBlendState
    {
        ColorWriteMask writeMask = ColorWriteMask::All;
        BlendMode sourceColorBlendMode = BlendMode::One;
        BlendMode destinationColorBlendMode = BlendMode::Zero;
        BlendMode sourceAlphaBlendMode = BlendMode::One;
        BlendMode destinationAlphaBlendMode = BlendMode::Zero;
        BlendOp colorBlendOperation = BlendOp::Add;
        BlendOp alphaBlendOperation = BlendOp::Add;
    };

    // Targets other than the first, alphaToMask: kept for the targets and devices of later steps.
    struct BlendState
    {
        bool separateMRTBlendStates = false;
        bool alphaToMask = false;
        RenderTargetBlendState targets[8]{};

        static BlendState defaultValue() { return BlendState(); }
    };

    struct DepthState
    {
        bool writeEnabled = true;
        CompareFunction compareFunction = CompareFunction::Less;

        static DepthState defaultValue() { return DepthState(); }
    };

    // Offsets, depth clip and conservative rasterization are kept until the devices have them.
    struct RasterState
    {
        CullMode cullingMode = CullMode::Back;
        i32 offsetUnits = 0;
        f32 offsetFactor = 0;
        bool depthClip = true;
        bool conservative = false;

        static RasterState defaultValue() { return RasterState(); }
    };

    // Kept until the devices have stencil state.
    struct StencilState
    {
        bool enabled = false;
        u8 readMask = 255;
        u8 writeMask = 255;
        CompareFunction compareFunctionFront = CompareFunction::Always;
        StencilOp passOperationFront = StencilOp::Keep;
        StencilOp failOperationFront = StencilOp::Keep;
        StencilOp zFailOperationFront = StencilOp::Keep;
        CompareFunction compareFunctionBack = CompareFunction::Always;
        StencilOp passOperationBack = StencilOp::Keep;
        StencilOp failOperationBack = StencilOp::Keep;
        StencilOp zFailOperationBack = StencilOp::Keep;

        static StencilState defaultValue() { return StencilState(); }
    };

    struct RenderStateBlock
    {
        RenderStateMask mask = RenderStateMask::Nothing;
        BlendState blendState;
        RasterState rasterState;
        DepthState depthState;
        StencilState stencilState;
        i32 stencilReference = 0;

        RenderStateBlock() = default;
        explicit RenderStateBlock(RenderStateMask m) : mask(m) {}
    };

    // ------------------------------------------------------------------------------------------------ culling

    struct ScriptableCullingParameters
    {
        static constexpr i32 maximumCullingPlanes = 10;
        i32 cullingMask = -1;
        Matrix4x4 cullingMatrix = Matrix4x4::identity;
        Plane planes[maximumCullingPlanes]{};
        i32 cullingPlaneCount = 6;
        CullingOptions cullingOptions = CullingOptions::None;
        Vector3 origin;
        bool isOrthographic = false;
        // The camera the parameters came from: its renderers' OnWillRenderObject and Camera.current while culling.
        EntityId camera;

        Plane GetCullingPlane(i32 index) const { return index >= 0 && index < cullingPlaneCount ? planes[index] : Plane(); }
        void SetCullingPlane(i32 index, const Plane& plane)
        {
            if (index >= 0 && index < maximumCullingPlanes)
                planes[index] = plane;
        }
    };

    // A light that passed culling (the reference's VisibleLight; C# reads the same memory through a NativeArray).
    struct VisibleLight
    {
        // The Light's EntityId.
        u64 lightId = 0;
        LightType lightType = LightType::Directional;
        Color finalColor;
        // Its rect on the screen in pixels (the whole camera for directional lights).
        Rect screenRect;
        Matrix4x4 localToWorldMatrix = Matrix4x4::identity;
        f32 range = 0;
        f32 spotAngle = 0;
        // Bit 0: its volume crosses the near plane, bit 1: the far plane.
        u32 flags = 0;

        Light* light() const;
        bool intersectsNearPlane() const { return (flags & 1u) != 0; }
        bool intersectsFarPlane() const { return (flags & 2u) != 0; }
    };
    static_assert(sizeof(VisibleLight) == 120, "VisibleLight: the layout C# reads");

    namespace render
    {
        struct CullingData;
    }

    // What Cull found: the renderers and lights inside the planes, for the context's draws (valid while the context
    // that made it renders).
    struct CullingResults
    {
        render::CullingData* data = nullptr;

        Span<const VisibleLight> visibleLights() const;
        bool IsValid() const;
    };
}
