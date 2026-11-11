#include <Helix/Engine/Asset/AssetLoader.h>
#include <Helix/Engine/Asset/Implementation/MetaFields.h>
#include <Helix/Engine/Asset/Implementation/TextureImport.h>
#include <Helix/Engine/Texture/Texture.h>

#include <Helix/Image/Codecs/ImageDecoder.h>
#include <Helix/Image/Texture/Mipmaps.h>
#include <Helix/Image/Texture/Resize.h>

namespace helix::engine
{
    namespace
    {
        // The TextureImporter fields of a .meta the loader follows.
        constexpr MetaField kTextureFields[] = {
            {"enableMipMap", [](void* s, double v) { static_cast<TextureImportSettings*>(s)->mipmaps = v != 0; }},
            {"sRGBTexture", [](void* s, double v) { static_cast<TextureImportSettings*>(s)->srgb = v != 0; }},
            {"isReadable", [](void* s, double v) { static_cast<TextureImportSettings*>(s)->readable = v != 0; }},
            {"maxTextureSize", [](void* s, double v) { static_cast<TextureImportSettings*>(s)->maxSize = u32(v); }},
            {"textureType", [](void* s, double v) { static_cast<TextureImportSettings*>(s)->normalMap = i32(v) == 1; }},
            {"filterMode", [](void* s, double v) { static_cast<TextureImportSettings*>(s)->sampling.filterMode = FilterMode(i32(v)); }},
            {"aniso", [](void* s, double v) { static_cast<TextureImportSettings*>(s)->sampling.aniso = i32(v); }},
            {"mipBias", [](void* s, double v) { static_cast<TextureImportSettings*>(s)->sampling.mipBias = f32(v); }},
            {"wrapU", [](void* s, double v) { static_cast<TextureImportSettings*>(s)->sampling.wrapU = TextureWrapMode(i32(v)); }},
            {"wrapV", [](void* s, double v) { static_cast<TextureImportSettings*>(s)->sampling.wrapV = TextureWrapMode(i32(v)); }},
            {"wrapW", [](void* s, double v) { static_cast<TextureImportSettings*>(s)->sampling.wrapW = TextureWrapMode(i32(v)); }},
        };

        StringView Stem(StringView path)
        {
            usize slash = path.FindLast('/');
            StringView name = slash == StringView::npos ? path : path.Substr(slash + 1);
            usize dot = name.FindLast('.');
            return dot == StringView::npos ? name : name.Substr(0, dot);
        }

        StringView Extension(StringView path)
        {
            usize dot = path.FindLast('.');
            return dot == StringView::npos ? StringView() : path.Substr(dot + 1);
        }

        void* Decode(const AssetDecodeInput& in, String& error)
        {
            TextureImportSettings settings;
            ReadMetaFields(in.meta, "TextureImporter:", Span<const MetaField>(kTextureFields, sizeof(kTextureFields) / sizeof(kTextureFields[0])), &settings);
            image::TextureData data;
            if (!image::DecodeImage(in.bytes, data, error, Extension(in.path)))
                return nullptr;
            DecodedTexture* out = new DecodedTexture();
            out->name = String(Stem(in.path));
            if (!ImportTexture(Move(data), settings, *out, error))
            {
                delete out;
                return nullptr;
            }
            return out;
        }

        bool Integrate(void* decoded, AssetObjects& out, Array<String>& errors)
        {
            DecodedTexture& d = *static_cast<DecodedTexture*>(decoded);
            Texture2D* t = MakeTexture(d);
            if (!t)
            {
                errors.Push("the pixels could not become a texture");
                return false;
            }
            out.Add(MainAssetFileId(ClassId::Texture2D), t, true);
            return true;
        }
    }

    bool ImportTexture(image::TextureData&& data, const TextureImportSettings& settings, DecodedTexture& out, String& error)
    {
        // Max Size: uncompressed images shrink to fit.
        u32 w = data.width, h = data.height;
        if (settings.maxSize > 0 && !image::IsBlockFormat(data.format) && data.mips == 1)
        {
            image::FitToMaxSize(data.width, data.height, settings.maxSize, w, h);
            if (w != data.width || h != data.height)
            {
                image::TextureData small;
                if (!image::ResizeTexture(data, w, h, image::ResampleFilter::Mitchell, settings.srgb && !settings.normalMap, small, error))
                    return false;
                data = Move(small);
            }
        }
        if (!PrepareTextureData(data, error))
            return false;
        if (settings.mipmaps && data.mips == 1 && !image::IsBlockFormat(data.format) && (data.width > 1 || data.height > 1))
        {
            image::MipmapSettings mips;
            mips.srgb = settings.srgb && !settings.normalMap;
            mips.normalMap = settings.normalMap;
            image::TextureData chain;
            if (!image::GenerateMipmaps(data, mips, chain, error))
                return false;
            chain.colorSpace = data.colorSpace;
            data = Move(chain);
        }
        out.data = Move(data);
        out.settings = settings.sampling;
        out.linear = !settings.srgb || settings.normalMap;
        out.readable = settings.readable;
        return true;
    }

    Texture2D* MakeTexture(DecodedTexture& d)
    {
        Texture2D* t = Texture2D::CreatePrepared(Move(d.data), d.linear);
        if (!t)
            return nullptr;
        t->SetName(d.name);
        t->SetSettings(d.settings);
        t->SetReadable(d.readable);
        return t;
    }

    AssetLoader TextureAssetLoader()
    {
        AssetLoader l;
        l.name = "Texture";
        // Every format Helix.Image decodes.
        Array<image::ImageDecoder> decoders;
        image::ImageDecoders(decoders);
        for (const image::ImageDecoder& d : decoders)
            for (StringView e : d.extensions)
                l.extensions.Push(String(e));
        l.referenceType = kAssetTypeImported;
        l.decode = &Decode;
        l.integrate = &Integrate;
        l.release = [](void* d) { delete static_cast<DecodedTexture*>(d); };
        return l;
    }
}
