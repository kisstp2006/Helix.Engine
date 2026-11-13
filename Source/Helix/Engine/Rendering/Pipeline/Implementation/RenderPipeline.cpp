#include <Helix/Engine/Rendering/Pipeline/RenderPipeline.h>
#include <Helix/Engine/Rendering/Implementation/RenderCore.h>

#include <Helix/Core/Logging/Log.h>
#include <Helix/Scripting/Bindings/Interop.h>

#include <bit>

namespace helix::engine
{
    namespace
    {
        LogChannel PipelineLog{"Engine"};

        struct PipelineState
        {
            EntityId defaultAsset, qualityAsset;
            u32 renderingLayerMask = 1;
            RenderPipeline* pipeline = nullptr;
            // The asset the pipeline was made of (none: the fallback), its script class's index (what the pipeline's type follows).
            EntityId asset;
            u32 script = ~0u;
            bool isFallback = false;
            bool reset = false;
            bool started = false;
            // A pipeline rendered before (asset, script and isFallback are what it was).
            bool rendered = false;
            RenderPipelineFactory factories[2] = {};
            Event<ScriptableRenderContext&, Span<Camera* const>> beginContext, endContext;
            Event<ScriptableRenderContext&, Camera*> beginCamera, endCamera;
            Event<ScriptableObject*, ScriptableObject*> assetChanged;
            Event<> typeChanged;
        };

        PipelineState& P()
        {
            static PipelineState s;
            return s;
        }

        // ------------------------------------------------------------------------------------------ fallback

        // Without an asset: each camera culled by its frustum, cleared by its flags (RenderFrame's first camera clears
        // the whole target), opaque queues front to back and transparent ones back to front with each material's
        // first pass that is no shadow, depth, meta or motion pass; the camera's scripts hear of each stage.
        class FallbackPipeline final : public RenderPipeline
        {
        public:
            void Render(ScriptableRenderContext& context, Span<Camera* const> cameras) override
            {
                bool first = render::ClearFirstCamera();
                const RenderTarget& target = render::Target();
                for (Camera* camera : cameras)
                {
                    Camera::SetCurrent(camera);
                    if (detail::gRenderStage)
                        detail::gRenderStage(camera, detail::RenderStage::PreCull);
                    ScriptableCullingParameters parameters;
                    if (!camera->TryGetCullingParameters(parameters))
                    {
                        Camera::SetCurrent(nullptr);
                        continue;
                    }
                    CullingResults culling = context.Cull(parameters);
                    if (detail::gRenderStage)
                        detail::gRenderStage(camera, detail::RenderStage::PreRender);

                    CameraClearFlags flags = camera->clearFlags();
                    Color bg = camera->backgroundColor();
                    Rect pr = camera->pixelRect();
                    // Skyboxes come with the pipelines: until then Skybox clears with the background color.
                    bool clearColor = flags == CameraClearFlags::SolidColor || flags == CameraClearFlags::Skybox;
                    bool clearDepth = flags != CameraClearFlags::Nothing;
                    bool whole = pr.x <= 0 && pr.y <= 0 && pr.width >= f32(target.width) && pr.height >= f32(target.height);
                    CommandBuffer clears;
                    if (first || (whole && (clearColor || clearDepth)))
                    {
                        clears.SetViewport(Rect{0, 0, f32(target.width), f32(target.height)});
                        clears.ClearRenderTarget(first || clearDepth, first || clearColor, bg);
                    }
                    context.ExecuteCommandBuffer(clears);
                    context.SetupCameraProperties(camera);
                    if (!whole && (clearColor || clearDepth))
                    {
                        CommandBuffer partial;
                        partial.ClearRenderTarget(clearDepth, clearColor, bg);
                        context.ExecuteCommandBuffer(partial);
                    }
                    SortingSettings sorting(camera);
                    // Along the view direction for every camera (as this pipeline always sorted).
                    sorting.distanceMetric = DistanceMetric::Orthographic;
                    for (u32 pass = 0; pass < 2; pass++)
                    {
                        render::CommandList& list = context.MutableList();
                        render::Command& c = list.Add(render::Op::DrawRenderers);
                        c.index = u32(list.draws.Size());
                        render::DrawRequest& r = list.draws.Emplace();
                        r.culling = culling.data;
                        r.drawing.sortingSettings = sorting;
                        r.drawing.sortingSettings.criteria = pass == 0 ? SortingCriteria::CommonOpaque : SortingCriteria::CommonTransparent;
                        r.filtering = FilteringSettings(pass == 0 ? RenderQueueRange::opaque() : RenderQueueRange::transparent());
                        r.forward = true;
                    }
                    context.Submit();
                    if (detail::gRenderStage)
                    {
                        detail::gRenderStage(camera, detail::RenderStage::PostRender);
                        detail::gRenderStage(nullptr, detail::RenderStage::RenderObject);
                    }
                    Camera::SetCurrent(nullptr);
                    first = false;
                }
            }
        };

