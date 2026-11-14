using HelixEngine.Shaders;

namespace HelixEngine.Builtin;

/// <summary>What the renderer sets once per camera.</summary>
public struct PerCamera
{
    public float4x4 helix_MatrixV;
    public float4x4 helix_MatrixP;
    public float4x4 helix_MatrixVP;
    /// <summary>xyz: the camera's position in world space.</summary>
    public float4 helix_WorldSpaceCameraPos;
    /// <summary>xyz: the direction the brightest directional light shines from (toward the light), w: 1 when there is one.</summary>
    public float4 helix_MainLightDirection;
    /// <summary>rgb: its color times its intensity.</summary>
    public float4 helix_MainLightColor;
    /// <summary>rgb: the ambient light.</summary>
    public float4 helix_AmbientColor;
}

/// <summary>What the renderer sets once per drawn object.</summary>
public struct PerDraw
{
    public float4x4 helix_ObjectToWorld;
    public float4x4 helix_WorldToObject;
}

/// <summary>One instance's object data in the instance block.</summary>
public struct PerInstance
{
    public float4x4 helix_ObjectToWorld;
    public float4x4 helix_WorldToObject;
}

[System.Runtime.CompilerServices.InlineArray(ShaderVariables.MaxInstances)]
public struct PerInstanceArray
{
    private PerInstance element;
}

/// <summary>What the renderer sets per instanced draw: the object data of each instance (the draw slot's block then).</summary>
public struct PerInstances
{
    public PerInstanceArray helix_Instances;
}

/// <summary>
/// The engine's shader variables. Their blocks take uniform slots 9 (camera) and 10 (draw); the dynamic keywords take 11,
/// so materials use slots 0 to 8 (12 blocks per stage is what every graphics API the engine runs on guarantees).
/// </summary>
public static class ShaderVariables
{
    public const int CameraSlot = 9;
    public const int DrawSlot = 10;
    /// <summary>The most instances of one instanced draw.</summary>
    public const int MaxInstances = 128;

    [Uniform(CameraSlot)] public static PerCamera camera;
    [Uniform(DrawSlot)] public static PerDraw draw;
    [Uniform(DrawSlot)] public static PerInstances instances;

    /// <summary>
    /// On in the variants the renderer draws instanced. A shader takes part by declaring this keyword in its definition;
    /// its per object data is then read in the vertex stage only.
    /// </summary>
    [Keyword(KeywordScope.Global)] public static readonly bool INSTANCING_ON;

    /// <summary>The object to world matrix of the object (of the instance in instanced variants).</summary>
    public static float4x4 ObjectToWorldMatrix() =>
        INSTANCING_ON ? instances.helix_Instances[(int)ShaderMath.InstanceId()].helix_ObjectToWorld : draw.helix_ObjectToWorld;

    /// <summary>An object space position in clip space.</summary>
    public static float4 ObjectToClip(float3 position) =>
        ShaderMath.Mul(camera.helix_MatrixVP, ShaderMath.Mul(ObjectToWorldMatrix(), new float4(position, 1.0f)));

    /// <summary>An object space position in world space.</summary>
    public static float3 ObjectToWorld(float3 position) => ShaderMath.Mul(ObjectToWorldMatrix(), new float4(position, 1.0f)).xyz;

    /// <summary>An object space normal in world space (uniform scale; the renderer passes the inverse for others).</summary>
    public static float3 ObjectToWorldNormal(float3 normal) => ShaderMath.Normalize(ShaderMath.Mul(ObjectToWorldMatrix(), new float4(normal, 0.0f)).xyz);
}

/// <summary>
/// The vertex input location of each mesh attribute: [Location(n)] reads attribute n of the mesh. Attributes a mesh does
/// not have read zero.
/// </summary>
public static class VertexInputs
{
    public const int Position = 0;
    public const int Normal = 1;
    public const int Tangent = 2;
    public const int Color = 3;
    public const int TexCoord0 = 4;
    public const int TexCoord1 = 5;
    public const int TexCoord2 = 6;
    public const int TexCoord3 = 7;
    public const int TexCoord4 = 8;
    public const int TexCoord5 = 9;
    public const int TexCoord6 = 10;
    public const int TexCoord7 = 11;
    public const int BlendWeight = 12;
    public const int BlendIndices = 13;
}
