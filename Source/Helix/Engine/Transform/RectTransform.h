#pragma once

#include <Helix/Engine/Event/Event.h>
#include <Helix/Engine/Transform/Transform.h>
#include <Helix/Math/Geometry.h>

// RectTransform: a Transform with a rectangle placed in its parent's rectangle by anchors (normalized places in the
// parent), a size relative to the anchors' distance and a pivot (the normalized origin of rotation and scale). The
// anchored position is the pivot's place relative to the anchors' reference point; it is kept when the parent's
// rectangle changes (the local position follows). A GameObject's Transform is replaced by a RectTransform when one
// is added. The canvas UI lays out and draws in these rectangles.
namespace helix::engine
{
    // What a driver (a layout component) controls of a RectTransform.
    HX_ENUM(Namespace = "HelixEngine", Attributes = "[System.Flags]")
    enum class DrivenTransformProperties : i32
    {
        None = 0,
        All = -1,
        AnchoredPositionX = 2,
        AnchoredPositionY = 4,
        AnchoredPositionZ = 8,
        Rotation = 16,
        ScaleX = 32,
        ScaleY = 64,
        ScaleZ = 128,
        AnchorMinX = 256,
        AnchorMinY = 512,
        AnchorMaxX = 1024,
        AnchorMaxY = 2048,
        SizeDeltaX = 4096,
        SizeDeltaY = 8192,
        PivotX = 16384,
        PivotY = 32768,
        AnchoredPosition = 6,
        AnchoredPosition3D = 14,
        Scale = 224,
        AnchorMin = 768,
        AnchorMax = 3072,
        Anchors = 3840,
        SizeDelta = 12288,
        Pivot = 49152,
    };

    HX_CLASS()
    class RectTransform final : public Transform
    {
        HX_ENGINE_CLASS(RectTransform, Transform, ClassId::RectTransform, ClassDisallowMultiple)

    public:
        HX_ENUM()
        enum class Axis : i32
        {
            Horizontal = 0,
            Vertical = 1,
        };

        HX_ENUM()
        enum class Edge : i32
        {
            Left = 0,
            Right = 1,
            Top = 2,
            Bottom = 3,
        };

        HX_ENUM()
        enum class FitResult : i32
        {
            Success = 0,
            AlreadyInside = 1,
            FailLargerThanTarget = 2,
            FailNotCoplanar = 3,
            FailZRotationMismatch = 4,
            FailInvalidSizeTarget = 5,
        };

        RectTransform() = default;
        RectTransform(const RectTransform& other) = default;

        // ---------------------------------------------------------------------------------------------- the rectangle
        // The rectangle in its own space: the pivot at the origin.
        HX_PROPERTY(ReadOnly) Rect rect() const;
        HX_PROPERTY() Vector2 anchorMin() const { return anchorMin_; }
        void SetAnchorMin(Vector2 value);
        HX_PROPERTY() Vector2 anchorMax() const { return anchorMax_; }
        void SetAnchorMax(Vector2 value);
        HX_PROPERTY() Vector2 anchoredPosition() const;
        void SetAnchoredPosition(Vector2 value);
        HX_PROPERTY() Vector3 anchoredPosition3D() const;
        void SetAnchoredPosition3D(Vector3 value);
        HX_PROPERTY() Vector2 sizeDelta() const { return sizeDelta_; }
        void SetSizeDelta(Vector2 value);
        HX_PROPERTY() Vector2 pivot() const { return pivot_; }
        void SetPivot(Vector2 value);
        HX_PROPERTY() Vector2 offsetMin() const;
        void SetOffsetMin(Vector2 value);
        HX_PROPERTY() Vector2 offsetMax() const;
        void SetOffsetMax(Vector2 value);

        // ---------------------------------------------------------------------------------------------- driving
        HX_PROPERTY(ReadOnly) Object* drivenByObject() const { return drivenBy_.Get(); }
        DrivenTransformProperties drivenProperties() const { return driven_; }
        HX_PROPERTY(Internal, ReadOnly) DrivenTransformProperties drivenPropertiesInternal() const { return driven_; }
        // A driver's claim (DrivenRectTransformTracker): the properties it sets (none: released).
        HX_FUNCTION(Internal) void SetDriven(Object* driver, DrivenTransformProperties properties);
        HX_PROPERTY() bool sendChildDimensionsChange() const { return sendChildDimensionsChange_; }
        void SetSendChildDimensionsChange(bool value) { sendChildDimensionsChange_ = value; }
        // Raised when a driven RectTransform's driven properties must be set again (it was read or reverted).
        HX_EVENT(Internal, ApiName = "reapplyDrivenProperties") static Event<RectTransform*>& reapplyDrivenPropertiesInternal();
        // Every RectTransform whose rectangle changed (the canvas UI's components hear their own).
        static Event<RectTransform*>& dimensionsChanged();
        HX_FUNCTION() void ForceUpdateRectTransforms() {}