        // ------------------------------------------------------------------------------------------ factories

        // C++ assets: the script's `RenderPipeline* CreatePipeline()`.
        RenderPipeline* CreateNative(ScriptableObject& asset)
        {
            const ScriptClass* s = asset.script();
            for (const scripting::NativeScriptMethod& m : s->methods)
                if (m.name == StringView("CreatePipeline") && m.callPointer)
                {
                    if (m.resultType != StringView("RenderPipeline"))
                    {
                        HX_LOG_ERROR(PipelineLog, "{}.CreatePipeline answers a {}*; a pipeline asset's CreatePipeline answers a RenderPipeline*.", s->name,
                                     m.resultType);
                        return nullptr;
                    }
                    return static_cast<RenderPipeline*>(m.callPointer(static_cast<Object*>(&asset)));
                }
            HX_LOG_ERROR(PipelineLog, "{} is no pipeline asset: it has no RenderPipeline* CreatePipeline().", s->name);
            return nullptr;
        }

        // C# pipelines: the C# side's functions (HxEngineRendering_SetManaged).
        struct Managed
        {
            u64 (*create)(u64 assetHandle) = nullptr;
            void (*render)(u64 pipeline, u64 context) = nullptr;
            void (*dispose)(u64 pipeline) = nullptr;
            // An event of the manager: its kind; the context's id (0 for the asset and type events).
            void (*raise)(u32 kind, u64 context) = nullptr;
        };

        Managed& M()
        {
            static Managed m;
            return m;
        }

        class ManagedPipeline final : public RenderPipeline
        {
        public:
            explicit ManagedPipeline(u64 handle) : handle_(handle) {}
            void Render(ScriptableRenderContext& context, Span<Camera* const>) override
            {
                if (handle_ && M().render)
                    M().render(handle_, context.Id());
            }

        protected:
            void Dispose(bool) override
            {
                if (handle_ && M().dispose)
                    M().dispose(handle_);
                handle_ = 0;
            }

        private:
            u64 handle_;
        };

        RenderPipeline* CreateManaged(ScriptableObject& asset)
        {
            u64 wrapper = scripting::ObjectHandles<Object>::WrapperOf(&asset);
            if (!M().create || !wrapper)
            {
                HX_LOG_ERROR(PipelineLog, "Pipeline asset '{}': its C# object is missing.", asset.Name());
                return nullptr;
            }
            u64 handle = M().create(wrapper);
            return handle ? new ManagedPipeline(handle) : nullptr;
        }

        void Release(RenderPipeline* p)
        {
            if (!p)
                return;
            p->Dispose();
            delete p;
        }

        ScriptableObject* AssetOf(EntityId id) { return EntityIdToObject<ScriptableObject>(id); }

        // The pipeline that renders now: the current asset's (made when it changed or was reset), else the fallback.
        RenderPipeline* EnsurePipeline()
        {
            PipelineState& s = P();
            ScriptableObject* asset = GraphicsSettings::currentRenderPipeline();
            EntityId id = asset ? asset->GetEntityId() : EntityId();
            if (s.pipeline && !s.reset && id == s.asset && (asset || s.isFallback))
                return s.pipeline;
            // What rendered before (kept when the pipeline was let go): the events compare with it.
            ScriptableObject* old = s.rendered && !s.isFallback ? AssetOf(s.asset) : nullptr;
            u32 oldScript = s.isFallback ? ~0u : s.script;
            bool renderedBefore = s.rendered;
            Release(s.pipeline);
            s.pipeline = nullptr;
            s.reset = false;
            if (asset)
            {
                const ScriptClass* script = asset->script();
                RenderPipelineFactory factory = script ? s.factories[u32(script->language)] : nullptr;
                if (!script)
                    HX_LOG_ERROR(PipelineLog, "Pipeline asset '{}' has no script.", asset->Name());
                else if (!factory)
                    HX_LOG_ERROR(PipelineLog, "Pipeline asset '{}': its language's pipelines are not available.", asset->Name());
                s.pipeline = factory ? factory(*asset) : nullptr;
            }
            s.isFallback = s.pipeline == nullptr;
            if (s.isFallback)
                s.pipeline = new FallbackPipeline();
            s.asset = id;
            s.script = s.isFallback ? ~0u : asset->script()->index;
            s.rendered = true;
            ScriptableObject* now = s.isFallback ? nullptr : asset;
            if (old != now)
                s.assetChanged.Invoke(old, now);
            if (!renderedBefore || oldScript != s.script)
                s.typeChanged.Invoke();
            return s.pipeline;
        }

