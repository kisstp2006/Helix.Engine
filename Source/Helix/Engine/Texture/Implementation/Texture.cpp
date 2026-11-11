#include <Helix/Engine/Texture/Texture.h>

#include <Helix/Core/Logging/Log.h>
#include <Helix/Core/Strings/Format.h>
#include <Helix/Image/Codecs/ImageDecoder.h>
#include <Helix/Image/Codecs/ImageEncoder.h>
#include <Helix/Image/Texture/BlockCodec.h>
#include <Helix/Image/Texture/Convert.h>
#include <Helix/Image/Texture/Mipmaps.h>

#include <cmath>
#include <cstring>

namespace helix::engine
{
    namespace
    {
        LogChannel TextureLog{"Engine"};
        using image::PixelFormat;

        struct FormatPair
        {
            TextureFormat texture;
            PixelFormat pixel;
        };

        // The layouts the engine holds; the first entry of a pixel format is its texture format. Formats missing here
        // (Alpha8, ARGB32, the 4444 and crunched ones, YUY2, ASTC HDR, most signed ones) have no Helix.Image layout.
        constexpr FormatPair kFormats[] = {
            {TextureFormat::RGBA32, PixelFormat::RGBA8},
            {TextureFormat::RGB24, PixelFormat::RGB8},
            {TextureFormat::BGRA32, PixelFormat::BGRA8},
            {TextureFormat::R8, PixelFormat::R8},
            {TextureFormat::RG16, PixelFormat::RG8},
            {TextureFormat::R16, PixelFormat::R16},
            {TextureFormat::RG32, PixelFormat::RG16},
            {TextureFormat::RGB48, PixelFormat::RGB16},
            {TextureFormat::RGBA64, PixelFormat::RGBA16},
            {TextureFormat::R8_SIGNED, PixelFormat::R8Snorm},
            {TextureFormat::RG16_SIGNED, PixelFormat::RG8Snorm},
            {TextureFormat::RGBA32_SIGNED, PixelFormat::RGBA8Snorm},
            {TextureFormat::RHalf, PixelFormat::R16Float},
            {TextureFormat::RGHalf, PixelFormat::RG16Float},
            {TextureFormat::RGBAHalf, PixelFormat::RGBA16Float},
            {TextureFormat::RFloat, PixelFormat::R32Float},
            {TextureFormat::RGFloat, PixelFormat::RG32Float},
            {TextureFormat::RGBAFloat, PixelFormat::RGBA32Float},
            {TextureFormat::RGB565, PixelFormat::B5G6R5},
            {TextureFormat::RGB9e5Float, PixelFormat::RGB9E5Float},
            {TextureFormat::DXT1, PixelFormat::BC1},
            {TextureFormat::DXT5, PixelFormat::BC3},
            {TextureFormat::BC4, PixelFormat::BC4},
            {TextureFormat::BC5, PixelFormat::BC5},
            {TextureFormat::BC6H, PixelFormat::BC6H},
            {TextureFormat::BC7, PixelFormat::BC7},
            {TextureFormat::ETC2_RGB, PixelFormat::ETC2RGB},
            {TextureFormat::ETC2_RGBA1, PixelFormat::ETC2RGBA1},
            {TextureFormat::ETC2_RGBA8, PixelFormat::ETC2RGBA},
            {TextureFormat::EAC_R, PixelFormat::EACR11},
            {TextureFormat::EAC_R_SIGNED, PixelFormat::EACR11Snorm},
            {TextureFormat::EAC_RG, PixelFormat::EACRG11},
            {TextureFormat::EAC_RG_SIGNED, PixelFormat::EACRG11Snorm},
            {TextureFormat::ASTC_4x4, PixelFormat::ASTC4x4},
            {TextureFormat::ASTC_5x5, PixelFormat::ASTC5x5},
            {TextureFormat::ASTC_6x6, PixelFormat::ASTC6x6},
            {TextureFormat::ASTC_8x8, PixelFormat::ASTC8x8},
            {TextureFormat::ASTC_10x10, PixelFormat::ASTC10x10},
            {TextureFormat::ASTC_12x12, PixelFormat::ASTC12x12},
            // ETC1 blocks are ETC2 RGB blocks.
            {TextureFormat::ETC_RGB4, PixelFormat::ETC2RGB},
        };

        TextureWrapMode WrapOf(const TextureSettings& s, bool vertical) { return vertical ? s.wrapV : s.wrapU; }

