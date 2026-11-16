#include <Helix/Engine/Rendering/RenderSettings.h>

#include <Helix/Engine/Rendering/Graphics.h>
#include <Helix/Engine/Shader/Shader.h>

#include <cmath>

namespace helix::engine
{
    namespace
    {
        struct Settings
        {
            RenderSettingsData data;
            // The probe as set (the Skybox and Custom modes take it).
            SphericalHarmonicsL2 probe;
        };

        Settings& S()
        {
            static Settings s;
            return s;
        }

        // The trilight's light of a direction: the sky above, the ground below, the equator between.
        Color Trilight(const RenderSettingsData& d, Vector3 n)
        {
            if (n.y >= 0)
                return Color::Lerp(d.ambientEquatorColor, d.ambientSkyColor, n.y);
            return Color::Lerp(d.ambientEquatorColor, d.ambientGroundColor, -n.y);
        }

        // A function of directions projected over a Fibonacci sphere and convolved with the cosine lobe (over pi).
        template <class F>
        SphericalHarmonicsL2 Project(F&& f)
        {
            constexpr i32 kSamples = 512;
            constexpr f32 kBand[9] = {1.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f};
            SphericalHarmonicsL2 sh;
            const f32 golden = 3.14159265f * (3.0f - std::sqrt(5.0f));
            for (i32 s = 0; s < kSamples; s++)
            {
                f32 y = 1.0f - (f32(s) + 0.5f) * 2.0f / f32(kSamples);
                f32 r = std::sqrt(Max(0.0f, 1.0f - y * y));
                f32 a = golden * f32(s);
                Vector3 n(std::cos(a) * r, y, std::sin(a) * r);
                Color c = f(n);
                f32 b[9];
                SphericalHarmonicsBasis(n, b);
                for (i32 i = 0; i < 9; i++)
                {
                    sh.coefficients[0][i] += c.r * b[i];
                    sh.coefficients[1][i] += c.g * b[i];
                    sh.coefficients[2][i] += c.b * b[i];
                }
            }
            const f32 weight = 4.0f * 3.14159265f / f32(kSamples);
            for (i32 c = 0; c < 3; c++)
                for (i32 i = 0; i < 9; i++)
                    sh.coefficients[c][i] *= weight * kBand[i];
            return sh;
        }

        const char* const kShaNames[7] = {"unity_SHAr", "unity_SHAg", "unity_SHAb", "unity_SHBr", "unity_SHBg", "unity_SHBb", "unity_SHC"};
    }

    Material* RenderSettings::skybox() { return S().data.skybox.Get(); }

    void RenderSettings::SetSkybox(Material* material) { S().data.skybox = PPtr<Material>(material); }

    AmbientMode RenderSettings::ambientMode() { return AmbientMode(S().data.ambientMode); }

    void RenderSettings::SetAmbientMode(AmbientMode value) { S().data.ambientMode = i32(value); }

    Color RenderSettings::ambientSkyColor() { return S().data.ambientSkyColor; }

    void RenderSettings::SetAmbientSkyColor(Color value)
    {
        S().data.ambientSkyColor = value;
        // The camera block's ambient color (what the fallback pipeline's shaders read) follows the flat color.
        Graphics::SetAmbientLight(value);
    }

    Color RenderSettings::ambientEquatorColor() { return S().data.ambientEquatorColor; }

    void RenderSettings::SetAmbientEquatorColor(Color value) { S().data.ambientEquatorColor = value; }

    Color RenderSettings::ambientGroundColor() { return S().data.ambientGroundColor; }

    void RenderSettings::SetAmbientGroundColor(Color value) { S().data.ambientGroundColor = value; }

    f32 RenderSettings::ambientIntensity() { return S().data.ambientIntensity; }

    void RenderSettings::SetAmbientIntensity(f32 value) { S().data.ambientIntensity = Max(0.0f, value); }

    SphericalHarmonicsL2 RenderSettings::ambientProbe()
    {
        const RenderSettingsData& d = S().data;
        switch (AmbientMode(d.ambientMode))
        {
        case AmbientMode::Flat:
        {
            SphericalHarmonicsL2 sh;
            sh.AddAmbientLight(d.ambientSkyColor);
            return sh;
        }
        case AmbientMode::Trilight:
            return Project([&](Vector3 n) { return Trilight(d, n); });
        default:
            return S().probe;
        }
    }

    void RenderSettings::SetAmbientProbe(const SphericalHarmonicsL2& probe) { S().probe = probe; }

    Array<f32> RenderSettings::ambientProbeCoefficients()
    {
        SphericalHarmonicsL2 sh = ambientProbe();
        Array<f32> out;
        for (i32 c = 0; c < 3; c++)
            for (i32 i = 0; i < 9; i++)
                out.Push(sh.coefficients[c][i]);
        return out;
    }

    void RenderSettings::SetAmbientProbeCoefficients(Array<f32> values)
    {
        SphericalHarmonicsL2 sh;
        for (u32 k = 0; k < values.Size() && k < 27; k++)
            sh.coefficients[k / 9][k % 9] = values[k];
        SetAmbientProbe(sh);
    }

    DefaultReflectionMode RenderSettings::defaultReflectionMode() { return DefaultReflectionMode(S().data.defaultReflectionMode); }

    void RenderSettings::SetDefaultReflectionMode(DefaultReflectionMode value) { S().data.defaultReflectionMode = i32(value); }

    Texture* RenderSettings::customReflectionTexture() { return S().data.customReflection.Get(); }

    void RenderSettings::SetCustomReflectionTexture(Texture* texture) { S().data.customReflection = PPtr<Texture>(texture); }

    i32 RenderSettings::defaultReflectionResolution() { return S().data.defaultReflectionResolution; }

    void RenderSettings::SetDefaultReflectionResolution(i32 value) { S().data.defaultReflectionResolution = Clamp(value, 16, 2048); }

    f32 RenderSettings::reflectionIntensity() { return S().data.reflectionIntensity; }

    void RenderSettings::SetReflectionIntensity(f32 value) { S().data.reflectionIntensity = Clamp(value, 0.0f, 1.0f); }

    i32 RenderSettings::reflectionBounces() { return S().data.reflectionBounces; }

    void RenderSettings::SetReflectionBounces(i32 value) { S().data.reflectionBounces = Clamp(value, 1, 5); }

    Texture* RenderSettings::defaultReflection()
    {
        const RenderSettingsData& d = S().data;
        Texture* custom = d.customReflection.Get();
        if (DefaultReflectionMode(d.defaultReflectionMode) == DefaultReflectionMode::Custom && custom)
            return custom;
        return Cubemap::black();
    }

    RenderSettingsData RenderSettings::Capture() { return S().data; }

    void RenderSettings::Apply(const RenderSettingsData& data)
    {
        S().data = data;
        Graphics::SetAmbientLight(data.ambientSkyColor);
    }

    void RenderSettings::ApplyGlobals()
    {
        PropertySheet& globals = Shader::Globals();
        Vector4 constants[7];
        ambientProbe().ShaderConstants(constants);
        for (i32 i = 0; i < 7; i++)
        {
            i32 id = Shader::PropertyToID(kShaNames[i]);
            if (!globals.Has(id) || globals.GetVector(id) != constants[i])
                globals.SetVector(id, constants[i]);
        }
        i32 cube = Shader::PropertyToID("unity_SpecCube0");
        Texture* reflection = defaultReflection();
        EntityId id = reflection ? reflection->GetEntityId() : EntityId();
        if (globals.GetTexture(cube) != id)
            globals.SetTexture(cube, id);
    }
}