        // ------------------------------------------------------------------------------------------ C# side

        // Kinds of RenderPipelineManager events the C# side raises (its RenderPipelineManager's order).
        enum class EventKind : u32
        {
            BeginContext,
            EndContext,
            BeginCamera,
            EndCamera,
            AssetChanged,
            TypeChanged,
        };

        // The cameras, the camera and the assets of the event the C# side hears of now.
        struct EventArgs
        {
            Span<Camera* const> cameras;
            Camera* camera = nullptr;
            ScriptableObject *from = nullptr, *to = nullptr;
        };

        EventArgs& Args()
        {
            static EventArgs a;
            return a;
        }

        void RaiseContext(void* kind, ScriptableRenderContext& context, Span<Camera* const> cameras)
        {
            EventArgs saved = Args();
            Args().cameras = cameras;
            M().raise(u32(uintptr_t(kind)), context.Id());
            Args() = saved;
        }

        void RaiseCamera(void* kind, ScriptableRenderContext& context, Camera* camera)
        {
            EventArgs saved = Args();
            Args().camera = camera;
            M().raise(u32(uintptr_t(kind)), context.Id());
            Args() = saved;
        }

        void RaiseAsset(void*, ScriptableObject* from, ScriptableObject* to)
        {
            EventArgs saved = Args();
            Args().from = from, Args().to = to;
            M().raise(u32(EventKind::AssetChanged), 0);
            Args() = saved;
        }

        void RaiseType(void*) { M().raise(u32(EventKind::TypeChanged), 0); }

        // The C# side's functions: create (asset handle to a pipeline handle), render, dispose, raise.
        u8 SetManaged(void** table, u32 count)
        {
            if (count < 4)
                return 0;
            Managed& m = M();
            bool first = m.create == nullptr;
            m.create = std::bit_cast<u64 (*)(u64)>(table[0]);
            m.render = std::bit_cast<void (*)(u64, u64)>(table[1]);
            m.dispose = std::bit_cast<void (*)(u64)>(table[2]);
            m.raise = std::bit_cast<void (*)(u32, u64)>(table[3]);
            SetRenderPipelineFactory(ScriptLanguage::CSharp, &CreateManaged);
            if (first)
            {
                PipelineState& s = P();
                s.beginContext.Add(&RaiseContext, reinterpret_cast<void*>(uintptr_t(EventKind::BeginContext)));
                s.endContext.Add(&RaiseContext, reinterpret_cast<void*>(uintptr_t(EventKind::EndContext)));
                s.beginCamera.Add(&RaiseCamera, reinterpret_cast<void*>(uintptr_t(EventKind::BeginCamera)));
                s.endCamera.Add(&RaiseCamera, reinterpret_cast<void*>(uintptr_t(EventKind::EndCamera)));
                s.assetChanged.Add(&RaiseAsset);
                s.typeChanged.Add(&RaiseType);
            }
            return 1;
        }

        void Start()
        {
            PipelineState& s = P();
            if (s.started)
                return;
            s.started = true;
            s.factories[u32(ScriptLanguage::Cpp)] = &CreateNative;
        }
    }

    // ---------------------------------------------------------------------------------------------- RenderPipeline

    void RenderPipeline::Dispose()
    {
        if (disposed_)
            return;
        disposed_ = true;
        Dispose(true);
    }

