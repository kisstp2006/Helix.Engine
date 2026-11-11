using HelixEngine.Shaders;

namespace HelixEngine.Builtin;

/// <summary>What draws in place of a shader or variant that does not compile: plain magenta.</summary>
[Shader("Hidden/InternalErrorShader")]
public sealed class InternalErrorShader : ShaderDefinition
{
    public struct Varyings
    {
        [Position] public float4 position;
    }

    [Pass("Error")]
    public static class Error
    {
        [VertexShader]
        public static Varyings Vertex([Location(VertexInputs.Position)] float3 position)
        {
            Varyings o;
            o.position = ShaderVariables.ObjectToClip(position);
            return o;
        }

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(Varyings input) => new float4(1.0f, 0.0f, 1.0f, 1.0f);
    }
}
