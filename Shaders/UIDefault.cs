using HelixEngine.Shaders;
using static HelixEngine.Shaders.ShaderMath;

namespace HelixEngine.Builtin;

/// <summary>What UI/Default and UI/DefaultETC1 share: the material's values and the canvas's per batch ones.</summary>
public struct UIMaterialData
{
    public float4 _Color;
    public float4 _MainTex_ST;
    /// <summary>The batch's clip rectangle in the root canvas's space (x min, y min, x max, y max; far away without one).</summary>
    public float4 _ClipRect;
    /// <summary>Added to the texture's sample: (1, 1, 1, 0) for alpha only textures (a font's).</summary>
    public float4 _TextureSampleAdd;
    public float _UIMaskSoftnessX;
    public float _UIMaskSoftnessY;
    /// <summary>1 where the target holds linear colors: the vertex colors (gamma) are made linear.</summary>
    public float _UIVertexToLinear;
    /// <summary>1 where the target holds gamma colors and the texture is sRGB (sampled linear): the sample goes back to gamma.</summary>
    public float _UISampleToGamma;
}

public struct UIVaryings
{
    [Position] public float4 position;
    [Location(0)] public float4 color;
    [Location(1)] public float2 uv;
    /// <summary>The point in the root canvas's space (the batch's clip is there).</summary>
    [Location(2)] public float2 canvasPosition;
}

/// <summary>The canvas UI's shading: tint, the clip rectangle with its softness, premultiplied output.</summary>
public static class UIShading
{
    static float ToLinear(float c) => c <= 0.04045f ? c / 12.92f : Pow((c + 0.055f) / 1.055f, 2.4f);

    static float ToGamma(float c) => c <= 0.0031308f ? c * 12.92f : 1.055f * Pow(c, 1.0f / 2.4f) - 0.055f;

    public static UIVaryings Vertex(UIMaterialData m, float3 position, float4 color, float4 uv)
    {
        UIVaryings o;
        o.position = ShaderVariables.ObjectToClip(position);
        float4 c = color;
        if (m._UIVertexToLinear > 0.5f)
            c = new float4(ToLinear(c.x), ToLinear(c.y), ToLinear(c.z), c.w);
        o.color = c * m._Color;
        o.uv = uv.xy * m._MainTex_ST.xy + m._MainTex_ST.zw;
        o.canvasPosition = position.xy;
        return o;
    }

    /// <summary>How much of the point the clip keeps on one axis: its distance inside the edges over the softness (a hard edge without one).</summary>
    static float Keep(float p, float lo, float hi, float softness)
    {
        float inside = Min(p - lo, hi - p);
        return softness > 0.0f ? Saturate(inside / softness) : (inside >= 0.0f ? 1.0f : 0.0f);
    }

    public static float4 Fragment(UIMaterialData m, UIVaryings input, float4 texel, bool alphaClip)
    {
        float4 t = texel + m._TextureSampleAdd;
        if (m._UISampleToGamma > 0.5f)
            t = new float4(ToGamma(t.x), ToGamma(t.y), ToGamma(t.z), t.w);
        float4 color = input.color * t;
        float4 r = m._ClipRect;
        float a = color.w * Keep(input.canvasPosition.x, r.x, r.z, m._UIMaskSoftnessX) * Keep(input.canvasPosition.y, r.y, r.w, m._UIMaskSoftnessY);
        if (alphaClip && a < 0.001f)
            Discard();
        return new float4(color.x * a, color.y * a, color.z * a, a);
    }
}

/// <summary>
/// UI/Default: the canvas UI's shader. The texture tinted by the vertex color and _Color, clipped to the batch's
/// rectangle (softened at its edges), blended premultiplied without depth writes; the depth test (helix_GUIZTestMode)
/// and the stencil (masks) and color mask come from the material's properties. HELIX_UI_ALPHACLIP drops transparent
/// fragments (for masks that write the stencil).
/// </summary>
[Shader("UI/Default")]
[Tag("Queue", "Transparent")]
[Tag("IgnoreProjector", "True")]
[Tag("RenderType", "Transparent")]
[Tag("PreviewType", "Plane")]
[Tag("CanUseSpriteAtlas", "True")]
public sealed class UIDefault : ShaderDefinition
{
    [Property("Sprite Texture"), MainTexture, DefaultTexture("white"), PerRendererData] public Texture2D<float4> _MainTex;
    [Property("Tint"), MainColor, Default(1.0f, 1.0f, 1.0f, 1.0f)] public float4 _Color;
    [Property("Stencil Comparison"), Default(8.0f)] public float _StencilComp;
    [Property("Stencil ID"), Default(0.0f)] public float _Stencil;
    [Property("Stencil Operation"), Default(0.0f)] public float _StencilOp;
    [Property("Stencil Write Mask"), Default(255.0f)] public float _StencilWriteMask;
    [Property("Stencil Read Mask"), Default(255.0f)] public float _StencilReadMask;
    [Property("Color Mask"), Default(15.0f)] public float _ColorMask;
    [Property("Use Alpha Clip"), Toggle("HELIX_UI_ALPHACLIP")] public float _UseUIAlphaClip;
    // The depth test: less or equal (an overlay clears the depth before its canvases).
    [Property("Depth Test"), HideInInspector, Default(4.0f)] public float helix_GUIZTestMode;

