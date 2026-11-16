#pragma once

#include <Helix/Engine/Material/Material.h>
#include <Helix/Engine/Rendering/SphericalHarmonicsL2.h>
#include <Helix/Engine/Texture/Texture.h>

// The scene's render settings the pipelines read: the skybox material (the skybox renderer list draws it), the ambient
// light (a probe the shaders read as unity_SHAr ... unity_SHC) and the reflection of objects no reflection probe
// reaches (unity_SpecCube0). A scene file keeps them (its RenderSettings document); they apply while that scene is the
// active one.
namespace helix::engine
{
    // Where the ambient light comes from.
    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class AmbientMode : i32
    {
        // The skybox's light (its probe is baked by the lightmapper, M9; until then ambientProbe as set).
        Skybox = 0,
        // Sky, equator and ground colors.
        Trilight = 1,
        Flat = 3,
        // ambientProbe as set.
        Custom = 4,
    };

    // The reflection of objects no reflection probe reaches.
    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class DefaultReflectionMode : i32
    {
        // The skybox's (baked by the lightmapper, M9; black until then).
        Skybox = 0,
        Custom = 1,
    };

    // The values of a scene's RenderSettings document (the reference's field names).
    struct RenderSettingsData
    {
        static constexpr StringView kTypeName = "RenderSettings";
        static constexpr i32 kVersion = 9;
        Color ambientSkyColor = Color(0.212f, 0.227f, 0.259f, 1);
        Color ambientEquatorColor = Color(0.114f, 0.125f, 0.133f, 1);
        Color ambientGroundColor = Color(0.047f, 0.043f, 0.035f, 1);
        f32 ambientIntensity = 1;
        i32 ambientMode = i32(AmbientMode::Skybox);
        PPtr<Material> skybox;
        i32 defaultReflectionMode = i32(DefaultReflectionMode::Skybox);
        i32 defaultReflectionResolution = 128;
        i32 reflectionBounces = 1;
        f32 reflectionIntensity = 1;
        PPtr<Texture> customReflection;

        template <class V>
        void Transfer(V& v)
        {
            v.Field("m_AmbientSkyColor", ambientSkyColor);
            v.Field("m_AmbientEquatorColor", ambientEquatorColor);
            v.Field("m_AmbientGroundColor", ambientGroundColor);
            v.Field("m_AmbientIntensity", ambientIntensity);
            v.Field("m_AmbientMode", ambientMode);
            v.Field("m_SkyboxMaterial", skybox);
            v.Field("m_DefaultReflectionMode", defaultReflectionMode);
            v.Field("m_DefaultReflectionResolution", defaultReflectionResolution);
            v.Field("m_ReflectionBounces", reflectionBounces);
            v.Field("m_ReflectionIntensity", reflectionIntensity);
            v.Field("m_CustomReflection", customReflection);
        }
    };

    HX_CLASS(Namespace = "HelixEngine", Static)
    class RenderSettings
    {
    public:
        HX_PROPERTY() static Material* skybox();
        static void SetSkybox(Material* material);

        HX_PROPERTY() static AmbientMode ambientMode();
        static void SetAmbientMode(AmbientMode value);
        // The flat ambient color (the same value as ambientSkyColor).
        HX_PROPERTY() static Color ambientLight() { return ambientSkyColor(); }
        static void SetAmbientLight(Color value) { SetAmbientSkyColor(value); }
        HX_PROPERTY() static Color ambientSkyColor();
        static void SetAmbientSkyColor(Color value);
        HX_PROPERTY() static Color ambientEquatorColor();
        static void SetAmbientEquatorColor(Color value);
        HX_PROPERTY() static Color ambientGroundColor();
        static void SetAmbientGroundColor(Color value);
        HX_PROPERTY() static f32 ambientIntensity();
        static void SetAmbientIntensity(f32 value);
        // The ambient light the shaders read: made from the colors in the Flat and Trilight modes, as set otherwise.
        static SphericalHarmonicsL2 ambientProbe();
        static void SetAmbientProbe(const SphericalHarmonicsL2& probe);
        // C#'s SphericalHarmonicsL2: its 27 coefficients (red, green, blue nine each).
        HX_PROPERTY(Name = "ambientProbeCoefficients", Setter = "SetAmbientProbeCoefficients", Internal) static Array<f32> ambientProbeCoefficients();
        static void SetAmbientProbeCoefficients(Array<f32> values);

        HX_PROPERTY() static DefaultReflectionMode defaultReflectionMode();
        static void SetDefaultReflectionMode(DefaultReflectionMode value);
        HX_PROPERTY() static Texture* customReflectionTexture();
        static void SetCustomReflectionTexture(Texture* texture);
        HX_PROPERTY() static i32 defaultReflectionResolution();
        static void SetDefaultReflectionResolution(i32 value);
        HX_PROPERTY() static f32 reflectionIntensity();
        static void SetReflectionIntensity(f32 value);
        HX_PROPERTY() static i32 reflectionBounces();
        static void SetReflectionBounces(i32 value);

        // The cube objects without a reflection probe reflect: the custom one in the Custom mode, else black.
        static Texture* defaultReflection();
        // The settings as a scene file keeps them, and set from one (the active scene's).
        static RenderSettingsData Capture();
        static void Apply(const RenderSettingsData& data);
        // The shaders' globals of the ambient light and the default reflection (each frame, where they changed).
        static void ApplyGlobals();
    };
}
