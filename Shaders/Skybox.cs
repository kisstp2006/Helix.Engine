using HelixEngine.Shaders;
using static HelixEngine.Shaders.ShaderMath;

namespace HelixEngine.Builtin;

/// <summary>What the skybox shaders share: the rotation about Y, the clip position at the far plane, the tint.</summary>
public static class SkyboxCommon
{
    /// <summary>The tint is doubled: a tint of 0.5 leaves the texture as it is (the reference's gamma space tint; the
    /// engine draws in gamma space).</summary>
    public const float ColorSpaceDouble = 2.0f;

    /// <summary>A position turned about Y by degrees.</summary>
    public static float3 RotateAroundYInDegrees(float3 position, float degrees)
    {
        float alpha = degrees * 3.14159265f / 180.0f;
        float s = Sin(alpha), c = Cos(alpha);
        return new float3(c * position.x - s * position.z, position.y, s * position.x + c * position.z);
    }

    /// <summary>The clip position of a point of the cube around the camera, at the far plane (what the skybox list
    /// asks of its shaders).</summary>
    public static float4 FarClip(float3 position)
    {
        float4 clip = ShaderVariables.ObjectToClip(position);
        return new float4(clip.x, clip.y, clip.w, clip.w);
    }

    public static float4 Shade(float3 texel, float4 tint, float exposure) => new float4(texel * tint.xyz * ColorSpaceDouble * exposure, 1.0f);
}

/// <summary>Skybox/Cubemap: a cube texture around the camera, tinted, exposed and turned about Y.</summary>
[Shader("Skybox/Cubemap")]
[Tag("Queue", "Background")]
[Tag("RenderType", "Background")]
[Tag("PreviewType", "Skybox")]
public sealed class SkyboxCubemap : ShaderDefinition
{
    [Property("Tint Color"), Default(0.5f, 0.5f, 0.5f, 0.5f)] public float4 _Tint;
    [Property("Exposure"), Range(0.0f, 8.0f), Default(1.0f)] public float _Exposure;
    [Property("Rotation"), Range(0.0f, 360.0f), Default(0.0f)] public float _Rotation;
    [Property("Cubemap   (HDR)"), NoScaleOffset, DefaultTexture("grey")] public TextureCube<float4> _Tex;

    public struct MaterialData
    {
        public float4 _Tint;
        public float _Exposure;
        public float _Rotation;
    }

    public struct Varyings
    {
        [Position] public float4 position;
        [Location(0)] public float3 texcoord;
    }

    [Pass("Skybox", Cull = CullMode.Off, ZWrite = false)]
    public static class Skybox
    {
        [Uniform(0)] public static MaterialData material;
        [Texture(0)] public static TextureCube<float4> _Tex;
        [Sampler(0)] public static SamplerState sampler_Tex;

        /// <summary>The cube turned by the rotation; the direction is the unturned position (the sky turns the other
        /// way, as the reference's).</summary>
        [VertexShader]
        public static Varyings Vertex([Location(VertexInputs.Position)] float3 position)
        {
            Varyings o;
            o.position = SkyboxCommon.FarClip(SkyboxCommon.RotateAroundYInDegrees(position, material._Rotation));
            o.texcoord = position;
            return o;
        }

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input) =>
            SkyboxCommon.Shade(_Tex.Sample(sampler_Tex, input.texcoord).xyz, material._Tint, material._Exposure);
    }
}

