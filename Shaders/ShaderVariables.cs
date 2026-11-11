using HelixEngine.Shaders;

namespace HelixEngine.Builtin;

/// <summary>What the renderer sets once per camera.</summary>
public struct PerCamera
{
    public float4x4 helix_MatrixV;
    public float4x4 helix_MatrixP;
    public float4x4 helix_MatrixVP;
    public float4 helix_WorldSpaceCameraPos;
}

/// <summary>What the renderer sets once per drawn object.</summary>
public struct PerDraw
{
    public float4x4 helix_ObjectToWorld;
    public float4x4 helix_WorldToObject;
}

/// <summary>
/// The engine's shader variables. Their blocks sit at the top uniform slots (camera 12, draw 13; the dynamic keywords
/// take 11), so materials use slots 0 to 10.
/// </summary>
public static class ShaderVariables
{
    public const int CameraSlot = 12;
    public const int DrawSlot = 13;

    [Uniform(CameraSlot)] public static PerCamera camera;
    [Uniform(DrawSlot)] public static PerDraw draw;

    /// <summary>An object space position in clip space.</summary>
    public static float4 ObjectToClip(float3 position) =>
        ShaderMath.Mul(camera.helix_MatrixVP, ShaderMath.Mul(draw.helix_ObjectToWorld, new float4(position, 1.0f)));
}
