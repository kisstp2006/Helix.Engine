#pragma once

#include <Helix/Engine/Texture/Texture.h>

// RenderTexture (class 84): a texture the GPU renders into. Its GPU textures are made when it is created (Create, or
// its first use) and go with Release; draws sample it as any texture (its resolved color, with mips when it has them).
// Temporary render textures are pooled by descriptor. Rows are kept bottom first as a Texture2D's are (the renderer
// flips rendering into textures).
namespace helix::engine
{
    namespace ClassId
    {
        inline constexpr i32 RenderTexture = 84;
    }

    HX_ENUM(Namespace = "HelixEngine")
    enum class RenderTextureFormat : i32
    {
        ARGB32 = 0,
        Depth = 1,
        ARGBHalf = 2,
        Shadowmap = 3,
        RGB565 = 4,
        ARGB4444 = 5,
        ARGB1555 = 6,
        Default = 7,
        ARGB2101010 = 8,
        DefaultHDR = 9,
        ARGB64 = 10,
        ARGBFloat = 11,
        RGFloat = 12,
        RGHalf = 13,
        RFloat = 14,
        RHalf = 15,
        R8 = 16,
        ARGBInt = 17,
        RGInt = 18,
        RInt = 19,
        BGRA32 = 20,
        RGB111110Float = 22,
        RG32 = 23,
        RGBAUShort = 24,
        RG16 = 25,
        BGRA10101010_XR = 26,
        BGR101010_XR = 27,
        R16 = 28,
    };

    HX_ENUM(Namespace = "HelixEngine")
    enum class RenderTextureReadWrite : i32
    {
        Default = 0,
        Linear = 1,
        sRGB = 2,
    };

    // Kept for tiled GPUs (every texture is stored here).
    HX_ENUM(Namespace = "HelixEngine", Attributes = "[System.Flags]")
    enum class RenderTextureMemoryless : i32
    {
        None = 0,
        Color = 1,
        Depth = 2,
        MSAA = 4,
    };

    HX_ENUM(Namespace = "HelixEngine")
    enum class VRTextureUsage : i32
    {
        None = 0,
        OneEye = 1,
        TwoEyes = 2,
        DeviceSpecific = 3,
    };

    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class ShadowSamplingMode : i32
    {
        CompareDepths = 0,
        RawDepth = 1,
        None = 2,
    };

    // The reference's GraphicsFormat in its numbering, for the formats the devices have (the others come with them).
    HX_ENUM(Namespace = "HelixEngine.Experimental.Rendering")
    enum class GraphicsFormat : i32
    {
        None = 0,
        R8_SRGB = 1,
        R8G8_SRGB = 2,
        R8G8B8_SRGB = 3,
        R8G8B8A8_SRGB = 4,
        R8_UNorm = 5,
        R8G8_UNorm = 6,
        R8G8B8_UNorm = 7,
        R8G8B8A8_UNorm = 8,
        R8_SNorm = 9,
        R8G8_SNorm = 10,
        R8G8B8_SNorm = 11,
        R8G8B8A8_SNorm = 12,
        R8_UInt = 13,
        R8G8_UInt = 14,
        R8G8B8_UInt = 15,
        R8G8B8A8_UInt = 16,
        R8_SInt = 17,
        R8G8_SInt = 18,
        R8G8B8_SInt = 19,
        R8G8B8A8_SInt = 20,
        R16_UNorm = 21,
        R16G16_UNorm = 22,
        R16G16B16_UNorm = 23,
        R16G16B16A16_UNorm = 24,
        R16_SNorm = 25,
        R16G16_SNorm = 26,
        R16G16B16_SNorm = 27,
        R16G16B16A16_SNorm = 28,
        R16_UInt = 29,
        R16G16_UInt = 30,
        R16G16B16_UInt = 31,
        R16G16B16A16_UInt = 32,
        R16_SInt = 33,
        R16G16_SInt = 34,
        R16G16B16_SInt = 35,
        R16G16B16A16_SInt = 36,
        R32_UInt = 37,
        R32G32_UInt = 38,
        R32G32B32_UInt = 39,
        R32G32B32A32_UInt = 40,
        R32_SInt = 41,
        R32G32_SInt = 42,
        R32G32B32_SInt = 43,
        R32G32B32A32_SInt = 44,
        R16_SFloat = 45,
        R16G16_SFloat = 46,
        R16G16B16_SFloat = 47,
        R16G16B16A16_SFloat = 48,
        R32_SFloat = 49,
        R32G32_SFloat = 50,
        R32G32B32_SFloat = 51,
        R32G32B32A32_SFloat = 52,
        B8G8R8_SRGB = 56,
        B8G8R8A8_SRGB = 57,
        B8G8R8_UNorm = 58,
        B8G8R8A8_UNorm = 59,
        B10G11R11_UFloatPack32 = 74,
        A2B10G10R10_UNormPack32 = 75,
        D16_UNorm = 90,
        D24_UNorm = 91,
        D24_UNorm_S8_UInt = 92,
        D32_SFloat = 93,
        D32_SFloat_S8_UInt = 94,
        S8_UInt = 95,
    };

