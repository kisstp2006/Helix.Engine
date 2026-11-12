#pragma once

#include <Helix/Core/Types/Guid.h>

// LoadableSceneId: a stable id of a scene asset (the guid the asset catalog lists it under), which survives renames and
// moves of the scene file. SceneManager loads scenes by it and finds the scenes that came from it.
namespace helix::engine
{
    struct LoadableSceneId
    {
        Guid guid;

        // True when it names a scene asset (it may still be missing from the catalog).
        bool IsValid() const { return guid != Guid(); }
        bool operator==(const LoadableSceneId&) const = default;
    };
}
