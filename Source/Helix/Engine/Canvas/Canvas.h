#pragma once

#include <Helix/Engine/Event/Event.h>
#include <Helix/Engine/GameObject/GameObject.h>
#include <Helix/Math/Geometry.h>

// Canvas: the root of a canvas UI hierarchy. Over the screen (overlay), in front of a camera at a plane distance, or a
// rectangle in the world; its RectTransform is the screen's (or the camera's view) in the first two. Each frame the
// canvases batch the CanvasRenderers under them in hierarchy order (nested canvases with their own sorting apart) and
// draw: overlay canvases over the screen by sorting order, camera canvases through their camera, world canvases with
// the scene's transparent objects (depth tested, turned in 3D as their transforms say). CanvasGroup fades and gates
// the elements under it.
namespace helix::engine
{
    class Camera;
    class Material;
    class RectTransform;
    class Transform;

    HX_ENUM(Namespace = "HelixEngine")
    enum class RenderMode : i32
    {
        ScreenSpaceOverlay = 0,
        ScreenSpaceCamera = 1,
        WorldSpace = 2,
    };

    // The vertex streams a canvas keeps besides position, color and the first texture coordinates.
    HX_ENUM(Namespace = "HelixEngine", Attributes = "[System.Flags]")
    enum class AdditionalCanvasShaderChannels : i32
    {
        None = 0,
        TexCoord1 = 1,
        TexCoord2 = 2,
        TexCoord3 = 4,
        Normal = 8,
        Tangent = 16,
    };

    HX_ENUM(Namespace = "HelixEngine")
    enum class StandaloneRenderResize : i32
    {
        Enabled = 0,
        Disabled = 1,
    };

    HX_CLASS(Namespace = "HelixEngine", Sealed)
    class Canvas final : public Behaviour
    {
        HX_ENGINE_CLASS(Canvas, Behaviour, ClassId::Canvas, ClassDisallowMultiple)

    public:
        HX_ENUM()
        enum class BatchingInterval : i32
        {
            GatedByRendering = 0,
            AlwaysUpdate = 1,
        };

        Canvas() = default;
        Canvas(const Canvas& other);
        ~Canvas();
        static void Requires(Array<u32>& out);

        // ---------------------------------------------------------------------------------------------- placement
        // A nested canvas has its root's.
        HX_PROPERTY() RenderMode renderMode() const;
        void SetRenderMode(RenderMode value) { renderMode_ = value; }
        HX_PROPERTY() Camera* worldCamera() const;
        void SetWorldCamera(Camera* value);
        HX_PROPERTY() f32 planeDistance() const { return planeDistance_; }
        void SetPlaneDistance(f32 value) { planeDistance_ = value; }
        HX_PROPERTY() f32 scaleFactor() const;
        void SetScaleFactor(f32 value);
        HX_PROPERTY() f32 referencePixelsPerUnit() const;
        void SetReferencePixelsPerUnit(f32 value) { referencePixelsPerUnit_ = value; }
        HX_PROPERTY() bool pixelPerfect() const;
        void SetPixelPerfect(bool value) { pixelPerfect_ = value; }
        HX_PROPERTY() bool overridePixelPerfect() const { return overridePixelPerfect_; }
        void SetOverridePixelPerfect(bool value) { overridePixelPerfect_ = value; }
        HX_PROPERTY() i32 targetDisplay() const;
        void SetTargetDisplay(i32 value) { targetDisplay_ = value; }
        HX_PROPERTY() StandaloneRenderResize updateRectTransformForStandalone() const { return updateForStandalone_; }
        void SetUpdateRectTransformForStandalone(StandaloneRenderResize value) { updateForStandalone_ = value; }
        // The rectangle on the screen it covers (its camera's for a camera canvas), in pixels.
        HX_PROPERTY(ReadOnly) Rect pixelRect() const;
        // The size of the display it draws on.
        HX_PROPERTY(ReadOnly) Vector2 renderingDisplaySize() const;

