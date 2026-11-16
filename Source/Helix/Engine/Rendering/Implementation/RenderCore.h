#pragma once

#include <Helix/Engine/Material/Material.h>
#include <Helix/Engine/Mesh/Mesh.h>
#include <Helix/Engine/Rendering/Graphics.h>
#include <Helix/Engine/Rendering/Pipeline/RenderStates.h>
#include <Helix/Engine/Texture/RenderTexture.h>

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
        // The index of the texture and sampler in the device's bindless table (registered on first use).
        mutable u32 bindless = ~0u;
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
        // DrawProcedural's instances (one draw with this instance count).
        u32 instances = 1;
    };

    // The GPU textures of a render texture: the sampled color (with its mips), the multisampled target it is resolved
    // from, the depth.
    struct GpuRenderTarget
    {
        u32 version = ~0u;
        rhi::TextureHandle color, msaa, depth;
        rhi::Format colorFormat = rhi::Format::Unknown, depthFormat = rhi::Format::Unknown;
        u32 width = 0, height = 0, samples = 1, layers = 1, mips = 1;
        GpuTexture sampled;
        // The texture draws go into: the multisampled one when there is one.
        rhi::TextureHandle Target() const { return msaa.IsValid() ? msaa : color; }
    };
    // Made (or made again) when the render texture changed; null without a device or for a texture it cannot make.
    const GpuRenderTarget* RenderTargetOf(RenderTexture& texture);
    // The device format of a graphics format (Unknown when the devices have none).
    rhi::Format DeviceFormat(GraphicsFormat format);

    // What pipelines are made for: the formats and samples of the bound target, and whether it is a texture (rows kept
    // bottom first: projection and winding are flipped).
    struct TargetFormats
    {
        rhi::Format colors[rhi::kMaxColorTargets]{};
        u32 colorCount = 1;
        rhi::Format depth = rhi::Format::Unknown;
        u32 samples = 1;
        bool flipped = false;
        // CommandBuffer.SetGlobalDepthBias: added to the depth bias of the draws' passes.
        i32 depthBias = 0;
        f32 depthBiasSlope = 0;
    };
    // The engine's target (the screen or the host's texture).
    TargetFormats ScreenFormats();

    // Render state a RenderStateBlock puts over a pass's own.
    struct StateOverride
    {
        RenderStateMask mask = RenderStateMask::Nothing;
        // Each target's (the first's for all without separateMRTBlendStates).
        rhi::BlendState blends[rhi::kMaxColorTargets];
        bool depthTest = true, depthWrite = true;
        rhi::CompareFunction depthCompare = rhi::CompareFunction::Less;
        rhi::CullMode cull = rhi::CullMode::Back;
        i32 depthBias = 0;
        f32 depthBiasSlope = 0;
        bool depthClip = true, conservative = false, alphaToCoverage = false;
        bool logicOpEnabled = false;
        rhi::LogicOp logicOp = rhi::LogicOp::Copy;
        rhi::StencilState stencil;
        u32 stencilReference = 0;
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
    // A texture's index in the device's bindless table (registered on first use; ~0 when the device has no bindless
    // textures or the texture cannot be made).
    u32 BindlessIndexOf(Texture& texture);
    // Bumped whenever a bindless index goes (a texture made again or released): blocks that hold indices are written again.
    u32 BindlessGeneration();
    // The pipeline of a variant for a mesh layout, a topology and a target (the screen's by default), with the
    // override's state over the pass's; passState: the pass's state with its material's values (`Cull [_Cull]`, the
    // variant's when null) and its key (0: the variant's state as it is).
    rhi::PipelineHandle PipelineOf(const ShaderVariant& variant, const GpuMesh& mesh, rhi::PrimitiveTopology topology, const StateOverride* state = nullptr,
                                   const TargetFormats* target = nullptr, const shadercompiler::RenderState* passState = nullptr, u64 passStateKey = 0);
    void Upload(const void* data, u32 size, rhi::TransientAllocation& out);

    // A part of a persistent uniform buffer (the SRP-batcher style path, PersistentBlocks.cpp).
    struct UniformPart
    {
        rhi::BufferHandle buffer;
        u32 offset = 0;
        u32 size = 0;
    };
    // The frame's start: frees what can be freed; on when the setting is and the device can bind by offset.
    void BeginPersistentBlocks(rhi::Device* device, u64 frame);
    bool PersistentBlocksEnabled();
    // The persistent parts of a pass's material blocks, written into `cl` when its values changed.
    Span<const UniformPart> MaterialBlockParts(rhi::CommandList& cl, const MaterialPassData& data);
    // The lights of a drawn object (PerObjectData LightData and LightIndices): data y is their count, the indices (in
    // the order of the culling's light index map) of up to eight lights by importance in two float4s. Zero without.
    struct ObjectLights
    {
        static constexpr u32 kMax = 8;
        f32 data[4] = {};
        f32 indices[kMax] = {};
        // The reflection probe of the object (PerObjectData.ReflectionProbes), as the shaders read it: its HDR decode
        // values, box max (w: blend distance), box min (w: weight), position (w: 1 for box projection). The default:
        // the scene's reflection at RenderSettings.reflectionIntensity, no box.
        f32 probe[16] = {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0};
        bool operator==(const ObjectLights&) const = default;
    };
    // The draw block: the object's matrices and its lights.
    constexpr u32 kDrawBlockBytes = u32(2 * sizeof(Matrix4x4) + sizeof(ObjectLights));
    // The persistent object block (object to world and back, the lights) of a renderer by its render data index.
    UniformPart ObjectBlockPart(rhi::CommandList& cl, u32 renderIndex, const ObjectLights& lights);
    void ReleasePersistentBlocks();
    // A mesh of `vertexCount` vertices without attributes, indexed in order, of a topology: what DrawProcedural draws
    // (its shaders take their vertices from the vertex id).
    Mesh* ProceduralMesh(MeshTopology topology, u32 vertexCount);
    // The magenta material of objects without a usable shader.
    Material* ErrorMaterial();
    // A projection of the reference (clip depth -1..1) as the device takes it (0..1); flipped upside down for a texture
    // target (render textures keep rows bottom first, as textures do).
    Matrix4x4 GpuProjection(const Matrix4x4& projection, bool intoTexture = false);

    // The light mode of a shader pass by the scriptable pipeline's rule: "SRPDefaultUnlit" without a LightMode tag.
    ShaderTagId PassTag(const shadercompiler::ShaderDescription& description, u32 descriptionPass);
    // The value of a pass tag ("LightMode" by the rule of PassTag; none when the pass has no such tag).
    ShaderTagId PassTagValue(const shadercompiler::ShaderDescription& description, u32 descriptionPass, ShaderTagId tagName);

    struct CullingData
    {
        // Indices into the render side's renderer data, and into the frame's queued meshes, in their order.
        Array<u32> renderers = Array<u32>();
        Array<u32> queued = Array<u32>();
        Array<VisibleLight> lights = Array<VisibleLight>();
        Array<VisibleReflectionProbe> probes = Array<VisibleReflectionProbe>();
        // The shadow casters of the visible lights: lights[i]'s are casters[casterStart[i] .. casterStart[i + 1]).
        Array<u32> casters = Array<u32>();
        Array<u32> casterStart = Array<u32>();
        EntityId camera;
        Vector3 origin;
        f32 shadowDistance = 0;
        f32 shadowNearPlaneOffset = 3;
        bool conservativeEnclosingSphere = false;
        i32 numIterationsEnclosingSphere = 64;
        // CullShadowCasters: per visible light the first of its splits in splitCasters (-1: not culled per split).
        Array<i32> lightSplits = Array<i32>();
        Array<Array<u32>> splitCasters = Array<Array<u32>>();
        // Per visible light its index in the per object light lists (-1: left out); empty: each light its own index.
        Array<i32> lightIndexMap = Array<i32>();
        // The lights of each renderer by render data index, made when a draw first asks (~0: not yet).
        mutable Array<u32> objectLightSlot = Array<u32>();
        mutable Array<ObjectLights> objectLights = Array<ObjectLights>();
        // The cube of each object's probe by the same slot (0: the scene's reflection, the global unity_SpecCube0).
        mutable Array<u64> objectProbeTextures = Array<u64>();
        // RenderSettings.reflectionIntensity when culled (the decode value of the scene's reflection).
        f32 reflectionIntensity = 1;
    };
    // The lights of an object of these bounds: directional lights first, then the point and spot lights reaching the
    // bounds by their color's brightness attenuated at the bounds; at most ObjectLights::kMax.
    ObjectLights LightsAt(const CullingData& culling, const Vector3& center, const Vector3& extent);
    // The reflection probe of an object of these bounds into its lights: the culled probes' first (by the culling's
    // order) whose bounds it touches; the cube's id (0: the scene's reflection).
    u64 ProbeAt(const CullingData& culling, const Vector3& center, const Vector3& extent, ObjectLights& lights);
    // The probe cube of a renderer whose lights ObjectLightsOf made.
    u64 ObjectProbeTextureOf(const CullingData& culling, u32 renderIndex);
    // A light's view matrix: from `eye` looking along `forward` (the view's -z), with these right and up axes.
    Matrix4x4 LightView(const Vector3& eye, const Vector3& right, const Vector3& up, const Vector3& forward);
    // Those of a renderer (by render data index), kept in the culling data.
    const ObjectLights& ObjectLightsOf(const CullingData& culling, u32 renderIndex);

    // A draw list entry: sorted by hi, then by lo's bits above the low 24, which hold the draw's index (the sort is
    // stable, so entries made in index order keep it among equal keys).
    struct SortEntry
    {
        static constexpr u32 kIndexBits = 24;
        static constexpr u64 kIndexMask = (1ull << kIndexBits) - 1;
        u64 hi;
        u64 lo;
        u32 item() const { return u32(lo & kIndexMask); }
    };
    // Stable LSD radix sort by (hi, lo above the index bits); large lists on the jobs.
    void RadixSort(Array<SortEntry>& entries, Array<SortEntry>& scratch);

    // The fallback pipeline: whether the first camera clears the whole target (RenderFrame's first camera does).
    bool ClearFirstCamera();

    struct CommandList;
    // Runs commands at once with the whole target as the viewport (Graphics.ExecuteCommandBuffer): the active render
    // texture's, else the screen's.
    void ExecuteNow(const CommandList& list, bool intoActive = true);
    // The materials DrawRenderers made for override shaders.
    void ReleaseSwappedMaterials();
}
