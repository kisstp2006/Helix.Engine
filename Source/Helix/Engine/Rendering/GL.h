#pragma once

#include <Helix/Math/Matrix4x4.h>
#include <Helix/Scripting/Bindings/Tags.h>

// The low level graphics calls of the script API (GL); for now the projection the device draws with.
namespace helix::engine
{
    HX_CLASS(Namespace = "HelixEngine", Static)
    class GL
    {
    public:
        // A camera's projection (clip depth -1 to 1) as the device takes it (depth 0 to 1), flipped upside down when
        // it draws into a texture (render textures keep their rows bottom first).
        HX_FUNCTION() static Matrix4x4 GetGPUProjectionMatrix(Matrix4x4 proj, bool renderIntoTexture);
    };
}