        // ---------------------------------------------------------------------------------------------- hierarchy
        HX_PROPERTY(ReadOnly) bool isRootCanvas() const { return ParentCanvas() == nullptr; }
        HX_PROPERTY(ReadOnly) Canvas* rootCanvas() const;
        // The nearest enabled canvas above it.
        Canvas* ParentCanvas() const;

        // ---------------------------------------------------------------------------------------------- sorting
        HX_PROPERTY() bool overrideSorting() const { return overrideSorting_; }
        void SetOverrideSorting(bool value) { overrideSorting_ = value; }
        HX_PROPERTY() i32 sortingOrder() const { return sortingOrder_; }
        void SetSortingOrder(i32 value) { sortingOrder_ = value; }
        HX_PROPERTY() i32 sortingLayerID() const { return sortingLayerID_; }
        void SetSortingLayerID(i32 value) { sortingLayerID_ = value; }
        HX_PROPERTY() StringView sortingLayerName() const;
        void SetSortingLayerName(StringView value);
        HX_PROPERTY(ReadOnly) i32 cachedSortingLayerValue() const { return 0; }
        // Its place among the canvases drawn this frame.
        HX_PROPERTY(ReadOnly) i32 renderOrder() const { return renderOrder_; }
        HX_PROPERTY() f32 normalizedSortingGridSize() const { return sortingGrid_; }
        void SetNormalizedSortingGridSize(f32 value) { sortingGrid_ = value; }

        // ---------------------------------------------------------------------------------------------- shading
        HX_PROPERTY() AdditionalCanvasShaderChannels additionalShaderChannels() const { return channels_; }
        void SetAdditionalShaderChannels(AdditionalCanvasShaderChannels value) { channels_ = value; }
        // The vertex colors made linear in the shader (not before) where the target holds linear colors.
        HX_PROPERTY() bool vertexColorAlwaysGammaSpace() const { return vertexGamma_; }
        void SetVertexColorAlwaysGammaSpace(bool value) { vertexGamma_ = value; }
        HX_PROPERTY() bool useReflectionProbes() const { return useReflectionProbes_; }
        void SetUseReflectionProbes(bool value) { useReflectionProbes_ = value; }

        // ---------------------------------------------------------------------------------------------- statics
        HX_PROPERTY() static BatchingInterval batchingInterval();
        static void SetBatchingInterval(BatchingInterval value);
        // Before the canvases batch: the layout and the graphics' rebuilds run in these.
        HX_EVENT(Internal, ApiName = "preWillRenderCanvases") static Event<>& preWillRenderCanvasesInternal();
        HX_EVENT(Internal, ApiName = "willRenderCanvases") static Event<>& willRenderCanvasesInternal();
        // The events raised and every canvas batched now.
        HX_FUNCTION() static void ForceUpdateCanvases();
        // UI/Default's material (one shared, made on first use).
        HX_FUNCTION() static Material* GetDefaultCanvasMaterial();
        // UI/DefaultETC1's: the alpha from a second texture (_AlphaTex).
        HX_FUNCTION() static Material* GetETC1SupportedCanvasMaterial();
        HX_FUNCTION(Internal) static Material* GetDefaultCanvasTextMaterialInternal() { return GetDefaultCanvasMaterial(); }

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            i32 mode = i32(renderMode_), resize = i32(updateForStandalone_), channels = i32(channels_);
            v.Field("m_RenderMode", mode);
            v.Field("m_Camera", camera_);
            v.Field("m_PlaneDistance", planeDistance_);
            v.Field("m_PixelPerfect", pixelPerfect_);
            v.Field("m_OverrideSorting", overrideSorting_);
            v.Field("m_OverridePixelPerfect", overridePixelPerfect_);
            v.Field("m_SortingBucketNormalizedSize", sortingGrid_);
            v.Field("m_VertexColorAlwaysGammaSpace", vertexGamma_);
            v.Field("m_AdditionalShaderChannelsFlag", channels);
            v.Field("m_UpdateRectTransformForStandalone", resize);
            v.Field("m_SortingLayerID", sortingLayerID_);
            v.Field("m_SortingOrder", sortingOrder_);
            v.Field("m_TargetDisplay", targetDisplay_);
            renderMode_ = RenderMode(mode), updateForStandalone_ = StandaloneRenderResize(resize), channels_ = AdditionalCanvasShaderChannels(channels);
        }