/// <summary>
/// Skybox/6 Sided: six textures, one per side of the cube around the camera, tinted, exposed and turned about Y. The
/// side is the one of the direction's largest axis; the textures meet edge to edge (the up texture's bottom edge and the
/// down texture's top edge touch the front texture).
/// </summary>
[Shader("Skybox/6 Sided")]
[Tag("Queue", "Background")]
[Tag("RenderType", "Background")]
[Tag("PreviewType", "Skybox")]
public sealed class SkyboxSixSided : ShaderDefinition
{
    [Property("Tint Color"), Default(0.5f, 0.5f, 0.5f, 0.5f)] public float4 _Tint;
    [Property("Exposure"), Range(0.0f, 8.0f), Default(1.0f)] public float _Exposure;
    [Property("Rotation"), Range(0.0f, 360.0f), Default(0.0f)] public float _Rotation;
    [Property("Front [+Z]   (HDR)"), NoScaleOffset, DefaultTexture("grey")] public Texture2D<float4> _FrontTex;
    [Property("Back [-Z]   (HDR)"), NoScaleOffset, DefaultTexture("grey")] public Texture2D<float4> _BackTex;
    [Property("Left [+X]   (HDR)"), NoScaleOffset, DefaultTexture("grey")] public Texture2D<float4> _LeftTex;
    [Property("Right [-X]   (HDR)"), NoScaleOffset, DefaultTexture("grey")] public Texture2D<float4> _RightTex;
    [Property("Up [+Y]   (HDR)"), NoScaleOffset, DefaultTexture("grey")] public Texture2D<float4> _UpTex;
    [Property("Down [-Y]   (HDR)"), NoScaleOffset, DefaultTexture("grey")] public Texture2D<float4> _DownTex;

    public struct MaterialData
    {
        public float4 _Tint;
        public float _Exposure;
        public float _Rotation;
    }

    public struct Varyings
    {
        [Position] public float4 position;
        [Location(0)] public float3 texcoord;
    }

    [Pass("Skybox", Cull = CullMode.Off, ZWrite = false)]
    public static class Skybox
    {
        [Uniform(0)] public static MaterialData material;
        [Texture(0)] public static Texture2D<float4> _FrontTex;
        [Sampler(0)] public static SamplerState sampler_FrontTex;
        [Texture(1)] public static Texture2D<float4> _BackTex;
        [Sampler(1)] public static SamplerState sampler_BackTex;
        [Texture(2)] public static Texture2D<float4> _LeftTex;
        [Sampler(2)] public static SamplerState sampler_LeftTex;
        [Texture(3)] public static Texture2D<float4> _RightTex;
        [Sampler(3)] public static SamplerState sampler_RightTex;
        [Texture(4)] public static Texture2D<float4> _UpTex;
        [Sampler(4)] public static SamplerState sampler_UpTex;
        [Texture(5)] public static Texture2D<float4> _DownTex;
        [Sampler(5)] public static SamplerState sampler_DownTex;

        [VertexShader]
        public static Varyings Vertex([Location(VertexInputs.Position)] float3 position)
        {
            Varyings o;
            o.position = SkyboxCommon.FarClip(SkyboxCommon.RotateAroundYInDegrees(position, material._Rotation));
            o.texcoord = position;
            return o;
        }

        /// <summary>The side of the direction's largest axis, its texture's coordinate where the direction meets the
        /// side (u to the right and v up as seen from inside).</summary>
        static float3 SideColor(float3 d)
        {
            float3 a = Abs(d);
            if (a.x >= a.y && a.x >= a.z)
            {
                if (d.x > 0.0f)
                    return _LeftTex.Sample(sampler_LeftTex, new float2(-d.z, d.y) / a.x * 0.5f + new float2(0.5f)).xyz;
                return _RightTex.Sample(sampler_RightTex, new float2(d.z, d.y) / a.x * 0.5f + new float2(0.5f)).xyz;
            }
            if (a.y >= a.z)
            {
                if (d.y > 0.0f)
                    return _UpTex.Sample(sampler_UpTex, new float2(d.x, -d.z) / a.y * 0.5f + new float2(0.5f)).xyz;
                return _DownTex.Sample(sampler_DownTex, new float2(d.x, d.z) / a.y * 0.5f + new float2(0.5f)).xyz;
            }
            if (d.z > 0.0f)
                return _FrontTex.Sample(sampler_FrontTex, new float2(d.x, d.y) / a.z * 0.5f + new float2(0.5f)).xyz;
            return _BackTex.Sample(sampler_BackTex, new float2(-d.x, d.y) / a.z * 0.5f + new float2(0.5f)).xyz;
        }

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input) => SkyboxCommon.Shade(SideColor(input.texcoord), material._Tint, material._Exposure);
    }
}
