using HelixEngine.Shaders;

namespace HelixEngine.Builtin;

/// <summary>Blit's default: the source texture (_MainTex, scaled and offset by _MainTex_ST) over the whole target.</summary>
[Shader("Hidden/BlitCopy")]
public sealed class BlitCopy : ShaderDefinition
{
    [Property("Source"), MainTexture, DefaultTexture("white")] public Texture2D<float4> _MainTex;

    public struct MaterialData
    {
        public float4 _MainTex_ST;
    }

    public struct Varyings
    {
        [Position] public float4 position;
        [Location(0)] public float2 uv;
    }

    [Pass("Copy", Cull = CullMode.Off, ZTest = CompareFunction.Always, ZWrite = false)]
    public static class Copy
    {
        [Uniform(0)] public static MaterialData material;
        [Texture(0)] public static Texture2D<float4> _MainTex;
        [Sampler(0)] public static SamplerState mainSampler;

        /// <summary>Vertex 0, 1 and 2 at (-1, -1), (3, -1) and (-1, 3) through the camera block's projection (flipped for a texture target).</summary>
        [VertexShader]
        public static Varyings Vertex([VertexId] uint id)
        {
            Varyings o;
            float x = (id & 1u) != 0u ? 3.0f : -1.0f;
            float y = (id & 2u) != 0u ? 3.0f : -1.0f;
            o.position = ShaderMath.Mul(ShaderVariables.camera.helix_MatrixP, new float4(x, y, 0.5f, 1.0f));
            o.uv = new float2(x * 0.5f + 0.5f, y * 0.5f + 0.5f) * material._MainTex_ST.xy + material._MainTex_ST.zw;
            return o;
        }

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input) => _MainTex.Sample(mainSampler, input.uv);
    }
}
