using HelixEngine.Builtin;
using HelixEngine.Shaders;
using static HelixEngine.Shaders.ShaderMath;

namespace HelixEngine.TestShaders;

public enum Quality
{
    Low,
    Medium,
    High,
}

[System.Runtime.CompilerServices.InlineArray(4)]
public struct Weights
{
    private float element;
}

/// <summary>Every kind of material property, keywords, an array set by name and a tag.</summary>
[Shader("Tests/Surface")]
[Tag("Queue", "Transparent+5")]
[Tag("RenderType", "Transparent")]
public sealed class Surface : ShaderDefinition
{
    [Property("Tint"), MainColor, Default(1.0f, 0.5f, 0.25f, 1.0f)] public float4 tint;
    [Property("Offset")] public float4 offset;
    [Property("Base Map"), MainTexture, DefaultTexture("white")] public Texture2D<float4> baseMap;
    [Property("Mask"), NoScaleOffset] public Texture2D<float4> mask;
    [Property("Smoothness"), Range(0.0f, 1.0f), Default(0.5f)] public float smoothness;
    [Property("Layers"), Default(2.0f)] public int layers;
    [Property("Transform")] public float4x4 transform;

    [Keyword] public static readonly bool Glow;
    [Keyword(ShaderFeature = true)] public static readonly Quality Detail;
    [Keyword(KeywordScope.Global)] public static readonly bool Fog;

    public struct MaterialData
    {
        public float4 tint;
        public float4 offset;
        public float4 baseMap_ST;
        public float smoothness;
        public int layers;
        public Weights weights;
        public float4x4 transform;
    }

    public struct Varyings
    {
        [Position] public float4 position;
        [Location(0)] public float2 uv;
    }

    [Pass("Forward", LightMode = "ForwardBase")]
    public static class Forward
    {
        [Uniform(0)] public static MaterialData material;
        [Texture(0)] public static Texture2D<float4> baseMap;
        [Texture(1)] public static Texture2D<float4> mask;
        [Sampler(0)] public static SamplerState baseSampler;
        [Sampler(1)] public static SamplerState maskSampler;

        [VertexShader]
        public static Varyings Vertex([Location(VertexInputs.Position)] float3 position, [Location(VertexInputs.TexCoord0)] float2 uv)
        {
            Varyings o;
            o.position = ShaderVariables.ObjectToClip(position);
            o.uv = uv * material.baseMap_ST.xy + material.baseMap_ST.zw;
            return o;
        }

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input)
        {
            float4 c = baseMap.Sample(baseSampler, input.uv) * material.tint * mask.Sample(maskSampler, input.uv).x;
            c.xyz += material.offset.xyz * material.weights[material.layers & 3] * material.smoothness;
            if (Glow)
                c.xyz *= 2.0f;
            if (Detail == Quality.High)
                c.x = Mul(material.transform, c).x;
            if (Fog)
                c.xyz *= 0.5f;
            return c;
        }
    }

    [Pass("Shadow", LightMode = "ShadowCaster")]
    public static class Shadow
    {
        [VertexShader]
        public static Varyings Vertex([Location(VertexInputs.Position)] float3 position) => new Varyings { position = ShaderVariables.ObjectToClip(position) };

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input) => new float4(0.0f);
    }
}

/// <summary>Few properties: switching a material to it keeps tint.</summary>
[Shader("Tests/Plain")]
public sealed class Plain : ShaderDefinition
{
    [Property("Tint"), Default(0.0f, 1.0f, 0.0f, 1.0f)] public float4 tint;
    [Property("Strength"), Default(3.0f)] public float strength;

    public struct MaterialData
    {
        public float4 tint;
        public float strength;
    }

    [Uniform(0)] public static MaterialData material;

    public struct Varyings
    {
        [Position] public float4 position;
    }

    [Pass("Forward")]
    public static class Forward
    {
        [VertexShader]
        public static Varyings Vertex([Location(VertexInputs.Position)] float3 position) => new Varyings { position = ShaderVariables.ObjectToClip(position) };

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input) => material.tint * material.strength;
    }
}

/// <summary>Reads itself recursively: no GPU runs it, so it must draw with the error shader.</summary>
[Shader("Tests/Broken")]
public sealed class Broken : ShaderDefinition
{
    public struct Varyings
    {
        [Position] public float4 position;
    }

    private static float Depth(float n) => n <= 0.0f ? 0.0f : Depth(n - 1.0f) + 1.0f;

    [Pass("Forward")]
    public static class Forward
    {
        [VertexShader]
        public static Varyings Vertex([Location(VertexInputs.Position)] float3 position) => new Varyings { position = ShaderVariables.ObjectToClip(position) };

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input) => new float4(Depth(input.position.x));
    }
}