    // The reference's GraphicsFormatUtility (the members render textures use).
    HX_CLASS(Namespace = "HelixEngine.Experimental.Rendering", Static)
    class GraphicsFormatUtility
    {
    public:
        // The format of a render texture format, linear or sRGB.
        HX_FUNCTION() static GraphicsFormat GetGraphicsFormat(RenderTextureFormat format, bool isSRGB);
        HX_FUNCTION() static RenderTextureFormat GetRenderTextureFormat(GraphicsFormat format);
        HX_FUNCTION() static bool IsSRGBFormat(GraphicsFormat format);
        HX_FUNCTION() static bool IsDepthFormat(GraphicsFormat format);
        HX_FUNCTION() static bool IsStencilFormat(GraphicsFormat format);
        // The depth format of a depth buffer's bits (0: none).
        HX_FUNCTION() static GraphicsFormat GetDepthStencilFormat(i32 minimumDepthBits);
        static i32 DepthBits(GraphicsFormat format);
    };

    // RenderTextureDescriptor: what a render texture is made of (C# has the reference's struct; this is its data).
    struct RenderTextureDescriptor
    {
        i32 width = 0;
        i32 height = 0;
        i32 msaaSamples = 1;
        i32 volumeDepth = 1;
        i32 mipCount = Texture::GenerateAllMips;
        GraphicsFormat graphicsFormat = GraphicsFormat::None;
        GraphicsFormat depthStencilFormat = GraphicsFormat::None;
        GraphicsFormat stencilFormat = GraphicsFormat::None;
        TextureDimension dimension = TextureDimension::Tex2D;
        ShadowSamplingMode shadowSamplingMode = ShadowSamplingMode::None;
        VRTextureUsage vrUsage = VRTextureUsage::None;
        RenderTextureMemoryless memoryless = RenderTextureMemoryless::None;
        bool sRGB = false;
        bool useMipMap = false;
        bool autoGenerateMips = true;
        bool enableRandomWrite = false;
        bool bindMS = false;
        bool useDynamicScale = false;

        RenderTextureDescriptor() = default;
        RenderTextureDescriptor(i32 w, i32 h, RenderTextureFormat format = RenderTextureFormat::Default, i32 depthBufferBits = 0, i32 mips = Texture::GenerateAllMips);
        RenderTextureDescriptor(i32 w, i32 h, GraphicsFormat format, i32 depthBufferBits = 0, i32 mips = Texture::GenerateAllMips);

        i32 depthBufferBits() const { return GraphicsFormatUtility::DepthBits(depthStencilFormat); }
        void SetDepthBufferBits(i32 bits) { depthStencilFormat = GraphicsFormatUtility::GetDepthStencilFormat(bits); }
        RenderTextureFormat colorFormat() const;
        void SetColorFormat(RenderTextureFormat format);
        bool operator==(const RenderTextureDescriptor& o) const;
    };

    // The descriptor as plain values for C# (internal).
    HX_STRUCT(Namespace = "HelixEngine.Internal", Internal)
    struct RenderTextureDescriptorData
    {
        HX_FIELD() i32 width = 0;
        HX_FIELD() i32 height = 0;
        HX_FIELD() i32 msaaSamples = 1;
        HX_FIELD() i32 volumeDepth = 1;
        HX_FIELD() i32 mipCount = Texture::GenerateAllMips;
        HX_FIELD() i32 graphicsFormat = 0;
        HX_FIELD() i32 depthStencilFormat = 0;
        HX_FIELD() i32 stencilFormat = 0;
        HX_FIELD() i32 dimension = 2;
        HX_FIELD() i32 shadowSamplingMode = 2;
        HX_FIELD() i32 vrUsage = 0;
        HX_FIELD() i32 memoryless = 0;
        // sRGB, useMipMap, autoGenerateMips, enableRandomWrite, bindMS, useDynamicScale from bit 0.
        HX_FIELD() i32 flags = 4;

        static RenderTextureDescriptorData From(const RenderTextureDescriptor& d);
        RenderTextureDescriptor ToDescriptor() const;
    };

    HX_CLASS(Namespace = "HelixEngine", Sealed)
    class RenderTexture : public Texture
    {
        HX_ENGINE_CLASS(RenderTexture, Texture, ClassId::RenderTexture, ClassNone)

    public:
        RenderTexture() = default;
        RenderTexture(const RenderTexture& other);
        ~RenderTexture() override;

