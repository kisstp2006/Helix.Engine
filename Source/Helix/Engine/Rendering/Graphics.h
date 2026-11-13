#pragma once

#include <Helix/Engine/Rendering/Camera.h>
#include <Helix/Engine/Rendering/Renderer.h>
#include <Helix/RHI/Device/Device.h>
#include <Helix/ShaderCompiler/Cache/ShaderCache.h>

// The renderer: every enabled camera by depth into the target the host gives, through the active render pipeline
// (Rendering/Pipeline: the asset's scriptable pipeline, or the fallback pipeline that draws each submesh with the first
// pass of its material that is no shadow, depth or meta pass). GPU meshes, textures, samplers, shaders and pipelines
// are cached by the versions of their sources.
namespace helix::engine
{
    class CommandBuffer;

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

    // How Graphics.RenderMesh draws a mesh (the reference's RenderParams with its constructor's defaults).
    struct RenderParams
    {
        Material* material = nullptr;
        // Null draws in every camera.
        Camera* camera = nullptr;
        const MaterialPropertyBlock* matProps = nullptr;
        // The layer index the cameras' culling masks test.
        i32 layer = 0;
        u32 renderingLayerMask = 1;
        i32 rendererPriority = 0;
        // Zero size: the mesh's bounds under the matrix.
        Bounds worldBounds;
        ShadowCastingMode shadowCastingMode = ShadowCastingMode::Off;
        bool receiveShadows = false;
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
        // The ambient light of the camera block (SetupCameraProperties).
        static void SetAmbientLight(Color color);

        // The player loop's FinishFrameRendering: renderer data brought up to date, then every enabled camera by depth.
        static void RenderFrame();
        // One camera now.
        static void RenderCamera(Camera& camera);
        static const RenderStats& LastStats();
        // Frees every GPU resource the renderer made.
        static void ReleaseResources();

        // A submesh drawn by the cameras of this frame (or by the one of the params), dropped once the frame is drawn.
        static void RenderMesh(const RenderParams& params, Mesh* mesh, i32 submeshIndex, const Matrix4x4& objectToWorld);
        // Meshes RenderMesh queued for the frame.
        static u32 QueuedMeshCount();
        // Runs a command buffer's commands now, into the target with the whole target as the viewport.
        static void ExecuteCommandBuffer(const CommandBuffer& buffer);
    };

    // RenderMesh for the script API: RenderParams holds objects, which bound structs cannot (C# has the struct).
    HX_CLASS(Namespace = "HelixEngine.Internal", Static, Internal)
    class GraphicsNative
    {
    public:
        HX_FUNCTION()
        static void RenderMesh(Material* material, Camera* camera, PropertyBlockId matProps, i32 layer, u32 renderingLayerMask, i32 rendererPriority,
                               Bounds worldBounds, ShadowCastingMode shadowCastingMode, bool receiveShadows, Mesh* mesh, i32 submeshIndex,
                               Matrix4x4 objectToWorld)
        {
            RenderParams p;
            p.material = material, p.camera = camera, p.matProps = MaterialPropertyBlock::Find(matProps), p.layer = layer;
            p.renderingLayerMask = renderingLayerMask, p.rendererPriority = rendererPriority, p.worldBounds = worldBounds;
            p.shadowCastingMode = shadowCastingMode, p.receiveShadows = receiveShadows;
            Graphics::RenderMesh(p, mesh, submeshIndex, objectToWorld);
        }
    };
}
