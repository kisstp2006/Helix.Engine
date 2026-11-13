#pragma once

#include <Helix/Engine/Material/Material.h>
#include <Helix/Engine/Mesh/Mesh.h>
#include <Helix/Engine/Rendering/Graphics.h>
#include <Helix/Engine/Rendering/Pipeline/RenderStates.h>

// What the renderer's parts share inside the engine: the GPU forms of meshes, textures and pipelines (cached by the
// versions of their sources), the target, Graphics.RenderMesh's meshes of the frame, and culling results.
namespace helix::engine::render
{
    inline constexpr u32 kNone = ~0u;

    struct GpuMesh
    {
        u32 version = ~0u;
        rhi::BufferHandle vertices;
        u32 offsets[kVertexAttributeCount];
        u32 strides[kVertexAttributeCount];
        rhi::VertexFormat formats[kVertexAttributeCount];
        rhi::BufferHandle indices;
        rhi::BufferHandle zeros;
        u32 vertexCount = 0;
        // Each submesh as the GPU draws it (quads made triangles); count 0 skips it.
        struct Sub
        {
            u32 start, count;
            i32 baseVertex;
            rhi::PrimitiveTopology topology;
        };
        Array<Sub> subs = Array<Sub>();
    };

    struct GpuTexture
    {
        u32 version = ~0u;
        rhi::TextureHandle texture;
        rhi::SamplerHandle sampler;
    };

    // Graphics.RenderMesh's meshes for the frame.
    struct Queued
    {
        EntityId mesh, material, camera;
        u32 submesh = 0;
        u32 layerMask = 1;
        u32 renderingLayerMask = 1;
        Matrix4x4 world;
        Bounds bounds;
        bool hasBlock = false;
        PropertySheet block;
    };

    // Render state a RenderStateBlock puts over a pass's own.
    struct StateOverride
    {
        RenderStateMask mask = RenderStateMask::Nothing;
        rhi::BlendState blend;
        bool depthTest = true, depthWrite = true;
        rhi::CompareFunction depthCompare = rhi::CompareFunction::Less;
        rhi::CullMode cull = rhi::CullMode::Back;
        u64 hash = 0;
    };
    StateOverride MakeOverride(const RenderStateBlock& block);

    rhi::Device* Device();
    const RenderTarget& Target();
    Color Ambient();
    RenderStats& Stats();
    Span<const Queued> QueuedMeshes();

    const GpuMesh* MeshOf(Mesh& mesh);
    const GpuTexture* TextureOf(Texture& texture);
    // The pipeline of a variant for a mesh layout, a topology and the target, with the override's state over the pass's.
    rhi::PipelineHandle PipelineOf(const ShaderVariant& variant, const GpuMesh& mesh, rhi::PrimitiveTopology topology, const StateOverride* state = nullptr);
    void Upload(const void* data, u32 size, rhi::TransientAllocation& out);
    // The magenta material of objects without a usable shader.
    Material* ErrorMaterial();
    // A projection of the reference (clip depth -1..1) as the device takes it (0..1).
    Matrix4x4 GpuProjection(const Matrix4x4& projection);

    // The light mode of a shader pass by the scriptable pipeline's rule: "SRPDefaultUnlit" without a LightMode tag.
    ShaderTagId PassTag(const shadercompiler::ShaderDescription& description, u32 descriptionPass);

    struct CullingData
    {
        // Indices into the render side's renderer data, and into the frame's queued meshes, in their order.
        Array<u32> renderers = Array<u32>();
        Array<u32> queued = Array<u32>();
        Array<VisibleLight> lights = Array<VisibleLight>();
        EntityId camera;
        Vector3 origin;
    };

    // The fallback pipeline: whether the first camera clears the whole target (RenderFrame's first camera does).
    bool ClearFirstCamera();

    struct CommandList;
    // Runs commands at once with the whole target as the viewport (Graphics.ExecuteCommandBuffer).
    void ExecuteNow(const CommandList& list);
    // The materials DrawRenderers made for override shaders.
    void ReleaseSwappedMaterials();
}
