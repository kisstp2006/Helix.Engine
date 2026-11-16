#pragma once

#include <Helix/Engine/Rendering/Graphics.h>
#include <Helix/Engine/Rendering/Pipeline/RenderPipeline.h>
#include <Helix/Platform/Window/Window.h>
#include <Helix/RHI/Device/Device.h>

#include <cstdio>
#include <cstdlib>

namespace helix::engine::testing
{
    inline const char* BackendName(rhi::Backend b)
    {
        switch (b)
        {
        case rhi::Backend::D3D11: return "D3D11";
        case rhi::Backend::OpenGL: return "OpenGL";
        case rhi::Backend::OpenGLES: return "OpenGL ES";
        default: return "WebGL 2";
        }
    }

    // Runs f on every device: each D3D11 adapter, OpenGL, WebGL 2 where there is a window.
    template <class F>
    void ForEachDevice(F&& f)
    {
        for (rhi::Backend b : {rhi::Backend::D3D11, rhi::Backend::OpenGL, rhi::Backend::WebGL2})
        {
            if ((b == rhi::Backend::WebGL2 && !platform::Window::IsSupported()) || !rhi::Device::IsBackendAvailable(b))
                continue;
            char name[128];
            u32 adapters = 0;
            while (b == rhi::Backend::D3D11 && rhi::Device::AdapterName(b, adapters, name, sizeof(name)))
                adapters++;
            for (i32 adapter = adapters ? 0 : -1; adapter < i32(adapters); adapter++)
            {
                // HELIX_RENDER_BATCHING=1: the persistent material and object blocks; HELIX_RENDER_THREAD=1: devices of the
                // threaded mode (a render thread owns the backend).
                static const bool batching = std::getenv("HELIX_RENDER_BATCHING") != nullptr;
                static const bool threaded = std::getenv("HELIX_RENDER_THREAD") != nullptr;
                GraphicsSettings::SetUseScriptableRenderPipelineBatching(batching);
                rhi::Device* device = rhi::Device::Create({.backend = b, .adapter = adapter, .vsync = false, .threaded = threaded});
                if (!device)
                    continue;
                if (adapter >= 0)
                    rhi::Device::AdapterName(b, u32(adapter), name, sizeof(name));
                std::printf("    %s %s\n", BackendName(b), adapter >= 0 ? name : "");
                f(*device);
                Graphics::SetDevice(nullptr);
                rhi::Device::Destroy(device);
            }
        }
    }
}