        // new RenderTexture(width, height, depth[, format, readWrite]); new RenderTexture(descriptor); new RenderTexture(other).
        static RenderTexture* New(const RenderTextureDescriptor& descriptor);
        HX_FUNCTION(Constructor) static RenderTexture* New(i32 width, i32 height, i32 depth) { return New(width, height, depth, RenderTextureFormat::Default); }
        HX_FUNCTION(Constructor) static RenderTexture* New(i32 width, i32 height, i32 depth, RenderTextureFormat format)
        {
            return New(width, height, depth, format, RenderTextureReadWrite::Default);
        }
        HX_FUNCTION(Constructor) static RenderTexture* New(i32 width, i32 height, i32 depth, RenderTextureFormat format, RenderTextureReadWrite readWrite);
        HX_FUNCTION(Constructor) static RenderTexture* New(i32 width, i32 height, i32 depth, GraphicsFormat format);
        HX_FUNCTION(Constructor) static RenderTexture* New(const RenderTexture& other);

        const RenderTextureDescriptor& descriptor() const { return desc_; }
        void SetDescriptor(const RenderTextureDescriptor& descriptor);

        // The descriptor's values; changing one of a created texture is an error (Release it first).
        void SetWidth(i32 value);
        void SetHeight(i32 value);
        void SetDimension(TextureDimension value);
        TextureDimension dimension() const override { return desc_.dimension; }
        HX_PROPERTY() i32 depth() const { return desc_.depthBufferBits(); }
        void SetDepth(i32 value);
        HX_PROPERTY() RenderTextureFormat format() const { return desc_.colorFormat(); }
        void SetFormat(RenderTextureFormat value);
        HX_PROPERTY() GraphicsFormat graphicsFormat() const { return desc_.graphicsFormat; }
        void SetGraphicsFormat(GraphicsFormat value);
        HX_PROPERTY() GraphicsFormat depthStencilFormat() const { return desc_.depthStencilFormat; }
        void SetDepthStencilFormat(GraphicsFormat value);
        HX_PROPERTY() GraphicsFormat stencilFormat() const { return desc_.stencilFormat; }
        void SetStencilFormat(GraphicsFormat value);
        HX_PROPERTY() i32 antiAliasing() const { return desc_.msaaSamples; }
        void SetAntiAliasing(i32 value);
        HX_PROPERTY() bool useMipMap() const { return desc_.useMipMap; }
        void SetUseMipMap(bool value);
        HX_PROPERTY() bool autoGenerateMips() const { return desc_.autoGenerateMips; }
        void SetAutoGenerateMips(bool value);
        HX_PROPERTY(ReadOnly) bool sRGB() const { return desc_.sRGB; }
        HX_PROPERTY() i32 volumeDepth() const { return desc_.volumeDepth; }
        void SetVolumeDepth(i32 value);
        HX_PROPERTY() bool enableRandomWrite() const { return desc_.enableRandomWrite; }
        void SetEnableRandomWrite(bool value);
        HX_PROPERTY() bool bindTextureMS() const { return desc_.bindMS; }
        void SetBindTextureMS(bool value);
        HX_PROPERTY() bool useDynamicScale() const { return desc_.useDynamicScale; }
        void SetUseDynamicScale(bool value);
        HX_PROPERTY() RenderTextureMemoryless memorylessMode() const { return desc_.memoryless; }
        void SetMemorylessMode(RenderTextureMemoryless value);
        HX_PROPERTY() VRTextureUsage vrUsage() const { return desc_.vrUsage; }
        void SetVrUsage(VRTextureUsage value);
        HX_PROPERTY(ReadOnly) bool isPowerOfTwo() const;

        // Makes the GPU textures (true when they are there; it is also created by its first use).
        HX_FUNCTION() bool Create();
        // Frees the GPU textures (the object stays).
        HX_FUNCTION() void Release();
        HX_FUNCTION() bool IsCreated() const { return created_; }
        // Accepted: contents are kept or lost by the GPU as it likes.
        HX_FUNCTION() void DiscardContents() {}
        HX_FUNCTION() void DiscardContents(bool discardColor, bool discardDepth) { (void)discardColor, (void)discardDepth; }
        HX_FUNCTION() void MarkRestoreExpected() {}
        // The multisampled color into the sampled one (also done when it stops being rendered into).
        HX_FUNCTION() void ResolveAntiAliasedSurface();
        HX_FUNCTION() void ResolveAntiAliasedSurface(RenderTexture* target);
        // The mips from the first one (a texture with useMipMap and no autoGenerateMips).
        HX_FUNCTION() void GenerateMips();
        HX_FUNCTION() bool SupportsStencil() const { return GraphicsFormatUtility::IsStencilFormat(desc_.depthStencilFormat); }

