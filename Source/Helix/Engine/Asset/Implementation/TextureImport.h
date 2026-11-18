#pragma once

#include <Helix/Engine/Texture/Texture.h>
#include <Helix/Math/Geometry.h>

namespace helix::engine
{
    // The texture importer settings the loaders follow (from a .meta, or the defaults of a new texture).
    struct TextureImportSettings
    {
        bool mipmaps = true;
        bool srgb = true;
        bool readable = false;
        bool normalMap = false;
        u32 maxSize = 2048;
        TextureSettings sampling;
    };

    // A sprite of an imported texture (its importer's sprite mode): its rectangle in pixels, normalized pivot,
    // border, pixels per unit, mesh type and fileID.
    struct SpriteImport
    {
        String name;
        Rect rect;
        Vector2 pivot = Vector2(0.5f, 0.5f);
        Vector4 border = Vector4(0, 0, 0, 0);
        f32 pixelsPerUnit = 100;
        i32 meshType = 1;
        i64 fileID = 0;
        // The rectangle is the whole texture (single mode, known once it is decoded).
        bool whole = false;
    };

    // A texture made ready on a job.
    struct DecodedTexture
    {
        image::TextureData data;
        TextureSettings settings;
        String name;
        bool linear = false;
        bool readable = false;
        Array<SpriteImport> sprites;
        // The decoded image's size (sprite rectangles are in it).
        u32 sourceWidth = 0, sourceHeight = 0;
    };

    // The sprites a .meta's TextureImporter asks for (texture type Sprite: single or multiple mode).
    void ReadSpriteImports(Span<const u8> meta, StringView textureName, Array<SpriteImport>& out);

    // Any thread: max size, the layout of a Texture2D, mips.
    bool ImportTexture(image::TextureData&& data, const TextureImportSettings& settings, DecodedTexture& out, String& error);
    // Main thread.
    Texture2D* MakeTexture(DecodedTexture& d);
}
