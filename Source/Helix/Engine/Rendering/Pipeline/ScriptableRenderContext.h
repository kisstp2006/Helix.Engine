#pragma once

#include <Helix/Engine/Rendering/Pipeline/CommandBuffer.h>

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
        // The camera's matrices, position, main light and ambient into the camera block, its viewport.
        void SetupCameraProperties(Camera* camera, bool stereoSetup = false);
        // A copy of the buffer's commands after the context's.
        void ExecuteCommandBuffer(const CommandBuffer& commandBuffer);
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
        u64 id_ = 0;
    };
}
