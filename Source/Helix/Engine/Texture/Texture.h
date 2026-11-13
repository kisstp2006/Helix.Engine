#pragma once

#include <Helix/Engine/Object/Object.h>
#include <Helix/Image/Texture/TextureData.h>
#include <Helix/Math/Color.h>

// Texture and Texture2D: the pixels of every mip in one TextureData of Helix.Image (format, sizes and a table of the
// mips), with the sampling settings of the reference. The pixel API converts through Helix.Image; Apply hands the
// pixels to the renderer (a version counter it watches).
namespace helix::engine
{
    // The reference's numbers; formats without a layout in Helix.Image are refused by the constructors.
    HX_ENUM(Namespace = "HelixEngine")
    enum class TextureFormat : i32
    {
        Alpha8 = 1,
        ARGB4444 = 2,
        RGB24 = 3,
        RGBA32 = 4,
        ARGB32 = 5,
        RGB565 = 7,
        R16 = 9,
        DXT1 = 10,
        DXT5 = 12,
        RGBA4444 = 13,
        BGRA32 = 14,
        RHalf = 15,
        RGHalf = 16,
        RGBAHalf = 17,
        RFloat = 18,
        RGFloat = 19,
        RGBAFloat = 20,
        YUY2 = 21,
        RGB9e5Float = 22,
        BC6H = 24,
        BC7 = 25,
        BC4 = 26,
        BC5 = 27,
        DXT1Crunched = 28,
        DXT5Crunched = 29,
        ETC_RGB4 = 34,
        EAC_R = 41,
        EAC_R_SIGNED = 42,
        EAC_RG = 43,
        EAC_RG_SIGNED = 44,
        ETC2_RGB = 45,
        ETC2_RGBA1 = 46,
        ETC2_RGBA8 = 47,
        ASTC_4x4 = 48,
        ASTC_5x5 = 49,
        ASTC_6x6 = 50,
        ASTC_8x8 = 51,
        ASTC_10x10 = 52,
        ASTC_12x12 = 53,
        RG16 = 62,
        R8 = 63,
        ETC_RGB4Crunched = 64,
        ETC2_RGBA8Crunched = 65,
        ASTC_HDR_4x4 = 66,
        ASTC_HDR_5x5 = 67,
        ASTC_HDR_6x6 = 68,
        ASTC_HDR_8x8 = 69,
        ASTC_HDR_10x10 = 70,
        ASTC_HDR_12x12 = 71,
        RG32 = 72,
        RGB48 = 73,
        RGBA64 = 74,
        R8_SIGNED = 75,
        RG16_SIGNED = 76,
        RGB24_SIGNED = 77,
        RGBA32_SIGNED = 78,
        R16_SIGNED = 79,
        RG32_SIGNED = 80,
        RGB48_SIGNED = 81,
        RGBA64_SIGNED = 82,
    };

    HX_ENUM(Namespace = "HelixEngine")
    enum class FilterMode : i32
    {
        Point = 0,
        Bilinear = 1,
        Trilinear = 2,
    };

    HX_ENUM(Namespace = "HelixEngine")
    enum class TextureWrapMode : i32
    {
        Repeat = 0,
        Clamp = 1,
        Mirror = 2,
        MirrorOnce = 3,
    };

    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class TextureDimension : i32
    {
        Unknown = -1,
        None = 0,
        Any = 1,
        Tex2D = 2,
        Tex3D = 3,
        Cube = 4,
        Tex2DArray = 5,
        CubeArray = 6,
    };

    // The Helix.Image layout of a texture format, Unknown when it has none.
    image::PixelFormat ToPixelFormat(TextureFormat format);
    // The texture format of a layout; false when the reference names none for it.
    bool ToTextureFormat(image::PixelFormat format, TextureFormat& out);
    // Brings decoded pixels to a layout with a texture format (decompressing or widening to RGBA as needed).
    bool ToTextureLayout(image::TextureData& data, String& error);
    // Decoded pixels as a Texture2D holds them (any thread): layer 0, rows bottom first, a layout with a texture format;
    // block formats stay blocks when Helix.Image can write them.
    bool PrepareTextureData(image::TextureData& data, String& error);