        i32 Wrap(i32 x, i32 size, TextureWrapMode mode)
        {
            switch (mode)
            {
            case TextureWrapMode::Repeat:
                return ((x % size) + size) % size;
            case TextureWrapMode::Mirror:
            {
                i32 m = ((x % (2 * size)) + 2 * size) % (2 * size);
                return m < size ? m : 2 * size - 1 - m;
            }
            case TextureWrapMode::MirrorOnce:
                x = x < 0 ? -x - 1 : x;
                return x >= size ? size - 1 : x;
            case TextureWrapMode::Clamp:
            default:
                return x < 0 ? 0 : x >= size ? size - 1 : x;
            }
        }

        // Rows of every subresource reversed (files hold rows top first, textures bottom first).
        void FlipRows(image::TextureData& data)
        {
            Array<u8> row;
            for (const image::Subresource& s : data.subresources)
            {
                usize rowBytes = usize(image::SurfaceBytes(data.format, s.width, 1));
                usize slice = usize(image::SurfaceBytes(data.format, s.width, s.height));
                row.Resize(rowBytes);
                for (u32 z = 0; z < (s.depth ? s.depth : 1); z++)
                {
                    u8* base = data.data.Data() + s.offset + z * slice;
                    for (u32 y = 0; y < s.height / 2; y++)
                    {
                        u8* a = base + y * rowBytes;
                        u8* b = base + (s.height - 1 - y) * rowBytes;
                        std::memcpy(row.Data(), a, rowBytes);
                        std::memcpy(a, b, rowBytes);
                        std::memcpy(b, row.Data(), rowBytes);
                    }
                }
            }
        }

        // Layer 0, face 0 and the first depth slice of every mip.
        bool First2D(image::TextureData& data, String& error)
        {
            if (data.layers == 1 && data.faces == 1 && data.depth <= 1)
                return true;
            image::TextureData out;
            if (!out.Allocate(data.format, data.width, data.height, 1, 1, 1, data.mips))
                return error = "the texture is too large", false;
            out.colorSpace = data.colorSpace;
            for (u32 m = 0; m < data.mips; m++)
                std::memcpy(out.Pixels(0, 0, m), data.Pixels(0, 0, m), usize(out.At(0, 0, m).size));
            data = Move(out);
            return true;
        }

        // Every subresource converted to `to`.
        bool Convert(image::TextureData& data, PixelFormat to, String& error)
        {
            image::TextureData out;
            if (!out.Allocate(to, data.width, data.height, data.depth, data.layers, data.faces, data.mips))
                return error = "the texture is too large", false;
            out.colorSpace = data.colorSpace;
            out.dimension = data.dimension;
            for (u32 i = 0; i < data.subresources.Size(); i++)
            {
                const image::Subresource& s = data.subresources[i];
                usize count = usize(s.width) * s.height * (s.depth ? s.depth : 1);
                if (!image::ConvertPixels(data.format, data.data.Data() + s.offset, to, out.data.Data() + out.subresources[i].offset, count))
                    return error = Format("pixels of {} cannot be converted", image::FormatInfo(data.format).name), false;
            }
            data = Move(out);
            return true;
        }

        PixelFormat WideFormat(PixelFormat f)
        {
            const image::PixelFormatInfo& info = image::FormatInfo(f);
            u32 bits = info.channels ? u32(info.bytesPerBlock) * 8 / info.channels : 8;
            if (info.kind == image::PixelKind::Float || info.kind == image::PixelKind::Packed)
                return bits <= 16 ? PixelFormat::RGBA16Float : PixelFormat::RGBA32Float;
            if (info.kind == image::PixelKind::Uint)
                return PixelFormat::RGBA32Float;
            return bits > 8 ? PixelFormat::RGBA16 : PixelFormat::RGBA8;
        }

        // Pixels of one mip as floats, decoding block formats.
        bool MipFloats(const image::TextureData& data, u32 mip, Array<f32>& out, String& error)
        {
            const image::TextureData* src = &data;
            image::TextureData decoded;
            if (image::IsBlockFormat(data.format))
            {
                if (!image::DecompressTexture(data, decoded, error))
                    return false;
                src = &decoded;
            }
            if (!image::CanConvert(src->format))
                return error = Format("pixels of {} cannot be read", image::FormatInfo(src->format).name), false;
            const image::Subresource& s = src->At(0, 0, mip);
            out.Resize(usize(s.width) * s.height * 4);
            image::ToFloat(src->format, src->data.Data() + s.offset, out.Data(), usize(s.width) * s.height);
            return true;
        }

