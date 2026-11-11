#pragma once

#include <Helix/Engine/Rendering/Camera.h>
#include <Helix/RHI/Device/Device.h>
#include <Helix/ShaderCompiler/Cache/ShaderCache.h>

// The placeholder renderer (until the pipelines of M7): every enabled camera by depth into the target the host gives,
// renderers culled by frustum and culling mask, opaque queues front to back and transparent ones back to front, each
// submesh drawn with the first pass of its material that is no shadow, depth or meta pass. GPU meshes, textures,
// samplers, shaders and pipelines are cached by the versions of their sources.
namespace helix::engine
{
    // Where cameras draw: textures of the device, or the swap chain when `color` is not valid.
    struct RenderTarget
    {
        rhi::TextureHandle color;
        rhi::TextureHandle depth;
        u32 width = 0, height = 0;
        rhi::Format colorFormat = rhi::Format::RGBA8Unorm;
        rhi::Format depthFormat = rhi::Format::Unknown;
    };

    struct RenderStats
    {
        u32 cameras = 0;
        // Renderers that passed culling, summed over the cameras.
        u32 visible = 0;
        u32 drawCalls = 0;
        // Pipelines and GPU resources made this frame.
        u32 pipelinesCreated = 0;
        u32 uploads = 0;
    };

    class Graphics
    {
    public:
        // The device the renderer draws with; null draws nothing. Setting another device drops the GPU caches.
        static void SetDevice(rhi::Device* device);
        static rhi::Device* Device();
        // The shader target of the device's backend and version.
        static shadercompiler::ShaderTarget ShaderTarget();
        // The default target of cameras (its size is the display size of Camera).
        static void SetTarget(const RenderTarget& target);
        static const RenderTarget& Target();
        // The ambient light of the placeholder renderer.
        static void SetAmbientLight(Color color);

        // The player loop's FinishFrameRendering: renderer data brought up to date, then every enabled camera by depth.
        static void RenderFrame();
        // One camera now.
        static void RenderCamera(Camera& camera);
        static const RenderStats& LastStats();
        // Frees every GPU resource the renderer made.
        static void ReleaseResources();
    };
}