    void RenderPipeline::BeginContextRendering(ScriptableRenderContext& context, Span<Camera* const> cameras) { P().beginContext.Invoke(context, cameras); }
    void RenderPipeline::EndContextRendering(ScriptableRenderContext& context, Span<Camera* const> cameras) { P().endContext.Invoke(context, cameras); }
    void RenderPipeline::BeginCameraRendering(ScriptableRenderContext& context, Camera* camera) { P().beginCamera.Invoke(context, camera); }
    void RenderPipeline::EndCameraRendering(ScriptableRenderContext& context, Camera* camera) { P().endCamera.Invoke(context, camera); }

    Event<ScriptableRenderContext&, Span<Camera* const>>& RenderPipelineManager::beginContextRendering() { return P().beginContext; }
    Event<ScriptableRenderContext&, Span<Camera* const>>& RenderPipelineManager::endContextRendering() { return P().endContext; }
    Event<ScriptableRenderContext&, Camera*>& RenderPipelineManager::beginCameraRendering() { return P().beginCamera; }
    Event<ScriptableRenderContext&, Camera*>& RenderPipelineManager::endCameraRendering() { return P().endCamera; }
    Event<ScriptableObject*, ScriptableObject*>& RenderPipelineManager::activeRenderPipelineAssetChanged() { return P().assetChanged; }
    Event<>& RenderPipelineManager::activeRenderPipelineTypeChanged() { return P().typeChanged; }
    RenderPipeline* RenderPipelineManager::currentPipeline() { return P().pipeline; }
    ScriptableObject* RenderPipelineManager::currentAsset() { return P().isFallback ? nullptr : AssetOf(P().asset); }

    void SetRenderPipelineFactory(ScriptLanguage language, RenderPipelineFactory factory)
    {
        Start();
        P().factories[u32(language)] = factory;
    }

    void ResetRenderPipeline(ScriptableObject* asset)
    {
        if (asset && asset->GetEntityId() == P().asset)
            P().reset = true;
    }

    // ---------------------------------------------------------------------------------------------- settings

    ScriptableObject* GraphicsSettings::defaultRenderPipeline() { return AssetOf(P().defaultAsset); }
    void GraphicsSettings::SetDefaultRenderPipeline(ScriptableObject* asset) { P().defaultAsset = asset ? asset->GetEntityId() : EntityId(); }

    ScriptableObject* GraphicsSettings::currentRenderPipeline()
    {
        ScriptableObject* q = QualitySettings::renderPipeline();
        return q ? q : defaultRenderPipeline();
    }

    u32 GraphicsSettings::defaultRenderingLayerMask() { return P().renderingLayerMask; }
    void GraphicsSettings::SetDefaultRenderingLayerMask(u32 mask) { P().renderingLayerMask = mask; }

    ScriptableObject* QualitySettings::renderPipeline() { return AssetOf(P().qualityAsset); }
    void QualitySettings::SetRenderPipeline(ScriptableObject* asset) { P().qualityAsset = asset ? asset->GetEntityId() : EntityId(); }

    ScriptableObject* QualitySettings::GetRenderPipelineAssetAt(i32 index)
    {
        if (index < 0 || index >= levelCount())
        {
            HX_LOG_ERROR(PipelineLog, "QualitySettings.GetRenderPipelineAssetAt: there is no quality level {}.", index);
            return nullptr;
        }
        return renderPipeline();
    }

    // ---------------------------------------------------------------------------------------------- rendering

    namespace render
    {
        void RenderWithPipeline(Span<Camera* const> cameras, bool)
        {
            Start();
            RenderPipeline* pipeline = EnsurePipeline();
            ScriptableRenderContext context;
            context.SetCameras(cameras);
            pipeline->Render(context, cameras);
            Stats().cameras += u32(cameras.Size());
            Camera::SetCurrent(nullptr);
        }

        void ReleasePipeline()
        {
            PipelineState& s = P();
            Release(s.pipeline);
            s.pipeline = nullptr;
            ReleaseSwappedMaterials();
        }
    }

    // ---------------------------------------------------------------------------------------------- the C# side's

    namespace detail
    {
        Span<Camera* const> PipelineEventCameras() { return Args().cameras; }
        Camera* PipelineEventCamera() { return Args().camera; }
        ScriptableObject* PipelineEventAsset(bool to) { return to ? Args().to : Args().from; }

        void RegisterRenderPipelineExports()
        {
            Start();
            scripting::RegisterNativeExport("HxEngineRendering_SetManaged", std::bit_cast<const void*>(&SetManaged));
        }
    }
}