        f32 ToUnit(u8 v) { return f32(v) / 255.0f; }
        u8 FromUnit(f32 v)
        {
            v = v < 0 ? 0 : v > 1 ? 1 : v;
            return u8(std::lround(v * 255.0f));
        }
    }

    PixelFormat ToPixelFormat(TextureFormat format)
    {
        for (const FormatPair& p : kFormats)
            if (p.texture == format)
                return p.pixel;
        return PixelFormat::Unknown;
    }

    bool ToTextureFormat(PixelFormat format, TextureFormat& out)
    {
        for (const FormatPair& p : kFormats)
            if (p.pixel == format)
                return out = p.texture, true;
        return false;
    }

    bool ToTextureLayout(image::TextureData& data, String& error)
    {
        TextureFormat f = TextureFormat::RGBA32;
        if (ToTextureFormat(data.format, f))
            return true;
        if (image::IsBlockFormat(data.format))
        {
            image::TextureData decoded;
            if (!image::DecompressTexture(data, decoded, error))
                return false;
            data = Move(decoded);
            if (ToTextureFormat(data.format, f))
                return true;
        }
        if (!image::CanConvert(data.format))
            return error = Format("the format {} has no texture format", image::FormatInfo(data.format).name), false;
        return Convert(data, WideFormat(data.format), error);
    }

    void Texture::ReleaseCpuData()
    {
        if (!readable_)
            data_.data = Array<u8>();
    }

    // ------------------------------------------------------------------------------------------------- creation

    bool Texture2D::Init(i32 width, i32 height, TextureFormat format, i32 mipCount, bool linear, bool clear)
    {
        PixelFormat pf = ToPixelFormat(format);
        if (pf == PixelFormat::Unknown)
        {
            HX_LOG_ERROR(TextureLog, "Texture2D: the texture format {} is not supported.", i32(format));
            return false;
        }
        if (width <= 0 || height <= 0)
        {
            HX_LOG_ERROR(TextureLog, "Texture2D: the size {} x {} is not valid.", width, height);
            return false;
        }
        u32 full = image::MipCount(u32(width), u32(height));
        u32 mips = mipCount < 0 ? full : mipCount == 0 ? 1 : u32(mipCount) > full ? full : u32(mipCount);
        image::TextureData data;
        if (!data.Allocate(pf, u32(width), u32(height), 1, 1, 1, mips))
        {
            HX_LOG_ERROR(TextureLog, "Texture2D: {} x {} is too large.", width, height);
            return false;
        }
        if (clear && !data.data.Empty())
            std::memset(data.data.Data(), 0, data.data.Size());
        data.colorSpace = linear ? image::ColorSpace::Linear : image::ColorSpace::Srgb;
        data_ = Move(data);
        format_ = format;
        linear_ = linear;
        readable_ = true;
        version_++;
        return true;
    }

    Texture2D* Texture2D::Create(i32 width, i32 height, TextureFormat format, bool mipChain, bool linear, bool createUninitialized)
    {
        return Create(width, height, format, mipChain ? GenerateAllMips : 1, linear, createUninitialized);
    }

    Texture2D* Texture2D::Create(i32 width, i32 height, TextureFormat format, i32 mipCount, bool linear, bool createUninitialized)
    {
        Texture2D* t = NewObject<Texture2D>();
        if (!t->Init(width, height, format, mipCount, linear, !createUninitialized))
        {
            DeleteObject(t);
            return nullptr;
        }
        return t;
    }

    bool PrepareTextureData(image::TextureData& data, String& error)
    {
        PixelFormat original = data.format;
        bool block = image::IsBlockFormat(original);
        if (!First2D(data, error))
            return false;
        if (block)
        {
            image::TextureData decoded;
            if (!image::DecompressTexture(data, decoded, error))
                return false;
            data = Move(decoded);
        }
        FlipRows(data);
        if (!ToTextureLayout(data, error))
            return false;
        // Block formats go back to their blocks when Helix.Image can write them (rows flipped in the pixels).
        TextureFormat f = TextureFormat::RGBA32;
        image::BlockCodec codec;
        if (block && ToTextureFormat(original, f) && image::FindBlockCodec(original, codec) && codec.encode)
        {
            image::TextureData blocks;
            if (image::CompressTexture(data, original, image::CompressionQuality::Normal, blocks, error))
                data = Move(blocks);
        }
        if (!ToTextureFormat(data.format, f))
            return error = "no texture format holds the pixels", false;
        return true;
    }

