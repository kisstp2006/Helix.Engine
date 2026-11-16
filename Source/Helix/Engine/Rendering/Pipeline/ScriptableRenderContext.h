#pragma once

#include <Helix/Engine/Rendering/Pipeline/CommandBuffer.h>
#include <Helix/Engine/Rendering/Pipeline/Shadows.h>

// ScriptableRenderContext: what a pipeline renders through. Cull runs at once; the other calls record commands that
// Submit runs on the device in order (draws go to the target the engine renders the cameras into). A context lives
// for one call of the pipeline's Render; its culling results with it.
namespace helix::engine
{
    namespace render
    {
        struct ContextState;
    }

    class ScriptableRenderContext
    {
    public:
        ScriptableRenderContext();
        ~ScriptableRenderContext();
        ScriptableRenderContext(const ScriptableRenderContext&) = delete;
        ScriptableRenderContext& operator=(const ScriptableRenderContext&) = delete;

        // The renderers and lights inside the parameters' planes on their layers; the camera's renderers that are
        // inside get OnWillRenderObject. Invalid results (and an error) for an invalid plane count.
        CullingResults Cull(const ScriptableCullingParameters& parameters);
        // The culled renderers (and Graphics.RenderMesh's meshes) the filter lets through, with their materials' passes
        // of the settings' pass names (a pass without a LightMode tag is "SRPDefaultUnlit"), sorted by the settings.
        void DrawRenderers(CullingResults cullingResults, const DrawingSettings& drawingSettings, const FilteringSettings& filteringSettings);
        void DrawRenderers(CullingResults cullingResults, const DrawingSettings& drawingSettings, const FilteringSettings& filteringSettings,
                           const RenderStateBlock& stateBlock);
        // What DrawRenderers would draw, as a list command buffers draw (valid while the context renders).
        RendererUtils::RendererList CreateRendererList(const RendererListParams& parameters);
        RendererUtils::RendererList CreateRendererList(const RendererUtils::RendererListDesc& desc);
        // The skybox of RenderSettings around the camera (an empty list without one): drawn where the camera clears to
        // the skybox, after the opaques (the skybox shaders draw at the far plane).
        RendererUtils::RendererList CreateSkyboxRendererList(Camera* camera);
        // The camera's skybox list drawn where the commands before it ran.
        void DrawSkybox(Camera* camera);
        // The shadow casters of each split of each light: those inside the split's culling planes (the lists made
        // from these culling results after this draw only them).
        void CullShadowCasters(CullingResults cullingResults, const ShadowCastersCullingInfos& infos);
        // The ShadowCaster passes of a visible light's shadow casters (of the split, after CullShadowCasters).
        RendererUtils::RendererList CreateShadowRendererList(const ShadowDrawingSettings& settings);
        // One more state of a tag value for a list of this context.
        void AddRendererListState(RendererUtils::RendererList list, const TaggedState& state);
        // Native render passes: the attachments of the pass, each subpass's colors (and the depth) as the target. The
        // devices of this milestone draw each subpass as a render pass of its own; attachments without a target are
        // temporary textures of the pass; inputs are the textures of earlier subpasses (as globals _HelixInput0..n).
        void BeginRenderPass(i32 width, i32 height, i32 samples, Span<const AttachmentDescriptor> attachments, i32 depthAttachmentIndex = -1);
        void BeginSubPass(Span<const i32> colors, Span<const i32> inputs = {}, bool isDepthReadOnly = false);
        void EndSubPass();
        void EndRenderPass();
        // The camera's matrices, position, main light and ambient into the camera block, its viewport.
        void SetupCameraProperties(Camera* camera, bool stereoSetup = false);
        // A copy of the buffer's commands after the context's.
        void ExecuteCommandBuffer(const CommandBuffer& commandBuffer);
        // The devices have one queue: the commands run in order with the others.
        void ExecuteCommandBufferAsync(const CommandBuffer& commandBuffer, ComputeQueueType) { ExecuteCommandBuffer(commandBuffer); }
        // OnRenderObject of every script, where the commands before it ran.
        void InvokeOnRenderObjectCallback();
        // Runs the recorded commands on the device.
        void Submit();

        // ---------------------------------------------------------------------------------------------- native
        // The cameras of the Render call.
        Span<Camera* const> cameras() const { return Span<Camera* const>(cameras_.Data(), cameras_.Size()); }
        void SetCameras(Span<Camera* const> cameras);
        // The id scripts hold; a live context of an id.
        u64 Id() const { return id_; }
        static ScriptableRenderContext* Find(u64 id);
        // Whether culling results came from a context that still renders.
        static bool IsLive(const render::CullingData* data);
        // What the engine's own pipelines record beyond the public calls.
        render::CommandList& MutableList() { return list_; }

    private:
        render::ContextState* state_ = nullptr;
        render::CommandList list_;
        Array<render::CullingData*> culling_ = Array<render::CullingData*>();
        Array<Camera*> cameras_ = Array<Camera*>();
        Array<u64> lists_ = Array<u64>();
        // The native render pass being recorded.
        Array<AttachmentDescriptor> passAttachments_ = Array<AttachmentDescriptor>();
        Array<u8> passStarted_ = Array<u8>();
        i32 passWidth_ = 0, passHeight_ = 0, passSamples_ = 1, passDepth_ = -1;
        bool inRenderPass_ = false, inSubPass_ = false;
        u64 id_ = 0;
    };
}
