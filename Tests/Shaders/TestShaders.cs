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

/// <summary>A color whose culling, depth writes and blending come from material properties (`Cull [_Cull]`).</summary>
[Shader("Tests/PropertyStates")]
public sealed class PropertyStates : ShaderDefinition
{
    [Property("Color"), MainColor, Default(1.0f, 1.0f, 1.0f, 1.0f)] public float4 color;
    [Property("Cull"), Default(2.0f)] public float _Cull;
    [Property("ZWrite"), Default(1.0f)] public float _ZWrite;
    [Property("Source Blend"), Default(1.0f)] public float _SrcBlend;
    [Property("Destination Blend"), Default(0.0f)] public float _DstBlend;

    public struct MaterialData
    {
        public float4 color;
    }

    public struct Varyings
    {
        [Position] public float4 position;
    }

    [Pass("Forward", CullProperty = "_Cull", ZWriteProperty = "_ZWrite")]
    [Blend(SourceProperty = "_SrcBlend", DestinationProperty = "_DstBlend")]
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

/// <summary>
/// The object's reflection probe and the ambient light: the probe cube toward -z scaled by its decode value, plus the
/// ambient probe's light from above (the globals unity_SHAr ... unity_SHC).
/// </summary>
[Shader("Tests/ProbeAndAmbient")]
public sealed class ProbeAndAmbient : ShaderDefinition
{
    [Keyword(KeywordScope.Global)] public static readonly bool INSTANCING_ON;

    public struct Ambient
    {
        public float4 unity_SHAr, unity_SHAg, unity_SHAb, unity_SHBr, unity_SHBg, unity_SHBb, unity_SHC;
    }

    public struct Varyings
    {
        [Position] public float4 position;
    }

    [Pass("Forward")]
    public static class Forward
    {
        [Uniform(0)] public static Ambient ambient;
        [Texture(0)] public static TextureCube<float4> unity_SpecCube0;
        [Sampler(0)] public static SamplerState cubeSampler;

        [VertexShader]
        public static Varyings Vertex([Location(VertexInputs.Position)] float3 position) => new Varyings { position = ShaderVariables.ObjectToClip(position) };

        static float SH(float4 a, float4 b, float c, float3 n)
        {
            float4 vB = new float4(n.x * n.y, n.y * n.z, n.z * n.z, n.z * n.x);
            return Dot(a, new float4(n, 1.0f)) + Dot(b, vB) + c * (n.x * n.x - n.y * n.y);
        }

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input)
        {
            float3 up = new float3(0.0f, 1.0f, 0.0f);
            float3 sky = new float3(SH(ambient.unity_SHAr, ambient.unity_SHBr, ambient.unity_SHC.x, up), SH(ambient.unity_SHAg, ambient.unity_SHBg, ambient.unity_SHC.y, up),
                                    SH(ambient.unity_SHAb, ambient.unity_SHBb, ambient.unity_SHC.z, up));
            float3 reflection = unity_SpecCube0.SampleLevel(cubeSampler, new float3(0.0f, 0.0f, -1.0f), 0.0f).xyz * ShaderVariables.SpecCube0HDR().x;
            return new float4(reflection + sky, 1.0f);
        }
    }
}

/// <summary>Tests/Unlit that takes part in instancing (it declares the INSTANCING_ON keyword).</summary>
[Shader("Tests/InstancedUnlit")]
public sealed class InstancedUnlit : ShaderDefinition
{
    [Property("Color"), MainColor, Default(1.0f, 1.0f, 1.0f, 1.0f)] public float4 color;
    [Keyword(KeywordScope.Global)] public static readonly bool INSTANCING_ON;

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

/// <summary>The object's lights as a color: red their count, green and blue the indices of the first two (each eighths).
/// It takes part in instancing.</summary>
[Shader("Tests/ObjectLights")]
public sealed class ObjectLights : ShaderDefinition
{
    [Keyword(KeywordScope.Global)] public static readonly bool INSTANCING_ON;

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
        public static float4 Fragment(Varyings input)
        {
            int count = ShaderVariables.ObjectLightCount();
            float first = count > 0 ? ShaderVariables.ObjectLightIndex(0) : 0;
            float second = count > 1 ? ShaderVariables.ObjectLightIndex(1) : 0;
            return new float4(count / 8.0f, first / 8.0f, second / 8.0f, 1.0f);
        }
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

/// <summary>A cube texture seen in six directions: the quad in three columns and two rows, +X, -X, +Y in the bottom row, -Y,
/// +Z, -Z in the top one.</summary>
[Shader("Tests/CubeFaces")]
public sealed class CubeFaces : ShaderDefinition
{
    [Property("Cube"), DefaultTexture("white")] public TextureCube<float4> cube;