    Texture2D* Texture2D::CreatePrepared(image::TextureData&& data, bool linear)
    {
        TextureFormat f = TextureFormat::RGBA32;
        if (!ToTextureFormat(data.format, f) || data.layers != 1 || data.faces != 1 || data.depth > 1)
        {
            HX_LOG_ERROR(TextureLog, "Texture2D: the pixels are not prepared for a 2D texture.");
            return nullptr;
        }
        Texture2D* t = NewObject<Texture2D>();
        data.colorSpace = linear ? image::ColorSpace::Linear : image::ColorSpace::Srgb;
        t->data_ = Move(data);
        t->format_ = f;
        t->linear_ = linear;
        t->version_++;
        return t;
    }

    Texture2D* Texture2D::Create(image::TextureData&& data, bool linear)
    {
        String error;
        if (!PrepareTextureData(data, error))
        {
            HX_LOG_ERROR(TextureLog, "Texture2D: {}.", error);
            return nullptr;
        }
        return CreatePrepared(Move(data), linear);
    }

    bool Texture2D::Reinitialize(i32 width, i32 height) { return Reinitialize(width, height, format_, data_.mips > 1); }

    bool Texture2D::Reinitialize(i32 width, i32 height, TextureFormat format, bool hasMipMap)
    {
        if (!Readable("Reinitialize"))
            return false;
        return Init(width, height, format, hasMipMap ? GenerateAllMips : 1, linear_, true);
    }

    // ------------------------------------------------------------------------------------------------- pixels

    bool Texture2D::Readable(StringView what) const
    {
        if (readable_)
            return true;
        HX_LOG_ERROR(TextureLog, "{}: texture '{}' is not readable; its memory can not be accessed from scripts.", what, name_);
        return false;
    }

    bool Texture2D::Writable(StringView what) const
    {
        if (!Readable(what))
            return false;
        if (image::IsBlockFormat(data_.format) || !image::CanConvert(data_.format))
        {
            HX_LOG_ERROR(TextureLog, "{}: texture '{}' has the compressed format {}, its pixels can not be written.", what, name_, i32(format_));
            return false;
        }
        return true;
    }

    Span<u8> Texture2D::MipBytes(i32 mip) const
    {
        if (mip < 0 || u32(mip) >= data_.mips || data_.data.Empty() || !Readable("GetPixelData"))
            return {};
        const image::Subresource& s = data_.At(0, 0, u32(mip));
        return Span<u8>(const_cast<u8*>(data_.data.Data()) + s.offset, usize(s.size));
    }

    bool Texture2D::SetPixelBytes(const u8* data, usize size, i32 mip)
    {
        if (!Readable("SetPixelData"))
            return false;
        if (mip < 0 || u32(mip) >= data_.mips)
        {
            HX_LOG_ERROR(TextureLog, "SetPixelData: mip {} of texture '{}' does not exist.", mip, name_);
            return false;
        }
        const image::Subresource& s = data_.At(0, 0, u32(mip));
        if (!data || size < s.size)
        {
            HX_LOG_ERROR(TextureLog, "SetPixelData: texture '{}' needs {} bytes for mip {}.", name_, s.size, mip);
            return false;
        }
        std::memcpy(data_.data.Data() + s.offset, data, usize(s.size));
        version_++;
        return true;
    }

    bool Texture2D::ReadRect(i32 x, i32 y, i32 w, i32 h, i32 mip, f32* rgba, StringView what) const
    {
        if (!Readable(what))
            return false;
        if (mip < 0 || u32(mip) >= data_.mips)
        {
            HX_LOG_ERROR(TextureLog, "{}: mip {} of texture '{}' does not exist.", what, mip, name_);
            return false;
        }
        const image::Subresource& s = data_.At(0, 0, u32(mip));
        if (x < 0 || y < 0 || w < 0 || h < 0 || u32(x + w) > s.width || u32(y + h) > s.height)
        {
            HX_LOG_ERROR(TextureLog, "{}: the rectangle {}, {}, {} x {} is outside mip {} of texture '{}'.", what, x, y, w, h, mip, name_);
            return false;
        }
        if (!image::IsBlockFormat(data_.format) && image::CanConvert(data_.format))
        {
            usize pixel = image::FormatInfo(data_.format).bytesPerBlock;
            for (i32 row = 0; row < h; row++)
                image::ToFloat(data_.format, data_.data.Data() + s.offset + (usize(y + row) * s.width + usize(x)) * pixel, rgba + usize(row) * usize(w) * 4,
                               usize(w));
            return true;
        }
        Array<f32> all;
        String error;
        if (!MipFloats(data_, u32(mip), all, error))
        {
            HX_LOG_ERROR(TextureLog, "{}: texture '{}': {}.", what, name_, error);
            return false;
        }
        for (i32 row = 0; row < h; row++)
            std::memcpy(rgba + usize(row) * usize(w) * 4, all.Data() + (usize(y + row) * s.width + usize(x)) * 4, usize(w) * 4 * sizeof(f32));
        return true;
    }

