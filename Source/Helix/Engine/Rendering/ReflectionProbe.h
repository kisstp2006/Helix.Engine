#pragma once

#include <Helix/Engine/Math/MathTransfer.h>

#include <Helix/Engine/GameObject/GameObject.h>
#include <Helix/Engine/Texture/Texture.h>
#include <Helix/Math/Color.h>
#include <Helix/Math/Geometry.h>

// ReflectionProbe: a box of the scene whose surroundings a cube texture holds (baked, or a custom one), reflected by
// the renderers inside it; the list of enabled probes culling reads.
namespace helix::engine
{
    namespace ClassId
    {
        inline constexpr i32 ReflectionProbe = 215;
    }

    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class ReflectionProbeMode : i32
    {
        Baked = 0,
        Realtime = 1,
        Custom = 2,
    };

    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class ReflectionProbeRefreshMode : i32
    {
        OnAwake = 0,
        EveryFrame = 1,
        ViaScripting = 2,
    };

    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class ReflectionProbeTimeSlicingMode : i32
    {
        AllFacesAtOnce = 0,
        IndividualFaces = 1,
        NoTimeSlicing = 2,
    };

    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class ReflectionProbeClearFlags : i32
    {
        Skybox = 1,
        SolidColor = 2,
    };

    // How a renderer takes its reflections from the probes around it.
    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class ReflectionProbeUsage : i32
    {
        Off = 0,
        BlendProbes = 1,
        BlendProbesAndSkybox = 2,
        Simple = 3,
    };

    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class ReflectionProbeSortingCriteria : i32
    {
        None = 0,
        Importance = 1,
        Size = 2,
        ImportanceThenSize = 3,
    };

    class ReflectionProbe;

    // A probe a renderer reflects and its share.
    struct ReflectionProbeBlendInfo
    {
        ReflectionProbe* probe = nullptr;
        f32 weight = 0;
    };

    HX_CLASS(Namespace = "HelixEngine", Sealed)
    class ReflectionProbe final : public Behaviour
    {
        HX_ENGINE_CLASS(ReflectionProbe, Behaviour, ClassId::ReflectionProbe, ClassDisallowMultiple)

    public:
        ReflectionProbe() = default;
        ReflectionProbe(const ReflectionProbe&) = default;

        HX_PROPERTY() Vector3 size() const { return size_; }
        void SetSize(Vector3 value) { size_ = value; }
        HX_PROPERTY() Vector3 center() const { return center_; }
        void SetCenter(Vector3 value) { center_ = value; }
        HX_PROPERTY() f32 nearClipPlane() const { return nearClip_; }
        void SetNearClipPlane(f32 value) { nearClip_ = value; }
        HX_PROPERTY() f32 farClipPlane() const { return farClip_; }
        void SetFarClipPlane(f32 value) { farClip_ = value; }
        HX_PROPERTY() f32 intensity() const { return intensity_; }
        void SetIntensity(f32 value) { intensity_ = value; }
        HX_PROPERTY() bool hdr() const { return hdr_; }
        void SetHdr(bool value) { hdr_ = value; }
        HX_PROPERTY() bool renderDynamicObjects() const { return renderDynamicObjects_; }
        void SetRenderDynamicObjects(bool value) { renderDynamicObjects_ = value; }
        HX_PROPERTY() f32 shadowDistance() const { return shadowDistance_; }
        void SetShadowDistance(f32 value) { shadowDistance_ = value; }
        HX_PROPERTY() i32 resolution() const { return resolution_; }
        void SetResolution(i32 value) { resolution_ = value; }
        HX_PROPERTY(Name = "cullingMask", Setter = "SetCullingMaskApi") i32 CullingMaskApi() const { return i32(cullingMask_); }
        void SetCullingMaskApi(i32 value) { cullingMask_ = u32(value); }
        u32 cullingMask() const { return cullingMask_; }
        HX_PROPERTY() ReflectionProbeClearFlags clearFlags() const { return clearFlags_; }
        void SetClearFlags(ReflectionProbeClearFlags value) { clearFlags_ = value; }
        HX_PROPERTY() Color backgroundColor() const { return backgroundColor_; }
        void SetBackgroundColor(Color value) { backgroundColor_ = value; }
        HX_PROPERTY() f32 blendDistance() const { return blendDistance_; }
        void SetBlendDistance(f32 value) { blendDistance_ = value; }
        HX_PROPERTY() bool boxProjection() const { return boxProjection_; }
        void SetBoxProjection(bool value) { boxProjection_ = value; }
        HX_PROPERTY() ReflectionProbeMode mode() const { return mode_; }
        void SetMode(ReflectionProbeMode value) { mode_ = value; }
        HX_PROPERTY() i32 importance() const { return importance_; }
        void SetImportance(i32 value) { importance_ = value; }
        HX_PROPERTY() ReflectionProbeRefreshMode refreshMode() const { return refreshMode_; }
        void SetRefreshMode(ReflectionProbeRefreshMode value) { refreshMode_ = value; }
        HX_PROPERTY() ReflectionProbeTimeSlicingMode timeSlicingMode() const { return timeSlicingMode_; }
        void SetTimeSlicingMode(ReflectionProbeTimeSlicingMode value) { timeSlicingMode_ = value; }
        HX_PROPERTY() Texture* bakedTexture() const { return bakedTexture_.Get(); }
        void SetBakedTexture(Texture* value) { bakedTexture_ = value; }
        HX_PROPERTY() Texture* customBakedTexture() const { return customBakedTexture_.Get(); }
        void SetCustomBakedTexture(Texture* value) { customBakedTexture_ = value; }
        // The cube the probe reflects: the custom one in Custom mode, else the baked one.
        HX_PROPERTY(ReadOnly) Texture* texture() const { return mode_ == ReflectionProbeMode::Custom ? customBakedTexture() : bakedTexture(); }
        // How shaders decode the texture: x its multiplier (the intensity), y the exponent (linear textures: 1).
        HX_PROPERTY(ReadOnly) Vector4 textureHDRDecodeValues() const { return Vector4(intensity_, 1, 0, 0); }
        // The box in world space: the probe's position and center, its size (the probe's rotation does not turn it).
        HX_PROPERTY(ReadOnly) Bounds bounds() const;
        HX_PROPERTY(ReadOnly) static i32 minBakedCubemapResolution() { return 16; }
        HX_PROPERTY(ReadOnly) static i32 maxBakedCubemapResolution() { return 2048; }
        // The settings of a new probe.
        HX_FUNCTION() void Reset();