    struct TextureSettings
    {
        static constexpr StringView kTypeName = "GLTextureSettings";
        FilterMode filterMode = FilterMode::Bilinear;
        i32 aniso = 1;
        f32 mipBias = 0;
        TextureWrapMode wrapU = TextureWrapMode::Repeat, wrapV = TextureWrapMode::Repeat, wrapW = TextureWrapMode::Repeat;

        template <class V>
        void Transfer(V& v)
        {
            v.Field("m_FilterMode", filterMode);
            v.Field("m_Aniso", aniso);
            v.Field("m_MipBias", mipBias);
            v.Field("m_WrapU", wrapU);
            v.Field("m_WrapV", wrapV);
            v.Field("m_WrapW", wrapW);
        }
    };

    HX_CLASS(Namespace = "HelixEngine")
    class Texture : public Object
    {
        HX_ENGINE_CLASS(Texture, Object, ClassId::Texture, ClassAbstract)

    public:
        static constexpr i32 GenerateAllMips = -1;

        // Not settable on this class (C#'s setters throw, as the reference's do).
        HX_PROPERTY(SetterThrows) i32 width() const { return i32(data_.width); }
        HX_PROPERTY(SetterThrows) i32 height() const { return i32(data_.height); }
        HX_PROPERTY(SetterThrows) virtual TextureDimension dimension() const { return TextureDimension::Tex2D; }
        HX_PROPERTY(ReadOnly) i32 mipmapCount() const { return i32(data_.mips); }
        HX_PROPERTY(ReadOnly) bool isReadable() const { return readable_; }
        // The pixels hold sRGB colors (sampling turns them linear).
        HX_PROPERTY(ReadOnly) bool isDataSRGB() const { return !linear_; }

        HX_PROPERTY() FilterMode filterMode() const { return settings_.filterMode; }
        void SetFilterMode(FilterMode value) { settings_.filterMode = value; }
        HX_PROPERTY() i32 anisoLevel() const { return settings_.aniso; }
        void SetAnisoLevel(i32 value) { settings_.aniso = value; }
        HX_PROPERTY() f32 mipMapBias() const { return settings_.mipBias; }
        void SetMipMapBias(f32 value) { settings_.mipBias = value; }
        HX_PROPERTY() TextureWrapMode wrapMode() const { return settings_.wrapU; }
        void SetWrapMode(TextureWrapMode value) { settings_.wrapU = settings_.wrapV = settings_.wrapW = value; }
        HX_PROPERTY() TextureWrapMode wrapModeU() const { return settings_.wrapU; }
        void SetWrapModeU(TextureWrapMode value) { settings_.wrapU = value; }
        HX_PROPERTY() TextureWrapMode wrapModeV() const { return settings_.wrapV; }
        void SetWrapModeV(TextureWrapMode value) { settings_.wrapV = value; }
        HX_PROPERTY() TextureWrapMode wrapModeW() const { return settings_.wrapW; }
        void SetWrapModeW(TextureWrapMode value) { settings_.wrapW = value; }
        const TextureSettings& settings() const { return settings_; }

        // Bumped by Apply and IncrementUpdateCount.
        HX_PROPERTY(ReadOnly) u32 updateCount() const { return updateCount_; }
        HX_FUNCTION() void IncrementUpdateCount() { updateCount_++, version_++; }

        // ---------------------------------------------------------------------------------------------- native
        // Every mip, layer and face of the texture (the CPU copy).
        const image::TextureData& Data() const { return data_; }
        // Bumped by every change the renderer has to upload.
        u32 Version() const { return version_; }
        // The CPU copy of a texture that is no longer readable (the renderer, once it holds the pixels).
        void ReleaseCpuData();

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("m_Name", name_);
            v.Field("m_IsReadable", readable_);
            i32 colorSpace = linear_ ? 0 : 1;
            v.Field("m_ColorSpace", colorSpace);
            linear_ = colorSpace == 0;
            v.Field("m_TextureSettings", settings_);
        }

    protected:
        Texture() = default;
        Texture(const Texture&) = default;

