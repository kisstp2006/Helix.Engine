#pragma once

#include <Helix/Engine/Asset/AssetLoader.h>

namespace helix::engine
{
    struct AssetRuntimeAccess
    {
        static void Init(AssetObjects& out, const Guid& guid, StringView path);
    };
}
