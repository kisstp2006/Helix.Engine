#pragma once

#include <Helix/Engine/GameObject/GameObject.h>
#include <Helix/Math/Color.h>

// Light: its settings, and the list of enabled lights the renderer reads.
namespace helix::engine
{
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

    enum class LightShadows : i32
    {
        None = 0,
        Hard = 1,
        Soft = 2,
    };

    enum class LightRenderMode : i32
    {
        Auto = 0,
        ForcePixel = 1,
        ForceVertex = 2,
    };

    class Light final : public Behaviour
    {
        HX_ENGINE_CLASS(Light, Behaviour, ClassId::Light, ClassDisallowMultiple)

    public:
        Light() = default;
        Light(const Light&) = default;

        LightType type() const { return type_; }
        void SetType(LightType value) { type_ = value; }
        Color color() const { return color_; }
        void SetColor(Color value) { color_ = value; }
        f32 intensity() const { return intensity_; }
        void SetIntensity(f32 value) { intensity_ = value; }
        f32 range() const { return range_; }
        void SetRange(f32 value) { range_ = value; }
        f32 spotAngle() const { return spotAngle_; }
        void SetSpotAngle(f32 value) { spotAngle_ = value; }
        f32 innerSpotAngle() const { return innerSpotAngle_; }
        void SetInnerSpotAngle(f32 value) { innerSpotAngle_ = value; }
        LightShadows shadows() const { return shadows_; }
        void SetShadows(LightShadows value) { shadows_ = value; }
        f32 shadowStrength() const { return shadowStrength_; }
        void SetShadowStrength(f32 value) { shadowStrength_ = value; }
        f32 shadowBias() const { return shadowBias_; }
        void SetShadowBias(f32 value) { shadowBias_ = value; }
        f32 shadowNormalBias() const { return shadowNormalBias_; }
        void SetShadowNormalBias(f32 value) { shadowNormalBias_ = value; }
        f32 shadowNearPlane() const { return shadowNearPlane_; }
        void SetShadowNearPlane(f32 value) { shadowNearPlane_ = value; }
        u32 cullingMask() const { return cullingMask_; }
        void SetCullingMask(u32 value) { cullingMask_ = value; }
        u32 renderingLayerMask() const { return renderingLayerMask_; }
        void SetRenderingLayerMask(u32 value) { renderingLayerMask_ = value; }
        f32 bounceIntensity() const { return bounceIntensity_; }
        void SetBounceIntensity(f32 value) { bounceIntensity_ = value; }
        f32 colorTemperature() const { return colorTemperature_; }
        void SetColorTemperature(f32 value) { colorTemperature_ = value; }
        bool useColorTemperature() const { return useColorTemperature_; }
        void SetUseColorTemperature(bool value) { useColorTemperature_ = value; }
        LightRenderMode renderMode() const { return renderMode_; }
        void SetRenderMode(LightRenderMode value) { renderMode_ = value; }

        // The color the light adds (color, by the temperature's when used, times intensity), linear.
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
            Rgba c{&color_.r};
            v.Field("m_Color", c);
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
        struct Rgba
        {
            static constexpr StringView kTypeName = "ColorRGBA";
            static constexpr bool kFlow = true;
            f32* c;
            template <class V>
            void Transfer(V& v)
            {
                v.Field("r", c[0]);
                v.Field("g", c[1]);
                v.Field("b", c[2]);
                v.Field("a", c[3]);
            }
        };
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
