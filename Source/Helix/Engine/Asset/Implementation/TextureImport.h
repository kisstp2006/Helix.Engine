#pragma once

#include <Helix/Engine/Texture/Texture.h>

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

    // A texture made ready on a job.
    struct DecodedTexture
    {
        image::TextureData data;
        TextureSettings settings;
        String name;
        bool linear = false;
        bool readable = false;
    };

    // Any thread: max size, the layout of a Texture2D, mips.
    bool ImportTexture(image::TextureData&& data, const TextureImportSettings& settings, DecodedTexture& out, String& error);
    // Main thread.
    Texture2D* MakeTexture(DecodedTexture& d);
}
