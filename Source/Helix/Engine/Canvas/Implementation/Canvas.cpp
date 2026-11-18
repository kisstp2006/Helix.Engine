#include <Helix/Engine/Canvas/Canvas.h>

#include <Helix/Engine/Canvas/CanvasRenderer.h>
#include <Helix/Engine/Material/Material.h>
#include <Helix/Engine/Rendering/Camera.h>
#include <Helix/Engine/Rendering/Graphics.h>
#include <Helix/Engine/Script/ScriptRuntime.h>
#include <Helix/Engine/Shader/Shader.h>
#include <Helix/Engine/Transform/RectTransform.h>
#include <Helix/Profiler/Profiler.h>

namespace helix::engine
{
    namespace canvas::detail
    {
        // The batcher's own (CanvasBatcher.cpp): a canvas's batches dropped.
        void Forget(Canvas& canvas);
        // The hierarchy under the canvas changed (scripts hear OnCanvasHierarchyChanged).
        void HierarchyChanged(Canvas& canvas);
    }

    namespace
    {
        Canvas::BatchingInterval gInterval = Canvas::BatchingInterval::GatedByRendering;

        Material* SharedMaterial(PPtr<Material>& slot, StringView shaderName)
        {
            if (Material* m = slot.Get())
                return m;
            Shader* shader = Shader::Find(shaderName);
            if (!shader)
                return nullptr;
            Material* m = Material::Create(shader);
            m->SetName(shaderName);
            m->hideFlags = HideFlags::HideAndDontSave;
            slot = PPtr<Material>(m);
            return m;
        }
    }

    // ------------------------------------------------------------------------------------------------------- Canvas

    Canvas::Canvas(const Canvas& other)
        : Behaviour(other), renderMode_(other.renderMode_), camera_(other.camera_), planeDistance_(other.planeDistance_), scaleFactor_(other.scaleFactor_),
          referencePixelsPerUnit_(other.referencePixelsPerUnit_), sortingGrid_(other.sortingGrid_), pixelPerfect_(other.pixelPerfect_),
          overridePixelPerfect_(other.overridePixelPerfect_), overrideSorting_(other.overrideSorting_), vertexGamma_(other.vertexGamma_),
          useReflectionProbes_(other.useReflectionProbes_), sortingOrder_(other.sortingOrder_), sortingLayerID_(other.sortingLayerID_),
          targetDisplay_(other.targetDisplay_), updateForStandalone_(other.updateForStandalone_), channels_(other.channels_)
    {
    }

    Canvas::~Canvas() { canvas::detail::Forget(*this); }

    void Canvas::Requires(Array<u32>& out) { out.Push(RectTransform::StaticClass()); }

    void Canvas::OnEnableNative() { canvas::detail::HierarchyChanged(*this); }

    void Canvas::OnDisableNative()
    {
        canvas::detail::HierarchyChanged(*this);
        canvas::detail::Forget(*this);
    }

    void Canvas::OnDestroyNative() { canvas::detail::Forget(*this); }

    Canvas* Canvas::ParentCanvas() const
    {
        Transform* t = transform();
        for (Transform* p = t ? t->parent() : nullptr; p; p = p->parent())
        {
            GameObject* g = p->gameObject();
            if (!g || !g->activeInHierarchy())
                continue;
            if (Canvas* c = g->GetComponent<Canvas>(); c && c->enabled())
                return c;
        }
        return nullptr;
    }

    Canvas* Canvas::rootCanvas() const
    {
        const Canvas* c = this;
        while (Canvas* p = c->ParentCanvas())
            c = p;
        return const_cast<Canvas*>(c);
    }

    RenderMode Canvas::renderMode() const
    {
        const Canvas* root = rootCanvas();
        return root == this ? renderMode_ : root->renderMode();
    }

    Camera* Canvas::worldCamera() const
    {
        const Canvas* root = rootCanvas();
        return root == this ? camera_.Get() : root->worldCamera();
    }

    void Canvas::SetWorldCamera(Camera* value) { camera_ = PPtr<Camera>(value); }

    f32 Canvas::scaleFactor() const
    {
        const Canvas* root = rootCanvas();
        return root == this ? scaleFactor_ : root->scaleFactor();
    }

    void Canvas::SetScaleFactor(f32 value)
    {
        // A root canvas's own (nested canvases take their root's); not below a small positive scale.
        if (isRootCanvas())
            scaleFactor_ = value > 0.0001f ? value : 0.0001f;
    }

    f32 Canvas::referencePixelsPerUnit() const
    {
        const Canvas* root = rootCanvas();
        return root == this ? referencePixelsPerUnit_ : root->referencePixelsPerUnit();
    }

    bool Canvas::pixelPerfect() const
    {
        if (isRootCanvas() || overridePixelPerfect_)
            return pixelPerfect_;
        return rootCanvas()->pixelPerfect();
    }