    public struct Varyings
    {
        [Position] public float4 position;
        [Location(0)] public float2 uv;
    }

    [Pass("Forward")]
    public static class Forward
    {
        [Texture(0)] public static TextureCube<float4> cube;
        [Sampler(0)] public static SamplerState cubeSampler;

        [VertexShader]
        public static Varyings Vertex([Location(VertexInputs.Position)] float3 position, [Location(VertexInputs.TexCoord0)] float2 uv)
        {
            Varyings o;
            o.position = ShaderVariables.ObjectToClip(position);
            o.uv = uv;
            return o;
        }

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input)
        {
            float column = Floor(input.uv.x * 3.0f);
            float face = column + (input.uv.y > 0.5f ? 3.0f : 0.0f);
            float3 direction = face < 0.5f ? new float3(1, 0, 0)
                             : face < 1.5f ? new float3(-1, 0, 0)
                             : face < 2.5f ? new float3(0, 1, 0)
                             : face < 3.5f ? new float3(0, -1, 0)
                             : face < 4.5f ? new float3(0, 0, 1)
                                           : new float3(0, 0, -1);
            return cube.Sample(cubeSampler, direction);
        }
    }
}

/// <summary>A shadow map read with a comparison sampler: white where the reference depth is not behind the stored one.</summary>
[Shader("Tests/ShadowCompare")]
public sealed class ShadowCompare : ShaderDefinition
{
    [Property("Shadow Map"), DefaultTexture("white")] public Texture2D<float> shadowMap;
    [Property("Reference"), Default(0.5f)] public float reference;

    public struct MaterialData
    {
        public float reference;
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
        [Texture(0)] public static Texture2D<float> shadowMap;
        [Sampler(0)] public static SamplerComparisonState shadowSampler;

        [VertexShader]
        public static Varyings Vertex([Location(VertexInputs.Position)] float3 position, [Location(VertexInputs.TexCoord0)] float2 uv)
        {
            Varyings o;
            o.position = ShaderVariables.ObjectToClip(position);
            o.uv = uv;
            return o;
        }

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input)
        {
            float lit = shadowMap.SampleCmpLevelZero(shadowSampler, input.uv, material.reference);
            return new float4(lit, lit, lit, 1.0f);
        }
    }
}

/// <summary>A skybox of a cube texture: the cube's object positions are the directions, drawn at the far plane.</summary>
[Shader("Tests/SkyboxCube")]
public sealed class SkyboxCube : ShaderDefinition
{
    [Property("Cube"), DefaultTexture("white")] public TextureCube<float4> cube;

    public struct Varyings
    {
        [Position] public float4 position;
        [Location(0)] public float3 direction;
    }

    [Pass("Forward", ZWrite = false, Cull = CullMode.Off)]
    public static class Forward
    {
        [Texture(0)] public static TextureCube<float4> cube;
        [Sampler(0)] public static SamplerState cubeSampler;

