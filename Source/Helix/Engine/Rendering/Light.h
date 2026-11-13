#pragma once

#include <Helix/Engine/Math/MathTransfer.h>

#include <Helix/Engine/GameObject/GameObject.h>
#include <Helix/Math/Color.h>

// Light: its settings, and the list of enabled lights the renderer reads.
namespace helix::engine
{
    HX_ENUM(Namespace = "HelixEngine")
    enum class LightType : i32
    {
        Spot = 0,
        Directional = 1,
        Point = 2,
        Rectangle = 3,
        Disc = 4,
        Pyramid = 5,
        Box = 6,
        Tube = 7,
    };

    HX_ENUM(Namespace = "HelixEngine")
    enum class LightShadows : i32
    {
        None = 0,
        Hard = 1,
        Soft = 2,
    };

    HX_ENUM(Namespace = "HelixEngine")
    enum class LightRenderMode : i32
    {
        Auto = 0,
        ForcePixel = 1,
        ForceVertex = 2,
    };

    HX_CLASS(Namespace = "HelixEngine", Sealed)
    class Light final : public Behaviour
    {
        HX_ENGINE_CLASS(Light, Behaviour, ClassId::Light, ClassDisallowMultiple)

    public:
        Light() = default;
        Light(const Light&) = default;

        HX_PROPERTY() LightType type() const { return type_; }
        void SetType(LightType value) { type_ = value; }
        HX_PROPERTY() Color color() const { return color_; }
        void SetColor(Color value) { color_ = value; }
        HX_PROPERTY() f32 intensity() const { return intensity_; }
        void SetIntensity(f32 value) { intensity_ = value; }
        HX_PROPERTY() f32 range() const { return range_; }
        void SetRange(f32 value) { range_ = value; }
        HX_PROPERTY() f32 spotAngle() const { return spotAngle_; }
        void SetSpotAngle(f32 value) { spotAngle_ = value; }
        HX_PROPERTY() f32 innerSpotAngle() const { return innerSpotAngle_; }
        void SetInnerSpotAngle(f32 value) { innerSpotAngle_ = value; }
        HX_PROPERTY() LightShadows shadows() const { return shadows_; }
        void SetShadows(LightShadows value) { shadows_ = value; }
        HX_PROPERTY() f32 shadowStrength() const { return shadowStrength_; }
        void SetShadowStrength(f32 value) { shadowStrength_ = value; }
        HX_PROPERTY() f32 shadowBias() const { return shadowBias_; }
        void SetShadowBias(f32 value) { shadowBias_ = value; }
        HX_PROPERTY() f32 shadowNormalBias() const { return shadowNormalBias_; }
        void SetShadowNormalBias(f32 value) { shadowNormalBias_ = value; }
        HX_PROPERTY() f32 shadowNearPlane() const { return shadowNearPlane_; }
        void SetShadowNearPlane(f32 value) { shadowNearPlane_ = value; }
        u32 cullingMask() const { return cullingMask_; }
        void SetCullingMask(u32 value) { cullingMask_ = value; }
        u32 renderingLayerMask() const { return renderingLayerMask_; }
        void SetRenderingLayerMask(u32 value) { renderingLayerMask_ = value; }
        HX_PROPERTY() f32 bounceIntensity() const { return bounceIntensity_; }
        void SetBounceIntensity(f32 value) { bounceIntensity_ = value; }
        HX_PROPERTY() f32 colorTemperature() const { return colorTemperature_; }
        void SetColorTemperature(f32 value) { colorTemperature_ = value; }
        HX_PROPERTY() bool useColorTemperature() const { return useColorTemperature_; }
        void SetUseColorTemperature(bool value) { useColorTemperature_ = value; }
        HX_PROPERTY() LightRenderMode renderMode() const { return renderMode_; }
        void SetRenderMode(LightRenderMode value) { renderMode_ = value; }

        // The color the light adds (color, by the temperature's when used, times intensity), linear.
        // The masks as the script API has them (int).
        HX_PROPERTY(Name = "cullingMask", Setter = "SetCullingMaskApi") i32 CullingMaskApi() const { return i32(cullingMask_); }
        void SetCullingMaskApi(i32 value) { cullingMask_ = u32(value); }
        HX_PROPERTY(Name = "renderingLayerMask", Setter = "SetRenderingLayerMaskApi") i32 RenderingLayerMaskApi() const { return i32(renderingLayerMask_); }
        void SetRenderingLayerMaskApi(i32 value) { renderingLayerMask_ = u32(value); }
        // The settings of a new light.
        HX_FUNCTION() void Reset();

        Color FinalColor() const;
        // The enabled lights.
        static Span<Light* const> EnabledLights();

        void OnEnableNative();
        void OnDisableNative();

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("m_Type", type_);
            v.Field("m_Color", color_);
            v.Field("m_Intensity", intensity_);
            v.Field("m_Range", range_);
            v.Field("m_SpotAngle", spotAngle_);
            v.Field("m_InnerSpotAngle", innerSpotAngle_);
            v.Field("m_CullingMask", cullingMask_);
            v.Field("m_RenderingLayerMask", renderingLayerMask_);
            v.Field("m_BounceIntensity", bounceIntensity_);
            v.Field("m_ColorTemperature", colorTemperature_);
            v.Field("m_UseColorTemperature", useColorTemperature_);
            v.Field("m_RenderMode", renderMode_);
            ShadowSettings s{this};
            v.Field("m_Shadows", s);
        }

    private:
        struct ShadowSettings
        {
            static constexpr StringView kTypeName = "ShadowSettings";
            Light* l;
            template <class V>
            void Transfer(V& v)
            {
                v.Field("m_Type", l->shadows_);
                v.Field("m_Strength", l->shadowStrength_);
                v.Field("m_Bias", l->shadowBias_);
                v.Field("m_NormalBias", l->shadowNormalBias_);
                v.Field("m_NearPlane", l->shadowNearPlane_);
            }
        };

        LightType type_ = LightType::Point;
        Color color_{1, 1, 1, 1};
        f32 intensity_ = 1;
        f32 range_ = 10;
        f32 spotAngle_ = 30;
        f32 innerSpotAngle_ = 21.80208f;
        LightShadows shadows_ = LightShadows::None;
        f32 shadowStrength_ = 1;
        f32 shadowBias_ = 0.05f;
        f32 shadowNormalBias_ = 0.4f;
        f32 shadowNearPlane_ = 0.2f;
        u32 cullingMask_ = ~0u;
        u32 renderingLayerMask_ = 1;
        f32 bounceIntensity_ = 1;
        f32 colorTemperature_ = 6570;
        bool useColorTemperature_ = false;
        LightRenderMode renderMode_ = LightRenderMode::Auto;
    };
}
