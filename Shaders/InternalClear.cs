using HelixEngine.Shaders;

namespace HelixEngine.Builtin;

/// <summary>Clears a camera's viewport: a triangle over it writing a color and the far depth (a render pass clear takes the
/// whole target).</summary>
[Shader("Hidden/InternalClear")]
public sealed class InternalClear : ShaderDefinition
{
    public struct ClearData
    {
        public float4 color;
    }

    public struct Varyings
    {
        [Position] public float4 position;
    }

    /// <summary>Vertex 0, 1 and 2 at (-1, -1), (3, -1) and (-1, 3), at the far depth.</summary>
    public static Varyings Cover(uint id)
    {
        Varyings o;
        float x = (id & 1u) != 0u ? 3.0f : -1.0f;
        float y = (id & 2u) != 0u ? 3.0f : -1.0f;
        o.position = new float4(x, y, 1.0f, 1.0f);
        return o;
    }

    [Pass("Color", Cull = CullMode.Off, ZTest = CompareFunction.Always)]
    public static class ColorAndDepth
    {
        [Uniform(0)] public static ClearData clear;

        [VertexShader]
        public static Varyings Vertex([VertexId] uint id) => Cover(id);

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input) => clear.color;
    }

    [Pass("Depth", Cull = CullMode.Off, ZTest = CompareFunction.Always, ColorMask = ColorWriteMask.None)]
    public static class DepthOnly
    {
        [VertexShader]
        public static Varyings Vertex([VertexId] uint id) => Cover(id);

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input) => new float4(0.0f);
    }
}
