#pragma once

#include <Helix/Engine/Event/Event.h>
#include <Helix/Engine/Rendering/Pipeline/ScriptableRenderContext.h>
#include <Helix/Engine/Script/ScriptableObject.h>

// The scriptable pipeline: a pipeline asset (a ScriptableObject whose script makes the pipeline: a C# class deriving
// from RenderPipelineAsset, or a C++ script with `RenderPipeline* CreatePipeline()`) set in GraphicsSettings or
// QualitySettings; the engine makes its pipeline once and calls its Render each frame with the cameras by depth.
// Without an asset the built-in fallback pipeline draws (the forward passes of every material, the camera messages).
namespace helix::engine
{
    class RenderPipeline
    {
    public:
        RenderPipeline() = default;
        virtual ~RenderPipeline() = default;
        RenderPipeline(const RenderPipeline&) = delete;
        RenderPipeline& operator=(const RenderPipeline&) = delete;

        bool disposed() const { return disposed_; }
        // The engine: Dispose(true) once, before the pipeline is deleted or let go.
        void Dispose();

        virtual void Render(ScriptableRenderContext& context, Span<Camera* const> cameras) = 0;

    protected:
        virtual void Dispose(bool disposing) { (void)disposing; }

    public:
        // The manager's events (pipelines call them around their work).
        static void BeginContextRendering(ScriptableRenderContext& context, Span<Camera* const> cameras);
        static void EndContextRendering(ScriptableRenderContext& context, Span<Camera* const> cameras);
        static void BeginCameraRendering(ScriptableRenderContext& context, Camera* camera);
        static void EndCameraRendering(ScriptableRenderContext& context, Camera* camera);

    private:
        bool disposed_ = false;
    };

    class RenderPipelineManager
    {
    public:
        static Event<ScriptableRenderContext&, Span<Camera* const>>& beginContextRendering();
        static Event<ScriptableRenderContext&, Span<Camera* const>>& endContextRendering();
        static Event<ScriptableRenderContext&, Camera*>& beginCameraRendering();
        static Event<ScriptableRenderContext&, Camera*>& endCameraRendering();
        // The asset that renders changed (the old and the new; null for the fallback pipeline).
        static Event<ScriptableObject*, ScriptableObject*>& activeRenderPipelineAssetChanged();
        // The pipeline's class changed (the fallback counts as one).
        static Event<>& activeRenderPipelineTypeChanged();
        // The pipeline rendering now (the fallback pipeline without an asset; null before the first frame).
        static RenderPipeline* currentPipeline();
        // The asset of the current pipeline (null for the fallback).
        static ScriptableObject* currentAsset();
    };

    // How the pipeline of an asset is made, by the language of the asset's script; null with an error when it makes none.
    using RenderPipelineFactory = RenderPipeline* (*)(ScriptableObject& asset);
    void SetRenderPipelineFactory(ScriptLanguage language, RenderPipelineFactory factory);
    // The pipeline of the asset goes (it is made again when it renders next): its asset's values changed or it was disabled.
    void ResetRenderPipeline(ScriptableObject* asset);

    class GraphicsSettings
    {
    public:
        // The asset when the quality level sets none (null: the fallback pipeline).
        static ScriptableObject* defaultRenderPipeline();
        static void SetDefaultRenderPipeline(ScriptableObject* asset);
        // The asset that renders: the quality level's, else the default.
        static ScriptableObject* currentRenderPipeline();
        static u32 defaultRenderingLayerMask();
        static void SetDefaultRenderingLayerMask(u32 mask);
    };

    class QualitySettings
    {
    public:
        // The asset of the current quality level (null: the default of GraphicsSettings).
        static ScriptableObject* renderPipeline();
        static void SetRenderPipeline(ScriptableObject* asset);
        // The asset of a quality level (there is one level until the quality levels come).
        static ScriptableObject* GetRenderPipelineAssetAt(i32 index);
        static i32 levelCount() { return 1; }
    };

    namespace render
    {
        // The pipeline renders the cameras (by depth) into the engine's target; the first one clears it when `clearFirst`.
        void RenderWithPipeline(Span<Camera* const> cameras, bool clearFirst);
        // Lets the pipeline go (the engine's end, a device change).
        void ReleasePipeline();
    }
}