        // The enabled probes.
        static Span<ReflectionProbe* const> EnabledProbes();
        // The probes (of `candidates`) a renderer of these bounds reflects, at most two, with their weights: the most
        // important first (the smaller box between equals), each weighted by how deep the bounds' center is inside its
        // box (1 inside, falling to 0 at blendDistance outside); BlendProbes shares the whole weight among the two,
        // BlendProbesAndSkybox leaves the rest to the skybox, Simple takes the first alone.
        static u32 ClosestProbes(const Bounds& rendererBounds, ReflectionProbeUsage usage, Span<ReflectionProbe* const> candidates,
                                 ReflectionProbeBlendInfo out[2]);

        void OnEnableNative();
        void OnDisableNative();

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("m_Mode", mode_);
            v.Field("m_RefreshMode", refreshMode_);
            v.Field("m_TimeSlicingMode", timeSlicingMode_);
            v.Field("m_Resolution", resolution_);
            v.Field("m_BoxSize", size_);
            v.Field("m_BoxOffset", center_);
            v.Field("m_NearClip", nearClip_);
            v.Field("m_FarClip", farClip_);
            v.Field("m_ShadowDistance", shadowDistance_);
            v.Field("m_ClearFlags", clearFlags_);
            v.Field("m_BackGroundColor", backgroundColor_);
            v.Field("m_CullingMask", cullingMask_);
            v.Field("m_IntensityMultiplier", intensity_);
            v.Field("m_BlendDistance", blendDistance_);
            v.Field("m_HDR", hdr_);
            v.Field("m_BoxProjection", boxProjection_);
            v.Field("m_RenderDynamicObjects", renderDynamicObjects_);
            v.Field("m_UseOcclusionCulling", useOcclusionCulling_);
            v.Field("m_Importance", importance_);
            v.Field("m_CustomBakedTexture", customBakedTexture_);
            v.Field("m_BakedTexture", bakedTexture_);
        }

    private:
        ReflectionProbeMode mode_ = ReflectionProbeMode::Baked;
        ReflectionProbeRefreshMode refreshMode_ = ReflectionProbeRefreshMode::OnAwake;
        ReflectionProbeTimeSlicingMode timeSlicingMode_ = ReflectionProbeTimeSlicingMode::AllFacesAtOnce;
        i32 resolution_ = 128;
        Vector3 size_{10, 10, 10};
        Vector3 center_;
        f32 nearClip_ = 0.3f;
        f32 farClip_ = 1000;
        f32 shadowDistance_ = 100;
        ReflectionProbeClearFlags clearFlags_ = ReflectionProbeClearFlags::Skybox;
        Color backgroundColor_{0.192157f, 0.301961f, 0.474510f, 0};
        u32 cullingMask_ = ~0u;
        f32 intensity_ = 1;
        f32 blendDistance_ = 1;
        bool hdr_ = true;
        bool boxProjection_ = false;
        bool renderDynamicObjects_ = false;
        bool useOcclusionCulling_ = true;
        i32 importance_ = 1;
        PPtr<Texture> customBakedTexture_;
        PPtr<Texture> bakedTexture_;
    };
}
