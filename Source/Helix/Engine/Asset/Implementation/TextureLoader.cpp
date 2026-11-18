#include <Helix/Engine/Asset/AssetLoader.h>
#include <Helix/Engine/Asset/Implementation/MetaFields.h>
#include <Helix/Engine/Asset/Implementation/TextureImport.h>
#include <Helix/Engine/Sprite/Sprite.h>
#include <Helix/Engine/Texture/Texture.h>
#include <Helix/Serialization/Yaml/Parser.h>

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
            ReadSpriteImports(in.meta, out->name.View(), out->sprites);
            u32 width = data.width, height = data.height;
            for (SpriteImport& s : out->sprites)
                if (s.whole)
                    s.rect = Rect(0, 0, f32(width), f32(height));
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
            // Its sprites (sized in the texture as imported: a texture shrunk by max size scales them).
            f32 kx = d.sourceWidth ? f32(t->width()) / f32(d.sourceWidth) : 1, ky = d.sourceHeight ? f32(t->height()) / f32(d.sourceHeight) : 1;
            for (const SpriteImport& s : d.sprites)
            {
                auto* sprite = NewObject<Sprite>();
                sprite->SetName(s.name.View());
                Rect r(s.rect.x * kx, s.rect.y * ky, s.rect.width * kx, s.rect.height * ky);
                sprite->Set(t, r, s.pivot, s.pixelsPerUnit, 0, SpriteMeshType(s.meshType), s.border);
                out.Add(s.fileID, sprite, false);
            }
            return true;
        }
    }

    namespace
    {
        bool Float(const serialization::yaml::Tree& t, u32 mapping, StringView key, f32& out)
        {
            double v;
            u32 n = t.Find(mapping, key);
            if (!t.IsScalar(n) || !t.AsFloat(n, v))
                return false;
            out = f32(v);
            return true;
        }

        Vector2 Vec2(const serialization::yaml::Tree& t, u32 node, Vector2 fallback)
        {
            if (!t.IsMapping(node))
                return fallback;
            Float(t, node, "x", fallback.x);
            Float(t, node, "y", fallback.y);
            return fallback;
        }

        // The reference's sprite alignments: center, the corners and edges' middles (custom: the pivot given).
        Vector2 Alignment(i32 a, Vector2 custom)
        {
            static const Vector2 kPivots[] = {Vector2(0.5f, 0.5f), Vector2(0, 1), Vector2(0.5f, 1), Vector2(1, 1), Vector2(0, 0.5f),
                                              Vector2(1, 0.5f), Vector2(0, 0), Vector2(0.5f, 0), Vector2(1, 0)};
            return a >= 0 && a < 9 ? kPivots[a] : custom;
        }
    }

    void ReadSpriteImports(Span<const u8> meta, StringView textureName, Array<SpriteImport>& out)
    {
        out.Clear();
        if (meta.Empty())
            return;
        using namespace serialization::yaml;
        String text("---\n");
        text += StringView(reinterpret_cast<const char*>(meta.Data()), meta.Size());
        Tree tree;
        String error;
        if (!Parse(text.View(), tree, error) || tree.documents.Empty())
            return;
        u32 importer = tree.Find(tree.documents[0].root, "TextureImporter");
        if (!tree.IsMapping(importer))
            return;
        f32 type = 0, mode = 1, ppu = 100, meshType = 1, alignment = 0;
        Float(tree, importer, "textureType", type);
        if (i32(type) != 8)
            return;
        Float(tree, importer, "spriteMode", mode);
        Float(tree, importer, "spritePixelsToUnits", ppu);
        Float(tree, importer, "spriteMeshType", meshType);
        Float(tree, importer, "alignment", alignment);
        auto border = [&](u32 mapping) {
            Vector4 b(0, 0, 0, 0);
            u32 n = tree.Find(mapping, "spriteBorder");
            if (!tree.IsMapping(n))
                n = tree.Find(mapping, "border");
            if (tree.IsMapping(n))
                Float(tree, n, "x", b.x), Float(tree, n, "y", b.y), Float(tree, n, "z", b.z), Float(tree, n, "w", b.w);
            return b;
        };
        if (i32(mode) == 1)
        {
            SpriteImport s;
            s.name = String(textureName);
            s.whole = true;
            s.pivot = Alignment(i32(alignment), Vec2(tree, tree.Find(importer, "spritePivot"), Vector2(0.5f, 0.5f)));
            s.border = border(importer);
            s.pixelsPerUnit = ppu;
            s.meshType = i32(meshType);
            s.fileID = MainAssetFileId(ClassId::Sprite);
            out.Push(Move(s));
            return;
        }
        // Multiple: the sprite sheet's rectangles.
        u32 sheet = tree.Find(importer, "spriteSheet");
        u32 sprites = tree.IsMapping(sheet) ? tree.Find(sheet, "sprites") : kNoNode;
        if (!tree.IsSequence(sprites))
            return;
        for (u32 i = 0; i < tree.Count(sprites); i++)
        {
            u32 e = tree.Item(sprites, i);
            if (!tree.IsMapping(e))
                continue;
            SpriteImport s;
            u32 name = tree.Find(e, "name");
            s.name = tree.IsScalar(name) ? String(tree.Text(name)) : String(textureName);
            u32 rect = tree.Find(e, "rect");
            if (tree.IsMapping(rect))
                Float(tree, rect, "x", s.rect.x), Float(tree, rect, "y", s.rect.y), Float(tree, rect, "width", s.rect.width), Float(tree, rect, "height", s.rect.height);
            f32 a = 0;
            Float(tree, e, "alignment", a);
            s.pivot = Alignment(i32(a), Vec2(tree, tree.Find(e, "pivot"), Vector2(0.5f, 0.5f)));
            s.border = border(e);
            s.pixelsPerUnit = ppu;
            s.meshType = i32(meshType);
            f32 id = 0;
            s.fileID = Float(tree, e, "internalID", id) && id != 0 ? i64(id) : MainAssetFileId(ClassId::Sprite) + 2 * i64(i);
            out.Push(Move(s));
        }
    }

    bool ImportTexture(image::TextureData&& data, const TextureImportSettings& settings, DecodedTexture& out, String& error)
    {
        out.sourceWidth = data.width;
        out.sourceHeight = data.height;
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