    i32 Canvas::targetDisplay() const
    {
        const Canvas* root = rootCanvas();
        return root == this ? targetDisplay_ : root->targetDisplay();
    }

    Rect Canvas::pixelRect() const
    {
        RenderMode mode = renderMode();
        Camera* camera = worldCamera();
        if (mode == RenderMode::ScreenSpaceCamera && camera)
            return camera->pixelRect();
        if (mode == RenderMode::WorldSpace && camera)
            return camera->pixelRect();
        Vector2 size = Graphics::ContentDisplaySize(gameObject(), targetDisplay());
        return Rect(0, 0, size.x, size.y);
    }

    Vector2 Canvas::renderingDisplaySize() const
    {
        Camera* camera = worldCamera();
        if (renderMode() == RenderMode::ScreenSpaceCamera && camera)
            return Vector2(f32(camera->pixelWidth()), f32(camera->pixelHeight()));
        return Graphics::ContentDisplaySize(gameObject(), targetDisplay());
    }

    StringView Canvas::sortingLayerName() const { return sortingLayerID_ == 0 ? StringView("Default") : StringView(); }

    void Canvas::SetSortingLayerName(StringView value)
    {
        // The engine has the default sorting layer only.
        if (value == StringView("Default"))
            sortingLayerID_ = 0;
    }

    Canvas::BatchingInterval Canvas::batchingInterval() { return gInterval; }
    void Canvas::SetBatchingInterval(BatchingInterval value) { gInterval = value; }

    Event<>& Canvas::preWillRenderCanvasesInternal()
    {
        static Event<>* e = new Event<>();
        return *e;
    }

    Event<>& Canvas::willRenderCanvasesInternal()
    {
        static Event<>* e = new Event<>();
        return *e;
    }

    void Canvas::ForceUpdateCanvases() { canvas::Batch(true); }

    Material* Canvas::GetDefaultCanvasMaterial()
    {
        static PPtr<Material> material;
        return SharedMaterial(material, "UI/Default");
    }

    Material* Canvas::GetETC1SupportedCanvasMaterial()
    {
        static PPtr<Material> material;
        return SharedMaterial(material, "UI/DefaultETC1");
    }

    // -------------------------------------------------------------------------------------------------- CanvasGroup

    void CanvasGroup::SetAlpha(f32 value)
    {
        value = value < 0 ? 0 : (value > 1 ? 1 : value);
        if (alpha_ != value)
            alpha_ = value, Changed();
    }

    void CanvasGroup::SetInteractable(bool value)
    {
        if (interactable_ != value)
            interactable_ = value, Changed();
    }

    void CanvasGroup::SetBlocksRaycasts(bool value)
    {
        if (blocksRaycasts_ != value)
            blocksRaycasts_ = value, Changed();
    }

    void CanvasGroup::SetIgnoreParentGroups(bool value)
    {
        if (ignoreParentGroups_ != value)
            ignoreParentGroups_ = value, Changed();
    }

    void CanvasGroup::Changed()
    {
        if (GameObject* g = gameObject())
            SendHierarchyMessage(g, ScriptMessage::OnCanvasGroupChanged, true);
    }

    // ------------------------------------------------------------------------------------------- UISystemProfilerApi

    namespace
    {
        profiler::Marker gLayout("UI.Layout"), gRender("UI.Render");
        profiler::Marker& MarkerOf(UISystemProfilerApi::SampleType type)
        {
            return type == UISystemProfilerApi::SampleType::Layout ? gLayout : gRender;
        }
    }

    void UISystemProfilerApi::BeginSample(SampleType type)
    {
        if (profiler::IsCapturing())
            profiler::detail::Begin(MarkerOf(type));
    }

    void UISystemProfilerApi::EndSample(SampleType type)
    {
        if (profiler::IsCapturing())
            profiler::detail::End(MarkerOf(type));
    }

    void UISystemProfilerApi::AddMarker(StringView name, Object* obj)
    {
        // A zero length sample named after the marker (the object is the reference's selection link).
        (void)obj;
        if (!profiler::IsCapturing())
            return;
        static Array<profiler::Marker*>* markers = new Array<profiler::Marker*>();
        static Array<String*>* names = new Array<String*>();
        profiler::Marker* marker = nullptr;
        for (usize i = 0; i < names->Size() && !marker; i++)
            if (*(*names)[i] == name)
                marker = (*markers)[i];
        if (!marker)
        {
            // Each name lives as long as its marker.
            names->Push(new String(name));
            marker = new profiler::Marker((*names)[names->Size() - 1]->CStr());
            markers->Push(marker);
        }
        profiler::detail::Begin(*marker);
        profiler::detail::End(*marker);
    }
}