        [VertexShader]
        public static Varyings Vertex([Location(VertexInputs.Position)] float3 position)
        {
            Varyings o;
            float4 clip = ShaderVariables.ObjectToClip(position);
            o.position = new float4(clip.x, clip.y, clip.w, clip.w);
            o.direction = position;
            return o;
        }

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input) => cube.Sample(cubeSampler, input.direction);
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

/// <summary>The camera block's projection params as a color: red when the target is drawn flipped, green when not, blue the near plane.</summary>
[Shader("Tests/ProjectionParams")]
public sealed class ProjectionParams : ShaderDefinition
{
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
        public static float4 Fragment(Varyings input)
        {
            float4 p = ShaderVariables.camera.helix_ProjectionParams;
            return new float4(p.x < 0.0f ? 1.0f : 0.0f, p.x > 0.0f ? 1.0f : 0.0f, p.y, 1.0f);
        }
    }
}

/// <summary>A block and a texture without properties: their values come by name from property blocks and globals.</summary>
[Shader("Tests/NamedValues")]
public sealed class NamedValues : ShaderDefinition
{
    public struct Named
    {
        public float4 namedColor;
    }

    public struct Varyings
    {
        [Position] public float4 position;
    }

    [Pass("Forward")]
    public static class Forward
    {
        [Uniform(0)] public static Named named;
        [Texture(0)] public static Texture2D<float4> namedTexture;
        [Sampler(0)] public static SamplerState namedSampler;

        [VertexShader]
        public static Varyings Vertex([Location(VertexInputs.Position)] float3 position) => new Varyings { position = ShaderVariables.ObjectToClip(position) };

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input) => named.namedColor * namedTexture.Sample(namedSampler, new float2(0.5f, 0.5f));
    }
}

/// <summary>Kernels the compute tests dispatch: values by name in a block, buffers in and out, a keyword, an image.</summary>
[ComputeDefinition("Tests/Fill")]
public sealed class Fill : ComputeDefinition
{
    [Keyword] public static readonly bool Doubled;

    public struct Params
    {
        public float scale;
        public int offset;
        public uint count;
        public float unused;
        public float4 bias;
    }

    [Uniform(0)] public static Params parameters;
    [Storage(0)] public static StructuredBuffer<float> input;
    [Storage(1)] public static RWStructuredBuffer<float> output;
    [Storage(2, Format = StorageFormat.RGBA8)] public static RWTexture2D<float4> image;

    [ComputeShader(64)]
    public static void Scale([DispatchThreadId] uint3 id)
    {
        if (id.x >= parameters.count)
            return;
        float v = input[(int)id.x] * parameters.scale + parameters.offset + parameters.bias.x;
        output[(int)id.x] = Doubled ? v * 2.0f : v;
    }

    [ComputeShader(8, 8)]
    public static void Paint([DispatchThreadId] uint3 id) => image[new int2((int)id.x, (int)id.y)] = parameters.bias;
}

/// <summary>The color of a global (a block member without a material property takes the global of its name).</summary>
[Shader("Tests/GlobalColor")]
public sealed class GlobalColor : ShaderDefinition
{
    public struct MaterialData
    {
        public float4 globalTint;
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
        public static float4 Fragment(Varyings input) => material.globalTint;
    }
}

/// <summary>A texture read through the device's bindless table at the index the engine writes into baseMap_Bindless (the
/// property's texture, the property block's over the material's, else its default).</summary>
[Shader("Tests/BindlessColor")]
public sealed class BindlessColor : ShaderDefinition
{
    [Property("Base Map"), DefaultTexture("white")] public Texture2D<float4> baseMap;

    public struct MaterialData
    {
        public uint baseMap_Bindless;
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
        public static float4 Fragment(Varyings input) => new BindlessTexture(material.baseMap_Bindless).Sample(new float2(0.5f, 0.5f));
    }
}

/// <summary>A draw that reads a graphics buffer set by name (a global or a property block's).</summary>
[Shader("Tests/BufferColor")]
public sealed class BufferColor : ShaderDefinition
{
    public struct Varyings
    {
        [Position] public float4 position;
    }

    [Pass("Forward")]
    public static class Forward
    {
        [Storage(0)] public static StructuredBuffer<float4> colors;

        [VertexShader]
        public static Varyings Vertex([Location(VertexInputs.Position)] float3 position) => new Varyings { position = ShaderVariables.ObjectToClip(position) };

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input) => colors[1];
    }
}