        // ---------------------------------------------------------------------------------------------- corners and edges
        // Bottom left, top left, top right, bottom right.
        void GetLocalCorners(Vector3 (&corners)[4]) const;
        void GetWorldCorners(Vector3 (&corners)[4]) const;
        HX_FUNCTION(Internal) Array<Vector3> LocalCornersInternal() const;
        HX_FUNCTION(Internal) Array<Vector3> WorldCornersInternal() const;
        // The axis aligned rectangle of the world corners (x and y).
        HX_FUNCTION() Rect GetWorldRect() const;
        // The world rectangle holds the other's.
        HX_FUNCTION() bool Contains(const RectTransform* other) const;
        // The rectangle's edges in the parent's space.
        HX_FUNCTION() f32 GetLocalTop() const;
        HX_FUNCTION() f32 GetLocalBottom() const;
        HX_FUNCTION() f32 GetLocalLeft() const;
        HX_FUNCTION() f32 GetLocalRight() const;
        // An edge moved there: the rectangle moves (preserveSize) or that edge alone does.
        HX_FUNCTION() void SetLocalTop(f32 top, bool preserveSize = true);
        HX_FUNCTION() void SetLocalBottom(f32 bottom, bool preserveSize = true);
        HX_FUNCTION() void SetLocalLeft(f32 left, bool preserveSize = true);
        HX_FUNCTION() void SetLocalRight(f32 right, bool preserveSize = true);

        // ---------------------------------------------------------------------------------------------- setters
        HX_FUNCTION() void SetAnchors(Vector2 anchor);
        HX_FUNCTION() void SetPivotAndAnchors(Vector2 value);
        HX_FUNCTION() void SetInsetAndSizeFromParentEdge(Edge edge, f32 inset, f32 size);
        HX_FUNCTION() void SetSizeWithCurrentAnchors(Axis axis, f32 size);
        // The pivot moved, the rectangle staying where it is (the children too, with adjustChildren).
        HX_FUNCTION() void SetPivotWithCounterAdjust(Vector2 pivot, bool adjustChildren = false);
        HX_FUNCTION() bool IsCoplanarWith(const RectTransform* other) const;
        HX_FUNCTION() FitResult FitInsideCoplanarRectTransform(RectTransform* target, bool allowShrink = false);
        HX_FUNCTION() FitResult TryFitInsideCoplanarRectTransform(RectTransform* target, bool allowShrink = false) const;

        // The parent's rectangle (zero without a RectTransform parent).
        Rect ParentRect() const;

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            Vector2 anchored = anchoredPosition();
            v.Field("m_AnchorMin", anchorMin_);
            v.Field("m_AnchorMax", anchorMax_);
            v.Field("m_AnchoredPosition", anchored);
            v.Field("m_SizeDelta", sizeDelta_);
            v.Field("m_Pivot", pivot_);
            if constexpr (V::kReading)
                loadedAnchored_ = anchored, hasLoadedAnchored_ = true;
        }

        // After a file's objects were linked (the parent known): the anchored position read applied.
        void ApplyLoaded();

    private:
        // The rectangle changed from `old`: the children keep their anchored positions, listeners hear it.
        void RectChanged(const Rect& old);
        // A change of anchors, size or pivot keeping the anchored position.
        template <class F>
        void Change(F&& f);
        Vector2 AnchorReference(const Rect& parent) const;

        Vector2 anchorMin_ = Vector2(0.5f, 0.5f), anchorMax_ = Vector2(0.5f, 0.5f);
        Vector2 sizeDelta_ = Vector2(100, 100), pivot_ = Vector2(0.5f, 0.5f);
        PPtr<Object> drivenBy_;
        DrivenTransformProperties driven_ = DrivenTransformProperties::None;
        bool sendChildDimensionsChange_ = false;
        Vector2 loadedAnchored_ = Vector2(0, 0);
        bool hasLoadedAnchored_ = false;
    };

    // The RectTransforms a driver controls: claimed by Add, released by Clear (the layout components keep one).
    class DrivenRectTransformTracker
    {
    public:
        void Add(Object* driver, RectTransform* rectTransform, DrivenTransformProperties drivenProperties);
        void Clear();

    private:
        Array<PPtr<RectTransform>> tracked_;
    };
}
