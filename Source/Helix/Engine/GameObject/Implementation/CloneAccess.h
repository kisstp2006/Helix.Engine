#pragma once

#include <Helix/Engine/GameObject/GameObject.h>

namespace helix::engine
{
    // Copies of hierarchies for the engine's own code (prefab instances in scene files).
    struct CloneAccess
    {
        // The copy of a GameObject's hierarchy as a new root of `scene`, its objects in clone order (each GameObject
        // before its components, parents before children) next to the originals; references inside remapped; not
        // named, placed or enabled.
        static bool Copy(GameObject* source, u32 scene, Array<Object*>& originals, Array<Object*>& clones);
        // Forgets the prefab template of a root (its asset changed or goes).
        static void ForgetTemplate(EntityId root);
    };
}
