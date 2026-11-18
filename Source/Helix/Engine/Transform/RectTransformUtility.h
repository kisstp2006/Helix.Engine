#pragma once

#include <Helix/Engine/Transform/RectTransform.h>

namespace helix::engine
{
    class Camera;
    class Canvas;

    // Conversions between the screen and RectTransforms (a null camera: an overlay canvas, whose world is the
    // screen's pixels), bounds of a hierarchy of rectangles, mirrored and swapped layouts.
    HX_CLASS(Namespace = "HelixEngine", Static)
    class RectTransformUtility
    {
    public:
        // The ray through a screen point (a null camera: forward from 100 units before the point).
        HX_FUNCTION() static Ray ScreenPointToRay(Camera* cam, Vector2 screenPos);
        HX_FUNCTION() static Vector2 WorldToScreenPoint(Camera* cam, Vector3 worldPoint);
        // Where the screen point's ray meets the rectangle's plane; false when it misses it.
        HX_FUNCTION(Out = "worldPoint") static bool ScreenPointToWorldPointInRectangle(RectTransform* rect, Vector2 screenPoint, Camera* cam, Vector3& worldPoint);
        HX_FUNCTION(Out = "localPoint") static bool ScreenPointToLocalPointInRectangle(RectTransform* rect, Vector2 screenPoint, Camera* cam, Vector2& localPoint);
        // The screen point is inside the rectangle (less a padding: left, bottom, right, top).
        HX_FUNCTION() static bool RectangleContainsScreenPoint(RectTransform* rect, Vector2 screenPoint);
        HX_FUNCTION() static bool RectangleContainsScreenPoint(RectTransform* rect, Vector2 screenPoint, Camera* cam);
        HX_FUNCTION() static bool RectangleContainsScreenPoint(RectTransform* rect, Vector2 screenPoint, Camera* cam, Vector4 offset);
        // The bounds of the active RectTransforms from child down, in root's space.
        HX_FUNCTION() static Bounds CalculateRelativeRectTransformBounds(Transform* root, Transform* child);
        HX_FUNCTION() static Bounds CalculateRelativeRectTransformBounds(Transform* trans);
        // Mirrored on an axis: the pivot (and, unless keepPositioning, the anchors and anchored position).
        HX_FUNCTION() static void FlipLayoutOnAxis(RectTransform* rect, i32 axis, bool keepPositioning, bool recursive);
        // The axes swapped: pivot and size (and, unless keepPositioning, the anchors and anchored position).
        HX_FUNCTION() static void FlipLayoutAxes(RectTransform* rect, bool keepPositioning, bool recursive);
        // The rectangle on the screen through a camera (none: its world rectangle).
        // A point of an element moved onto the screen's pixel grid where its canvas is pixel perfect (screen space).
        HX_FUNCTION() static Vector2 PixelAdjustPoint(Vector2 point, Transform* elementTransform, Canvas* canvas);
        // The rectangle with its corners on the pixel grid (else as it is).
        HX_FUNCTION() static Rect PixelAdjustRect(RectTransform* rectTransform, Canvas* canvas);
        HX_FUNCTION(Internal) static Rect GetScreenRectInternal(RectTransform* rectTransform, Camera* camera);
    };
}