    bool Texture2D::WriteRect(i32 x, i32 y, i32 w, i32 h, i32 mip, const f32* rgba, StringView what)
    {
        if (!Writable(what))
            return false;
        if (mip < 0 || u32(mip) >= data_.mips)
        {
            HX_LOG_ERROR(TextureLog, "{}: mip {} of texture '{}' does not exist.", what, mip, name_);
            return false;
        }
        const image::Subresource& s = data_.At(0, 0, u32(mip));
        if (x < 0 || y < 0 || w < 0 || h < 0 || u32(x + w) > s.width || u32(y + h) > s.height)
        {
            HX_LOG_ERROR(TextureLog, "{}: the rectangle {}, {}, {} x {} is outside mip {} of texture '{}'.", what, x, y, w, h, mip, name_);
            return false;
        }
        usize pixel = image::FormatInfo(data_.format).bytesPerBlock;
        for (i32 row = 0; row < h; row++)
            image::FromFloat(data_.format, rgba + usize(row) * usize(w) * 4, data_.data.Data() + s.offset + (usize(y + row) * s.width + usize(x)) * pixel,
                             usize(w));
        version_++;
        return true;
    }

    Color Texture2D::GetPixel(i32 x, i32 y, i32 mipLevel) const
    {
        Color c;
        if (mipLevel < 0 || u32(mipLevel) >= data_.mips || data_.data.Empty())
        {
            ReadRect(0, 0, 0, 0, mipLevel, &c.r, "GetPixel");
            return c;
        }
        const image::Subresource& s = data_.At(0, 0, u32(mipLevel));
        x = Wrap(x, i32(s.width), WrapOf(settings_, false));
        y = Wrap(y, i32(s.height), WrapOf(settings_, true));
        ReadRect(x, y, 1, 1, mipLevel, &c.r, "GetPixel");
        return c;
    }

    Color Texture2D::GetPixelBilinear(f32 u, f32 v, i32 mipLevel) const
    {
        if (mipLevel < 0 || u32(mipLevel) >= data_.mips || !Readable("GetPixelBilinear"))
            return Color();
        Array<f32> all;
        String error;
        if (!MipFloats(data_, u32(mipLevel), all, error))
        {
            HX_LOG_ERROR(TextureLog, "GetPixelBilinear: texture '{}': {}.", name_, error);
            return Color();
        }
        const image::Subresource& s = data_.At(0, 0, u32(mipLevel));
        i32 w = i32(s.width), h = i32(s.height);
        f32 px = u * f32(w) - 0.5f, py = v * f32(h) - 0.5f;
        f32 fx0 = std::floor(px), fy0 = std::floor(py);
        f32 fx = px - fx0, fy = py - fy0;
        i32 x0 = i32(fx0), y0 = i32(fy0);
        i32 xs[2] = {Wrap(x0, w, settings_.wrapU), Wrap(x0 + 1, w, settings_.wrapU)};
        i32 ys[2] = {Wrap(y0, h, settings_.wrapV), Wrap(y0 + 1, h, settings_.wrapV)};
        f32 out[4] = {};
        for (u32 k = 0; k < 4; k++)
        {
            auto at = [&](i32 x, i32 y) { return all[(usize(y) * usize(w) + usize(x)) * 4 + k]; };
            f32 top = at(xs[0], ys[0]) * (1 - fx) + at(xs[1], ys[0]) * fx;
            f32 bottom = at(xs[0], ys[1]) * (1 - fx) + at(xs[1], ys[1]) * fx;
            out[k] = top * (1 - fy) + bottom * fy;
        }
        return Color(out[0], out[1], out[2], out[3]);
    }

