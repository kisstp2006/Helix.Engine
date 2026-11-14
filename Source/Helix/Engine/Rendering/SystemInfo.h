#pragma once

#include <Helix/Engine/Rendering/Graphics.h>

// SystemInfo: what the engine runs on (the members of the rendering milestones).
namespace helix::engine
{
    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class RenderingThreadingMode : i32
    {
        Direct = 0,
        SingleThreaded = 1,
        MultiThreaded = 2,
        LegacyJobified = 3,
        NativeGraphicsJobs = 4,
        NativeGraphicsJobsWithoutRenderThread = 5,
        NativeGraphicsJobsSplitThreading = 6,
    };

    HX_CLASS(Namespace = "HelixEngine", Static)
    class SystemInfo
    {
    public:
        // MultiThreaded where a render thread owns the device, else Direct.
        HX_PROPERTY(ReadOnly) static RenderingThreadingMode renderingThreadingMode()
        {
            rhi::Device* d = Graphics::Device();
            return d && d->Threaded() ? RenderingThreadingMode::MultiThreaded : RenderingThreadingMode::Direct;
        }
    };
}
