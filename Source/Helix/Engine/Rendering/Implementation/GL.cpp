#include <Helix/Engine/Rendering/GL.h>
#include <Helix/Engine/Rendering/Implementation/RenderCore.h>

namespace helix::engine
{
    Matrix4x4 GL::GetGPUProjectionMatrix(Matrix4x4 proj, bool renderIntoTexture) { return render::GpuProjection(proj, renderIntoTexture); }
}