    Array<Color> Texture2D::GetPixels(i32 mipLevel) const
    {
        if (mipLevel < 0 || u32(mipLevel) >= data_.mips)
            return GetPixels(0, 0, 1, 1, mipLevel);
        const image::Subresource& s = data_.At(0, 0, u32(mipLevel));
        return GetPixels(0, 0, i32(s.width), i32(s.height), mipLevel);
    }

    Array<Color> Texture2D::GetPixels(i32 x, i32 y, i32 blockWidth, i32 blockHeight, i32 mipLevel) const
    {
        Array<Color> out;
        if (blockWidth <= 0 || blockHeight <= 0)
            return out;
        out.Resize(usize(blockWidth) * usize(blockHeight));
        if (!ReadRect(x, y, blockWidth, blockHeight, mipLevel, &out.Data()->r, "GetPixels"))
            out.Clear();
        return out;
    }

    Array<Color32> Texture2D::GetPixels32(i32 mipLevel) const
    {
        Array<Color> c = GetPixels(mipLevel);
        Array<Color32> out;
        out.Resize(c.Size());
        for (usize i = 0; i < c.Size(); i++)
            out[i] = Color32(FromUnit(c[i].r), FromUnit(c[i].g), FromUnit(c[i].b), FromUnit(c[i].a));
        return out;
    }

    bool Texture2D::SetPixel(i32 x, i32 y, Color color, i32 mipLevel)
    {
        if (mipLevel < 0 || u32(mipLevel) >= data_.mips)
            return WriteRect(0, 0, 0, 0, mipLevel, &color.r, "SetPixel");
        const image::Subresource& s = data_.At(0, 0, u32(mipLevel));
        // Writes outside the texture are dropped.
        if (x < 0 || y < 0 || u32(x) >= s.width || u32(y) >= s.height)
            return Writable("SetPixel");
        return WriteRect(x, y, 1, 1, mipLevel, &color.r, "SetPixel");
    }

    bool Texture2D::SetPixels(Span<const Color> colors, i32 mipLevel)
    {
        if (mipLevel < 0 || u32(mipLevel) >= data_.mips)
            return WriteRect(0, 0, 0, 0, mipLevel, nullptr, "SetPixels");
        const image::Subresource& s = data_.At(0, 0, u32(mipLevel));
        return SetPixels(0, 0, i32(s.width), i32(s.height), colors, mipLevel);
    }

    bool Texture2D::SetPixels(i32 x, i32 y, i32 blockWidth, i32 blockHeight, Span<const Color> colors, i32 mipLevel)
    {
        if (blockWidth < 0 || blockHeight < 0 || colors.Size() < usize(blockWidth) * usize(blockHeight))
        {
            HX_LOG_ERROR(TextureLog, "SetPixels on texture '{}': {} colors for {} x {} pixels.", name_, colors.Size(), blockWidth, blockHeight);
            return false;
        }
        return WriteRect(x, y, blockWidth, blockHeight, mipLevel, reinterpret_cast<const f32*>(colors.Data()), "SetPixels");
    }

    bool Texture2D::SetPixels32(Span<const Color32> colors, i32 mipLevel)
    {
        if (mipLevel < 0 || u32(mipLevel) >= data_.mips)
            return WriteRect(0, 0, 0, 0, mipLevel, nullptr, "SetPixels32");
        const image::Subresource& s = data_.At(0, 0, u32(mipLevel));
        return SetPixels32(0, 0, i32(s.width), i32(s.height), colors, mipLevel);
    }

    bool Texture2D::SetPixels32(i32 x, i32 y, i32 blockWidth, i32 blockHeight, Span<const Color32> colors, i32 mipLevel)
    {
        if (blockWidth < 0 || blockHeight < 0 || colors.Size() < usize(blockWidth) * usize(blockHeight))
        {
            HX_LOG_ERROR(TextureLog, "SetPixels32 on texture '{}': {} colors for {} x {} pixels.", name_, colors.Size(), blockWidth, blockHeight);
            return false;
        }
        Array<f32> f;
        f.Resize(usize(blockWidth) * usize(blockHeight) * 4);
        for (usize i = 0; i < usize(blockWidth) * usize(blockHeight); i++)
        {
            f[i * 4 + 0] = ToUnit(colors[i].r);
            f[i * 4 + 1] = ToUnit(colors[i].g);
            f[i * 4 + 2] = ToUnit(colors[i].b);
            f[i * 4 + 3] = ToUnit(colors[i].a);
        }
        return WriteRect(x, y, blockWidth, blockHeight, mipLevel, f.Data(), "SetPixels32");
    }