        // A pooled texture of the descriptor (released ones are reused; unused ones go after some frames).
        static RenderTexture* GetTemporary(const RenderTextureDescriptor& descriptor);
        HX_FUNCTION() static RenderTexture* GetTemporary(i32 width, i32 height) { return GetTemporary(width, height, 0); }
        HX_FUNCTION() static RenderTexture* GetTemporary(i32 width, i32 height, i32 depthBuffer)
        {
            return GetTemporary(width, height, depthBuffer, RenderTextureFormat::Default);
        }
        HX_FUNCTION() static RenderTexture* GetTemporary(i32 width, i32 height, i32 depthBuffer, RenderTextureFormat format)
        {
            return GetTemporary(width, height, depthBuffer, format, RenderTextureReadWrite::Default, 1);
        }
        HX_FUNCTION() static RenderTexture* GetTemporary(i32 width, i32 height, i32 depthBuffer, RenderTextureFormat format, RenderTextureReadWrite readWrite,
                                                         i32 antiAliasing);
        HX_FUNCTION() static RenderTexture* GetTemporary(i32 width, i32 height, i32 depthBuffer, GraphicsFormat format);
        HX_FUNCTION() static void ReleaseTemporary(RenderTexture* temp);
        // The render texture Graphics.Blit, Graphics.SetRenderTarget and the like target (null: the screen).
        HX_PROPERTY() static RenderTexture* active();
        static void SetActive(RenderTexture* value);

        // ---------------------------------------------------------------------------------------------- native
        // Bumped when its GPU textures have to be made again.
        u32 GpuVersion() const { return gpuVersion_; }
        // Pooled temporaries not used for some frames go (the renderer calls it once a frame).
        static void ReleaseUnusedTemporaries(u32 frame);
        static u32 TemporaryCount();

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("m_Width", desc_.width);
            v.Field("m_Height", desc_.height);
            v.Field("m_AntiAliasing", desc_.msaaSamples);
            v.Field("m_MipCount", desc_.mipCount);
            i32 depthFormat = i32(desc_.depthStencilFormat);
            v.Field("m_DepthStencilFormat", depthFormat);
            desc_.depthStencilFormat = GraphicsFormat(depthFormat);
            i32 colorFormat = i32(desc_.graphicsFormat);
            v.Field("m_ColorFormat", colorFormat);
            desc_.graphicsFormat = GraphicsFormat(colorFormat);
            v.Field("m_MipMap", desc_.useMipMap);
            v.Field("m_GenerateMips", desc_.autoGenerateMips);
            v.Field("m_SRGB", desc_.sRGB);
            v.Field("m_UseDynamicScale", desc_.useDynamicScale);
            v.Field("m_BindMS", desc_.bindMS);
            i32 dimension = i32(desc_.dimension);
            v.Field("m_Dimension", dimension);
            desc_.dimension = TextureDimension(dimension);
            v.Field("m_VolumeDepth", desc_.volumeDepth);
            if constexpr (V::kReading)
                Changed();
        }

    private:
        // Width and height for Texture's getters; a new GPU version.
        void Changed();
        bool Editable(const char* what) const;

        RenderTextureDescriptor desc_;
        bool created_ = false;
        u32 gpuVersion_ = 0;
    };

    // What C# calls for the members its RenderTexture has in another shape (internal).
    HX_CLASS(Namespace = "HelixEngine.Internal", Static, Internal)
    class RenderTextureNative
    {
    public:
        HX_FUNCTION() static RenderTexture* Create(RenderTextureDescriptorData descriptor) { return RenderTexture::New(descriptor.ToDescriptor()); }
        HX_FUNCTION() static RenderTextureDescriptorData Descriptor(RenderTexture* texture)
        {
            return texture ? RenderTextureDescriptorData::From(texture->descriptor()) : RenderTextureDescriptorData();
        }
        HX_FUNCTION() static void SetDescriptor(RenderTexture* texture, RenderTextureDescriptorData descriptor)
        {
            if (texture)
                texture->SetDescriptor(descriptor.ToDescriptor());
        }
        HX_FUNCTION() static void SetWidth(RenderTexture* texture, i32 value)
        {
            if (texture)
                texture->SetWidth(value);
        }
        HX_FUNCTION() static void SetHeight(RenderTexture* texture, i32 value)
        {
            if (texture)
                texture->SetHeight(value);
        }
        HX_FUNCTION() static void SetDimension(RenderTexture* texture, TextureDimension value)
        {
            if (texture)
                texture->SetDimension(value);
        }
        HX_FUNCTION() static RenderTexture* GetTemporary(RenderTextureDescriptorData descriptor) { return RenderTexture::GetTemporary(descriptor.ToDescriptor()); }
    };
}
