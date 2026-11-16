#pragma once

#include <Helix/Engine/Rendering/Graphics.h>
#include <Helix/Engine/Texture/RenderTexture.h>

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

    // The graphics API of a device (the reference's values; the consoles' are left out).
    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class GraphicsDeviceType : i32
    {
        Direct3D11 = 2,
        Null = 4,
        OpenGLES2 = 8,
        OpenGLES3 = 11,
        Metal = 16,
        OpenGLCore = 17,
        Direct3D12 = 18,
        Vulkan = 21,
    };

    HX_CLASS(Namespace = "HelixEngine", Static)
    class SystemInfo
    {
    public:
        // The API of the engine's device (OpenGL ES 3 for OpenGL ES and WebGL 2; Null without a device).
        HX_PROPERTY(ReadOnly) static GraphicsDeviceType graphicsDeviceType()
        {
            rhi::Device* d = Graphics::Device();
            if (!d)
                return GraphicsDeviceType::Null;
            switch (d->Caps().backend)
            {
            case rhi::Backend::D3D11:
                return GraphicsDeviceType::Direct3D11;
            case rhi::Backend::OpenGL:
                return GraphicsDeviceType::OpenGLCore;
            default:
                return GraphicsDeviceType::OpenGLES3;
            }
        }
        // MultiThreaded where a render thread owns the device, else Direct.
        HX_PROPERTY(ReadOnly) static RenderingThreadingMode renderingThreadingMode()
        {
            rhi::Device* d = Graphics::Device();
            return d && d->Threaded() ? RenderingThreadingMode::MultiThreaded : RenderingThreadingMode::Direct;
        }
        // Whether render textures of the format can be made on the device (a format the device has).
        HX_FUNCTION() static bool SupportsRenderTextureFormat(RenderTextureFormat format);
    };
}