    Span<u8> Texture2D::GetRawTextureData() const
    {
        if (!Readable("GetRawTextureData"))
            return {};
        return Span<u8>(const_cast<u8*>(data_.data.Data()), data_.data.Size());
    }

    bool Texture2D::LoadRawTextureData(Span<const u8> data)
    {
        if (!Readable("LoadRawTextureData"))
            return false;
        if (data.Size() < data_.data.Size())
        {
            HX_LOG_ERROR(TextureLog, "LoadRawTextureData: not enough data for texture '{}' ({} of {} bytes).", name_, data.Size(), data_.data.Size());
            return false;
        }
        std::memcpy(data_.data.Data(), data.Data(), data_.data.Size());
        version_++;
        return true;
    }

    void Texture2D::Apply(bool updateMipmaps, bool makeNoLongerReadable)
    {
        if (!Readable("Apply"))
            return;
        if (updateMipmaps && data_.mips > 1 && !image::IsBlockFormat(data_.format))
        {
            image::MipmapSettings settings;
            settings.srgb = !linear_;
            image::TextureData chain;
            String error;
            if (!image::GenerateMipmaps(data_, settings, chain, error))
                HX_LOG_ERROR(TextureLog, "Apply on texture '{}': the mips could not be made: {}.", name_, error);
            else
                for (u32 m = 1; m < data_.mips && m < chain.mips; m++)
                    std::memcpy(data_.Pixels(0, 0, m), chain.Pixels(0, 0, m), usize(data_.At(0, 0, m).size));
        }
        updateCount_++;
        version_++;
        if (makeNoLongerReadable)
            readable_ = false;
    }

    bool Texture2D::Compress(bool highQuality)
    {
        if (!Readable("Compress") || image::IsBlockFormat(data_.format))
            return false;
        bool alpha = image::FormatInfo(data_.format).channels == 4;
        PixelFormat to = alpha ? PixelFormat::BC3 : PixelFormat::BC1;
        image::TextureData out;
        String error;
        if (!image::CompressTexture(data_, to, highQuality ? image::CompressionQuality::Best : image::CompressionQuality::Normal, out, error))
        {
            HX_LOG_ERROR(TextureLog, "Compress on texture '{}': {}.", name_, error);
            return false;
        }
        out.colorSpace = data_.colorSpace;
        data_ = Move(out);
        format_ = alpha ? TextureFormat::DXT5 : TextureFormat::DXT1;
        version_++;
        return true;
    }

    bool Texture2D::LoadImage(Span<const u8> bytes, bool markNonReadable)
    {
        if (!Readable("LoadImage"))
            return false;
        image::TextureData decoded;
        String error;
        if (!image::DecodeImage(bytes, decoded, error))
        {
            HX_LOG_ERROR(TextureLog, "LoadImage on texture '{}': {}.", name_, error);
            return false;
        }
        bool mips = data_.mips > 1;
        bool ok = First2D(decoded, error);
        if (ok && image::IsBlockFormat(decoded.format))
        {
            image::TextureData d;
            ok = image::DecompressTexture(decoded, d, error);
            if (ok)
                decoded = Move(d);
        }
        if (ok)
        {
            FlipRows(decoded);
            ok = ToTextureLayout(decoded, error);
        }
        TextureFormat f = format_;
        if (ok && !ToTextureFormat(decoded.format, f))
            ok = false, error = "no texture format holds the pixels";
        if (!ok)
        {
            HX_LOG_ERROR(TextureLog, "LoadImage on texture '{}': {}.", name_, error);
            return false;
        }
        if (mips && decoded.mips == 1)
        {
            image::TextureData chain;
            if (chain.Allocate(decoded.format, decoded.width, decoded.height, 1, 1, 1, image::MipCount(decoded.width, decoded.height)))
            {
                std::memcpy(chain.Pixels(0, 0, 0), decoded.Pixels(0, 0, 0), usize(decoded.At(0, 0, 0).size));
                decoded = Move(chain);
            }
        }
        decoded.colorSpace = linear_ ? image::ColorSpace::Linear : image::ColorSpace::Srgb;
        data_ = Move(decoded);
        format_ = f;
        Apply(mips, markNonReadable);
        return true;
    }