        void OnEnableNative();
        void OnDisableNative();
        void OnDestroyNative();

    private:
        friend struct CanvasBatcherAccess;

        RenderMode renderMode_ = RenderMode::ScreenSpaceOverlay;
        PPtr<Camera> camera_;
        f32 planeDistance_ = 100, scaleFactor_ = 1, referencePixelsPerUnit_ = 100, sortingGrid_ = 0.1f;
        bool pixelPerfect_ = false, overridePixelPerfect_ = false, overrideSorting_ = false, vertexGamma_ = false, useReflectionProbes_ = false;
        i32 sortingOrder_ = 0, sortingLayerID_ = 0, targetDisplay_ = 0, renderOrder_ = 0;
        StandaloneRenderResize updateForStandalone_ = StandaloneRenderResize::Enabled;
        AdditionalCanvasShaderChannels channels_ = AdditionalCanvasShaderChannels::None;
        // What the batcher keeps of it (its mesh and batches) while it draws apart.
        struct CanvasBatches* batches_ = nullptr;
    };

    // A CanvasGroup's alpha multiplies the elements under it (the groups above it too unless ignoreParentGroups);
    // interactable and blocksRaycasts gate the selectables and the raycasts below it.
    HX_CLASS(Namespace = "HelixEngine", Sealed)
    class CanvasGroup final : public Behaviour
    {
        HX_ENGINE_CLASS(CanvasGroup, Behaviour, ClassId::CanvasGroup, ClassNone)

    public:
        CanvasGroup() = default;
        CanvasGroup(const CanvasGroup&) = default;

        HX_PROPERTY() f32 alpha() const { return alpha_; }
        void SetAlpha(f32 value);
        HX_PROPERTY() bool interactable() const { return interactable_; }
        void SetInteractable(bool value);
        HX_PROPERTY() bool blocksRaycasts() const { return blocksRaycasts_; }
        void SetBlocksRaycasts(bool value);
        HX_PROPERTY() bool ignoreParentGroups() const { return ignoreParentGroups_; }
        void SetIgnoreParentGroups(bool value);
        // A raycast through it hits what is below it where it blocks raycasts.
        HX_FUNCTION() bool IsRaycastLocationValid(Vector2 sp, Camera* eventCamera) const
        {
            (void)sp, (void)eventCamera;
            return blocksRaycasts_;
        }

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("m_Alpha", alpha_);
            v.Field("m_Interactable", interactable_);
            v.Field("m_BlocksRaycasts", blocksRaycasts_);
            v.Field("m_IgnoreParentGroups", ignoreParentGroups_);
        }

        void OnEnableNative() { Changed(); }
        void OnDisableNative() { Changed(); }

    private:
        // The scripts of its GameObject and below hear OnCanvasGroupChanged.
        void Changed();

        f32 alpha_ = 1;
        bool interactable_ = true, blocksRaycasts_ = true, ignoreParentGroups_ = false;
    };

    // Profiler samples of the canvas UI's layout and rendering.
    HX_CLASS(Namespace = "HelixEngine", Static)
    class UISystemProfilerApi
    {
    public:
        HX_ENUM()
        enum class SampleType : i32
        {
            Layout = 0,
            Render = 1,
        };

        HX_FUNCTION() static void BeginSample(SampleType type);
        HX_FUNCTION() static void EndSample(SampleType type);
        HX_FUNCTION() static void AddMarker(StringView name, Object* obj);
    };

    // The batcher, for the engine's own: every canvas batched now (the events raised first when asked), and the
    // canvases of the frame queued for drawing.
    namespace canvas
    {
        void Batch(bool raiseEvents);
        void QueueDraws();
        // The canvases the last batching drew apart (roots and nested ones with their own sorting).
        u32 CanvasCount();
        // The draws (submeshes) of a canvas drawn apart in the last batching (0 for others).
        u32 BatchCount(const Canvas& canvas);
    }
}