    [Keyword(KeywordScope.Local)] public static readonly bool HELIX_UI_ALPHACLIP;

    [Pass("Default", Cull = CullMode.Off, ZWrite = false, ZTestProperty = "helix_GUIZTestMode", ColorMaskProperty = "_ColorMask")]
    [Blend(BlendFactor.One, BlendFactor.OneMinusSrcAlpha)]
    [Stencil(ReferenceProperty = "_Stencil", ReadMaskProperty = "_StencilReadMask", WriteMaskProperty = "_StencilWriteMask", CompareProperty = "_StencilComp",
             PassProperty = "_StencilOp")]
    public static class Default
    {
        [Uniform(0)] public static UIMaterialData material;
        [Texture(0)] public static Texture2D<float4> _MainTex;
        [Sampler(0)] public static SamplerState sampler_MainTex;

        [VertexShader]
        public static UIVaryings Vertex([Location(VertexInputs.Position)] float3 position, [Location(VertexInputs.Color)] float4 color,
                                        [Location(VertexInputs.TexCoord0)] float4 uv) => UIShading.Vertex(material, position, color, uv);

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(UIVaryings input) =>
            UIShading.Fragment(material, input, _MainTex.Sample(sampler_MainTex, input.uv), HELIX_UI_ALPHACLIP);
    }
}

/// <summary>UI/DefaultETC1: UI/Default with the alpha from a second texture's red (_AlphaTex), for textures stored without alpha.</summary>
[Shader("UI/DefaultETC1")]
[Tag("Queue", "Transparent")]
[Tag("IgnoreProjector", "True")]
[Tag("RenderType", "Transparent")]
[Tag("PreviewType", "Plane")]
[Tag("CanUseSpriteAtlas", "True")]
public sealed class UIDefaultETC1 : ShaderDefinition
{
    [Property("Sprite Texture"), MainTexture, DefaultTexture("white"), PerRendererData] public Texture2D<float4> _MainTex;
    [Property("Alpha Texture"), DefaultTexture("white"), PerRendererData] public Texture2D<float4> _AlphaTex;
    [Property("Tint"), MainColor, Default(1.0f, 1.0f, 1.0f, 1.0f)] public float4 _Color;
    [Property("Stencil Comparison"), Default(8.0f)] public float _StencilComp;
    [Property("Stencil ID"), Default(0.0f)] public float _Stencil;
    [Property("Stencil Operation"), Default(0.0f)] public float _StencilOp;
    [Property("Stencil Write Mask"), Default(255.0f)] public float _StencilWriteMask;
    [Property("Stencil Read Mask"), Default(255.0f)] public float _StencilReadMask;
    [Property("Color Mask"), Default(15.0f)] public float _ColorMask;
    [Property("Use Alpha Clip"), Toggle("HELIX_UI_ALPHACLIP")] public float _UseUIAlphaClip;
    [Property("Depth Test"), HideInInspector, Default(4.0f)] public float helix_GUIZTestMode;

    [Keyword(KeywordScope.Local)] public static readonly bool HELIX_UI_ALPHACLIP;

    [Pass("Default", Cull = CullMode.Off, ZWrite = false, ZTestProperty = "helix_GUIZTestMode", ColorMaskProperty = "_ColorMask")]
    [Blend(BlendFactor.One, BlendFactor.OneMinusSrcAlpha)]
    [Stencil(ReferenceProperty = "_Stencil", ReadMaskProperty = "_StencilReadMask", WriteMaskProperty = "_StencilWriteMask", CompareProperty = "_StencilComp",
             PassProperty = "_StencilOp")]
    public static class Default
    {
        [Uniform(0)] public static UIMaterialData material;
        [Texture(0)] public static Texture2D<float4> _MainTex;
        [Texture(1)] public static Texture2D<float4> _AlphaTex;
        [Sampler(0)] public static SamplerState sampler_MainTex;

        [VertexShader]
        public static UIVaryings Vertex([Location(VertexInputs.Position)] float3 position, [Location(VertexInputs.Color)] float4 color,
                                        [Location(VertexInputs.TexCoord0)] float4 uv) => UIShading.Vertex(material, position, color, uv);

        [FragmentShader]
        [return: Target(0)]
        public static float4 Fragment(UIVaryings input)
        {
            float4 color = _MainTex.Sample(sampler_MainTex, input.uv);
            float4 texel = new float4(color.x, color.y, color.z, _AlphaTex.Sample(sampler_MainTex, input.uv).x);
            return UIShading.Fragment(material, input, texel, HELIX_UI_ALPHACLIP);
        }
    }
}