    namespace
    {
        bool Encode(const image::TextureData& data, StringView format, Array<u8>& out, const image::EncodeOptions& options, StringView name)
        {
            out.Clear();
            String error;
            image::TextureData top;
            const image::TextureData* src = &data;
            image::TextureData decoded;
            if (image::IsBlockFormat(data.format))
            {
                if (!image::DecompressTexture(data, decoded, error))
                {
                    HX_LOG_ERROR(TextureLog, "EncodeTo{} on texture '{}': {}.", format, name, error);
                    return false;
                }
                src = &decoded;
            }
            if (!top.Allocate(src->format, src->width, src->height))
                return false;
            std::memcpy(top.Pixels(), src->Pixels(), usize(top.At(0, 0, 0).size));
            FlipRows(top);
            if (!image::EncodeImage(top, format, out, error, options))
            {
                HX_LOG_ERROR(TextureLog, "EncodeTo{} on texture '{}': {}.", format, name, error);
                return false;
            }
            return true;
        }
    }

    bool Texture2D::EncodeToPNG(Array<u8>& out) const { return Readable("EncodeToPNG") && Encode(data_, "png", out, {}, name_); }
    bool Texture2D::EncodeToJPG(Array<u8>& out, i32 quality) const
    {
        image::EncodeOptions o;
        o.quality = u32(quality < 1 ? 1 : quality > 100 ? 100 : quality);
        return Readable("EncodeToJPG") && Encode(data_, "jpg", out, o, name_);
    }
    bool Texture2D::EncodeToTGA(Array<u8>& out) const { return Readable("EncodeToTGA") && Encode(data_, "tga", out, {}, name_); }
    bool Texture2D::EncodeToEXR(Array<u8>& out, u32 flags) const
    {
        image::EncodeOptions o;
        o.exrFlags = flags;
        return Readable("EncodeToEXR") && Encode(data_, "exr", out, o, name_);
    }

    namespace
    {
        Texture2D* Builtin(EntityId& id, StringView name, Color32 color, bool linear)
        {
            if (Texture2D* t = EntityIdToObject<Texture2D>(id))
                return t;
            Texture2D* t = Texture2D::Create(4, 4, TextureFormat::RGBA32, false, linear);
            Color32 pixels[16];
            for (Color32& p : pixels)
                p = color;
            t->SetPixels32(Span<const Color32>(pixels, 16));
            t->Apply(false);
            t->SetName(name);
            t->SetPersistent(true);
            t->hideFlags = HideFlags::HideAndDontSave;
            id = t->GetEntityId();
            return t;
        }
        EntityId gBuiltins[6];
    }

    Texture2D* Texture2D::whiteTexture() { return Builtin(gBuiltins[0], "White", Color32(255, 255, 255, 255), false); }
    Texture2D* Texture2D::blackTexture() { return Builtin(gBuiltins[1], "Black", Color32(0, 0, 0, 0), false); }
    Texture2D* Texture2D::grayTexture() { return Builtin(gBuiltins[2], "Gray", Color32(128, 128, 128, 128), false); }
    Texture2D* Texture2D::linearGrayTexture() { return Builtin(gBuiltins[3], "LinearGray", Color32(128, 128, 128, 128), true); }
    Texture2D* Texture2D::redTexture() { return Builtin(gBuiltins[4], "Red", Color32(255, 0, 0, 255), false); }
    Texture2D* Texture2D::normalTexture() { return Builtin(gBuiltins[5], "Normal", Color32(128, 128, 255, 128), true); }

    void Texture2D::Loaded(i32 width, i32 height, TextureFormat format, i32 mips)
    {
        PixelFormat pf = ToPixelFormat(format);
        String error;
        image::TextureData table;
        if (pf == PixelFormat::Unknown)
            error = Format("the texture format {} is not supported", i32(format));
        else if (width <= 0 || height <= 0 || mips <= 0 || u32(mips) > image::MipCount(u32(width), u32(height)) ||
                 !table.Allocate(pf, u32(width), u32(height), 1, 1, 1, u32(mips)))
            error = Format("{} x {} with {} mips is not a valid size", width, height, mips);
        else if (table.data.Size() != data_.data.Size())
            error = Format("{} bytes of pixels where {} x {} needs {}", data_.data.Size(), width, height, table.data.Size());
        if (!error.Empty())
        {
            HX_LOG_ERROR(TextureLog, "Texture '{}' read from a file is damaged: {}.", name_, error);
            data_ = image::TextureData();
            format_ = TextureFormat::RGBA32;
            version_++;
            return;
        }
        table.data = Move(data_.data);
        table.colorSpace = linear_ ? image::ColorSpace::Linear : image::ColorSpace::Srgb;
        data_ = Move(table);
        format_ = format;
        version_++;
    }
}