        image::TextureData data_;
        TextureSettings settings_;
        bool linear_ = false;
        bool readable_ = true;
        u32 updateCount_ = 0;
        u32 version_ = 0;
    };

    HX_CLASS(Namespace = "HelixEngine", Sealed)
    class Texture2D : public Texture
    {
        HX_ENGINE_CLASS(Texture2D, Texture, ClassId::Texture2D, ClassNone)

    public:
        Texture2D() = default;
        Texture2D(const Texture2D&) = default;

        // new Texture2D(width, height[, format, mipChain, linear, createUninitialized]); null with an error for formats
        // the engine cannot hold. Pixels start black unless createUninitialized.
        static Texture2D* Create(i32 width, i32 height, TextureFormat format = TextureFormat::RGBA32, bool mipChain = true, bool linear = false,
                                 bool createUninitialized = false);
        // With a mip count (-1 for the full chain).
        static Texture2D* Create(i32 width, i32 height, TextureFormat format, i32 mipCount, bool linear, bool createUninitialized = false);
        // The reference's constructors.
        HX_FUNCTION(Constructor) static Texture2D* New(i32 width, i32 height) { return Create(width, height); }
        HX_FUNCTION(Constructor) static Texture2D* New(i32 width, i32 height, TextureFormat textureFormat, bool mipChain) { return Create(width, height, textureFormat, mipChain); }
        HX_FUNCTION(Constructor) static Texture2D* New(i32 width, i32 height, TextureFormat textureFormat, bool mipChain, bool linear)
        {
            return Create(width, height, textureFormat, mipChain, linear);
        }
        HX_FUNCTION(Constructor) static Texture2D* New(i32 width, i32 height, TextureFormat textureFormat, bool mipChain, bool linear, bool createUninitialized)
        {
            return Create(width, height, textureFormat, mipChain, linear, createUninitialized);
        }
        HX_FUNCTION(Constructor) static Texture2D* New(i32 width, i32 height, TextureFormat textureFormat, i32 mipCount, bool linear)
        {
            return Create(width, height, textureFormat, mipCount, linear);
        }
        HX_FUNCTION(Constructor) static Texture2D* New(i32 width, i32 height, TextureFormat textureFormat, i32 mipCount, bool linear, bool createUninitialized)
        {
            return Create(width, height, textureFormat, mipCount, linear, createUninitialized);
        }
        // From decoded pixels (rows top first, as files hold them).
        static Texture2D* Create(image::TextureData&& data, bool linear);
        // From pixels PrepareTextureData made ready (the asset loaders, which prepare on a job).
        static Texture2D* CreatePrepared(image::TextureData&& data, bool linear);
        void SetSettings(const TextureSettings& settings) { settings_ = settings; }
        void SetReadable(bool readable) { readable_ = readable; }

        HX_PROPERTY(ReadOnly) TextureFormat format() const { return format_; }
        // New size and format; the pixels are lost (black).
        HX_FUNCTION() bool Reinitialize(i32 width, i32 height);
        HX_FUNCTION() bool Reinitialize(i32 width, i32 height, TextureFormat format, bool hasMipMap);

        // Out of range coordinates follow the wrap mode. Block compressed textures are decoded for reading.
        HX_FUNCTION() Color GetPixel(i32 x, i32 y, i32 mipLevel = 0) const;
        HX_FUNCTION() Color GetPixelBilinear(f32 u, f32 v, i32 mipLevel = 0) const;
        HX_FUNCTION() Array<Color> GetPixels(i32 mipLevel = 0) const;
        HX_FUNCTION() Array<Color> GetPixels(i32 x, i32 y, i32 blockWidth, i32 blockHeight, i32 mipLevel = 0) const;
        HX_FUNCTION() Array<Color32> GetPixels32(i32 mipLevel = 0) const;
        HX_FUNCTION(Void) bool SetPixel(i32 x, i32 y, Color color, i32 mipLevel = 0);
        HX_FUNCTION(Void) bool SetPixels(Span<const Color> colors, i32 mipLevel = 0);
        HX_FUNCTION(Void) bool SetPixels(i32 x, i32 y, i32 blockWidth, i32 blockHeight, Span<const Color> colors, i32 mipLevel = 0);
        HX_FUNCTION(Void) bool SetPixels32(Span<const Color32> colors, i32 mipLevel = 0);
        HX_FUNCTION(Void) bool SetPixels32(i32 x, i32 y, i32 blockWidth, i32 blockHeight, Span<const Color32> colors, i32 mipLevel = 0);
        // The bytes of one mip in the texture's format, as T.
        template <class T>
        Span<T> GetPixelData(i32 mipLevel) const
        {
            Span<u8> b = MipBytes(mipLevel);
            return Span<T>(reinterpret_cast<T*>(b.Data()), b.Size() / sizeof(T));
        }
        template <class T>
        bool SetPixelData(Span<const T> data, i32 mipLevel, i32 sourceDataStartIndex = 0)
        {
            if (sourceDataStartIndex < 0 || usize(sourceDataStartIndex) > data.Size())
                return SetPixelBytes(nullptr, ~usize(0), mipLevel);
            return SetPixelBytes(reinterpret_cast<const u8*>(data.Data() + sourceDataStartIndex), (data.Size() - usize(sourceDataStartIndex)) * sizeof(T),
                                 mipLevel);
        }
        // Every mip one after the other.
        Span<u8> GetRawTextureData() const;
        HX_FUNCTION(Void) bool LoadRawTextureData(Span<const u8> data);
        // The raw bytes as a result (the reference's byte[] GetRawTextureData()).
        HX_FUNCTION(Name = "GetRawTextureData") Array<u8> RawTextureData() const
        {
            Span<u8> b = GetRawTextureData();
            Array<u8> out;
            out.Append(b.Data(), b.Size());
            return out;
        }

        // Makes the mips from the top level when updateMipmaps, then hands the pixels to the renderer;
        // makeNoLongerReadable makes the CPU copy inaccessible (it goes once the renderer holds the pixels).
        HX_FUNCTION() void Apply(bool updateMipmaps = true, bool makeNoLongerReadable = false);
        // To DXT1 (no alpha) or DXT5.
        bool Compress(bool highQuality);

        // ImageConversion: the file's size and pixels replace the texture's (any format Helix.Image decodes).
        bool LoadImage(Span<const u8> data, bool markNonReadable = false);
        bool EncodeToPNG(Array<u8>& out) const;
        bool EncodeToJPG(Array<u8>& out, i32 quality = 75) const;
        bool EncodeToTGA(Array<u8>& out) const;
        bool EncodeToEXR(Array<u8>& out, u32 flags = 0) const;

        // 4 x 4 textures made on first use and kept for good.
        HX_PROPERTY(ReadOnly) static Texture2D* whiteTexture();
        HX_PROPERTY(ReadOnly) static Texture2D* blackTexture();
        HX_PROPERTY(ReadOnly) static Texture2D* grayTexture();
        HX_PROPERTY(ReadOnly) static Texture2D* linearGrayTexture();
        HX_PROPERTY(ReadOnly) static Texture2D* redTexture();
        HX_PROPERTY(ReadOnly) static Texture2D* normalTexture();

        // Resources.UnloadAsset.
        void UnloadDataNative()
        {
            data_ = image::TextureData();
            version_++;
        }

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            i32 w = i32(data_.width), h = i32(data_.height), mips = i32(data_.mips);
            TextureFormat f = format_;
            v.Field("m_Width", w);
            v.Field("m_Height", h);
            v.Field("m_TextureFormat", f);
            v.Field("m_MipCount", mips);
            v.Field("m_ImageData", data_.data);
            if constexpr (V::kReading)
                Loaded(w, h, f, mips);
        }

    private:
        bool Init(i32 width, i32 height, TextureFormat format, i32 mipCount, bool linear, bool clear);
        bool Writable(StringView what) const;
        bool Readable(StringView what) const;
        Span<u8> MipBytes(i32 mip) const;
        bool SetPixelBytes(const u8* data, usize size, i32 mip);
        // Reads a rectangle of a mip as floats (block formats decoded); false on bad arguments.
        bool ReadRect(i32 x, i32 y, i32 w, i32 h, i32 mip, f32* rgba, StringView what) const;
        bool WriteRect(i32 x, i32 y, i32 w, i32 h, i32 mip, const f32* rgba, StringView what);
        void Loaded(i32 width, i32 height, TextureFormat format, i32 mips);

        TextureFormat format_ = TextureFormat::RGBA32;
    };
}