/// <summary>One color.</summary>
[Shader("Tests/Unlit")]
public sealed class Unlit : ShaderDefinition
{
    [Property("Color"), MainColor, Default(1.0f, 1.0f, 1.0f, 1.0f)] public float4 color;

    public struct MaterialData
    {
        public float4 color;
    }

    public struct Varyings
    {
        [Position] public float4 position;
    }

    [Pass("Forward")]
    public static class Forward
    {
        [Uniform(0)] public static MaterialData material;

        [VertexShader]
        public static Varyings Vertex([Location(VertexInputs.Position)] float3 position) => new Varyings { position = ShaderVariables.ObjectToClip(position) };

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input) => material.color;
    }
}

/// <summary>A texture times a color, with tiling.</summary>
[Shader("Tests/Textured")]
public sealed class Textured : ShaderDefinition
{
    [Property("Base Map"), MainTexture, DefaultTexture("white")] public Texture2D<float4> baseMap;
    [Property("Color"), MainColor, Default(1.0f, 1.0f, 1.0f, 1.0f)] public float4 color;

    public struct MaterialData
    {
        public float4 color;
        public float4 baseMap_ST;
    }

    public struct Varyings
    {
        [Position] public float4 position;
        [Location(0)] public float2 uv;
    }

    [Pass("Forward")]
    public static class Forward
    {
        [Uniform(0)] public static MaterialData material;
        [Texture(0)] public static Texture2D<float4> baseMap;
        [Sampler(0)] public static SamplerState baseSampler;

        [VertexShader]
        public static Varyings Vertex([Location(VertexInputs.Position)] float3 position, [Location(VertexInputs.TexCoord0)] float2 uv)
        {
            Varyings o;
            o.position = ShaderVariables.ObjectToClip(position);
            o.uv = uv * material.baseMap_ST.xy + material.baseMap_ST.zw;
            return o;
        }

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input) => baseMap.Sample(baseSampler, input.uv) * material.color;
    }
}

/// <summary>Alpha blended, in the transparent queue, without depth writes.</summary>
[Shader("Tests/Transparent")]
[Tag("Queue", "Transparent")]
public sealed class Transparent : ShaderDefinition
{
    [Property("Color"), MainColor, Default(1.0f, 1.0f, 1.0f, 0.5f)] public float4 color;

    public struct MaterialData
    {
        public float4 color;
    }

    public struct Varyings
    {
        [Position] public float4 position;
    }

    [Pass("Forward", ZWrite = false)]
    [Blend(BlendFactor.SrcAlpha, BlendFactor.OneMinusSrcAlpha)]
    public static class Forward
    {
        [Uniform(0)] public static MaterialData material;

        [VertexShader]
        public static Varyings Vertex([Location(VertexInputs.Position)] float3 position) => new Varyings { position = ShaderVariables.ObjectToClip(position) };

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input) => material.color;
    }
}

/// <summary>Lambert lighting from the main light of the camera block, with the ambient light.</summary>
[Shader("Tests/Lambert")]
public sealed class Lambert : ShaderDefinition
{
    [Property("Color"), MainColor, Default(1.0f, 1.0f, 1.0f, 1.0f)] public float4 color;

    public struct MaterialData
    {
        public float4 color;
    }

    public struct Varyings
    {
        [Position] public float4 position;
        [Location(0)] public float3 normal;
    }

    [Pass("Forward", LightMode = "ForwardBase")]
    public static class Forward
    {
        [Uniform(0)] public static MaterialData material;

        [VertexShader]
        public static Varyings Vertex([Location(VertexInputs.Position)] float3 position, [Location(VertexInputs.Normal)] float3 normal)
        {
            Varyings o;
            o.position = ShaderVariables.ObjectToClip(position);
            o.normal = ShaderVariables.ObjectToWorldNormal(normal);
            return o;
        }

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input)
        {
            float3 n = Normalize(input.normal);
            float light = Saturate(Dot(n, ShaderVariables.camera.helix_MainLightDirection.xyz));
            float3 lit = material.color.xyz * (ShaderVariables.camera.helix_MainLightColor.xyz * light + ShaderVariables.camera.helix_AmbientColor.xyz);
            return new float4(lit, 1.0f);
        }
    }

    [Pass("ShadowCaster", LightMode = "ShadowCaster")]
    public static class ShadowCaster
    {
        [VertexShader]
        public static Varyings Vertex([Location(VertexInputs.Position)] float3 position) => new Varyings { position = ShaderVariables.ObjectToClip(position) };

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input) => new float4(0.0f, 1.0f, 0.0f, 1.0f);
    }
}
