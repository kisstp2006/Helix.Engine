#include <Helix/Engine/Canvas/Canvas.h>
#include <Helix/Engine/Transform/RectTransformUtility.h>

#include <Helix/Engine/Rendering/Camera.h>

#include <cmath>

namespace helix::engine
{
    Ray RectTransformUtility::ScreenPointToRay(Camera* cam, Vector2 screenPos)
    {
        if (cam)
            return cam->ScreenPointToRay(Vector3(screenPos.x, screenPos.y, 0));
        return Ray(Vector3(screenPos.x, screenPos.y, -100), Vector3::forward);
    }

    Vector2 RectTransformUtility::WorldToScreenPoint(Camera* cam, Vector3 worldPoint)
    {
        if (!cam)
            return Vector2(worldPoint.x, worldPoint.y);
        Vector3 p = cam->WorldToScreenPoint(worldPoint);
        return Vector2(p.x, p.y);
    }

    bool RectTransformUtility::ScreenPointToWorldPointInRectangle(RectTransform* rect, Vector2 screenPoint, Camera* cam, Vector3& worldPoint)
    {
        worldPoint = Vector3(0, 0, 0);
        if (!rect)
            return false;
        Ray ray = ScreenPointToRay(cam, screenPoint);
        // The rectangle's plane: through its origin, facing back along its forward axis.
        Vector3 normal = rect->rotation() * Vector3::back;
        Vector3 origin = rect->position();
        f32 denom = Vector3::Dot(ray.direction, normal);
        if (std::fabs(denom) < 1e-6f)
            return false;
        f32 t = Vector3::Dot(origin - ray.origin, normal) / denom;
        if (t < 0)
            return false;
        worldPoint = ray.origin + ray.direction * t;
        return true;
    }

    bool RectTransformUtility::ScreenPointToLocalPointInRectangle(RectTransform* rect, Vector2 screenPoint, Camera* cam, Vector2& localPoint)
    {
        localPoint = Vector2(0, 0);
        Vector3 world;
        if (!ScreenPointToWorldPointInRectangle(rect, screenPoint, cam, world))
            return false;
        Vector3 local = rect->InverseTransformPoint(world);
        localPoint = Vector2(local.x, local.y);
        return true;
    }

    bool RectTransformUtility::RectangleContainsScreenPoint(RectTransform* rect, Vector2 screenPoint)
    {
        return RectangleContainsScreenPoint(rect, screenPoint, nullptr, Vector4(0, 0, 0, 0));
    }

    bool RectTransformUtility::RectangleContainsScreenPoint(RectTransform* rect, Vector2 screenPoint, Camera* cam)
    {
        return RectangleContainsScreenPoint(rect, screenPoint, cam, Vector4(0, 0, 0, 0));
    }

    bool RectTransformUtility::RectangleContainsScreenPoint(RectTransform* rect, Vector2 screenPoint, Camera* cam, Vector4 offset)
    {
        Vector2 local;
        if (!ScreenPointToLocalPointInRectangle(rect, screenPoint, cam, local))
            return false;
        Rect r = rect->rect();
        Rect inner(r.x + offset.x, r.y + offset.y, r.width - offset.x - offset.z, r.height - offset.y - offset.w);
        return local.x >= inner.x && local.y >= inner.y && local.x <= inner.x + inner.width && local.y <= inner.y + inner.height;
    }

    namespace
    {
        void Gather(Transform* root, Transform* t, Vector3& lo, Vector3& hi, bool& any)
        {
            if (!t || (t->gameObject() && !t->gameObject()->activeInHierarchy()))
                return;
            if (t->Is<RectTransform>())
            {
                Vector3 corners[4];
                static_cast<RectTransform*>(t)->GetWorldCorners(corners);
                for (const Vector3& c : corners)
                {
                    Vector3 p = root->InverseTransformPoint(c);
                    if (!any)
                        lo = hi = p, any = true;
                    lo = Vector3::Min(lo, p);
                    hi = Vector3::Max(hi, p);
                }
            }
            for (u32 i = 0; i < t->childCount(); i++)
                Gather(root, t->GetChild(i), lo, hi, any);
        }
    }

    Bounds RectTransformUtility::CalculateRelativeRectTransformBounds(Transform* root, Transform* child)
    {
        if (!root || !child)
            return Bounds(Vector3(0, 0, 0), Vector3(0, 0, 0));
        Vector3 lo(0, 0, 0), hi(0, 0, 0);
        bool any = false;
        Gather(root, child, lo, hi, any);
        if (!any)
            return Bounds(Vector3(0, 0, 0), Vector3(0, 0, 0));
        return Bounds((lo + hi) * 0.5f, hi - lo);
    }

    Bounds RectTransformUtility::CalculateRelativeRectTransformBounds(Transform* trans) { return CalculateRelativeRectTransformBounds(trans, trans); }

    namespace
    {
        f32& At(Vector2& v, i32 axis) { return axis == 0 ? v.x : v.y; }
    }

    void RectTransformUtility::FlipLayoutOnAxis(RectTransform* rect, i32 axis, bool keepPositioning, bool recursive)
    {
        if (!rect || axis < 0 || axis > 1)
            return;
        if (recursive)
            for (u32 i = 0; i < rect->childCount(); i++)
                if (Transform* c = rect->GetChild(i); c && c->Is<RectTransform>())
                    FlipLayoutOnAxis(static_cast<RectTransform*>(c), axis, false, true);
        Vector2 pivot = rect->pivot();
        At(pivot, axis) = 1 - At(pivot, axis);
        rect->SetPivot(pivot);
        if (keepPositioning)
            return;
        Vector2 anchored = rect->anchoredPosition();
        At(anchored, axis) = -At(anchored, axis);
        Vector2 amin = rect->anchorMin(), amax = rect->anchorMax();
        f32 oldMin = At(amin, axis);
        At(amin, axis) = 1 - At(amax, axis);
        At(amax, axis) = 1 - oldMin;
        rect->SetAnchorMin(amin);
        rect->SetAnchorMax(amax);
        rect->SetAnchoredPosition(anchored);
    }

    void RectTransformUtility::FlipLayoutAxes(RectTransform* rect, bool keepPositioning, bool recursive)
    {
        if (!rect)
            return;
        if (recursive)
            for (u32 i = 0; i < rect->childCount(); i++)
                if (Transform* c = rect->GetChild(i); c && c->Is<RectTransform>())
                    FlipLayoutAxes(static_cast<RectTransform*>(c), false, true);
        auto swap = [](Vector2 v) { return Vector2(v.y, v.x); };
        rect->SetPivot(swap(rect->pivot()));
        rect->SetSizeDelta(swap(rect->sizeDelta()));
        if (keepPositioning)
            return;
        Vector2 anchored = swap(rect->anchoredPosition());
        Vector2 amin = swap(rect->anchorMin()), amax = swap(rect->anchorMax());
        rect->SetAnchorMin(amin);
        rect->SetAnchorMax(amax);
        rect->SetAnchoredPosition(anchored);
    }

    Rect RectTransformUtility::GetScreenRectInternal(RectTransform* rectTransform, Camera* camera)
    {
        if (!rectTransform)
            return Rect(0, 0, 0, 0);
        if (!camera)
            return rectTransform->GetWorldRect();
        Vector3 corners[4];
        rectTransform->GetWorldCorners(corners);
        f32 x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
        for (const Vector3& c : corners)
        {
            Vector3 p = camera->WorldToScreenPoint(c);
            x0 = p.x < x0 ? p.x : x0, y0 = p.y < y0 ? p.y : y0, x1 = p.x > x1 ? p.x : x1, y1 = p.y > y1 ? p.y : y1;
        }
        return Rect(x0, y0, x1 - x0, y1 - y0);
    }

    namespace
    {
        // Snapping applies: a screen space canvas that is pixel perfect.
        Canvas* SnappingRoot(Canvas* canvas)
        {
            if (!canvas || canvas->renderMode() == RenderMode::WorldSpace || canvas->scaleFactor() <= 0 || !canvas->pixelPerfect())
                return nullptr;
            return canvas->rootCanvas();
        }

        // A world point on the root's pixel grid (in its space: scaled pixels from the screen point of its center).
        Vector3 SnapWorld(Canvas& root, Vector3 world)
        {
            Transform* t = root.transform();
            Vector3 c = t->InverseTransformPoint(world);
            f32 s = root.scaleFactor();
            Rect pixels = root.pixelRect();
            Vector2 o(pixels.x + pixels.width * 0.5f, pixels.y + pixels.height * 0.5f);
            c.x = (std::round(c.x * s + o.x) - o.x) / s;
            c.y = (std::round(c.y * s + o.y) - o.y) / s;
            return t->TransformPoint(c);
        }
    }

    Vector2 RectTransformUtility::PixelAdjustPoint(Vector2 point, Transform* elementTransform, Canvas* canvas)
    {
        Canvas* root = SnappingRoot(canvas);
        if (!root || !elementTransform)
            return point;
        Vector3 world = SnapWorld(*root, elementTransform->TransformPoint(Vector3(point.x, point.y, 0)));
        Vector3 local = elementTransform->InverseTransformPoint(world);
        return Vector2(local.x, local.y);
    }

    Rect RectTransformUtility::PixelAdjustRect(RectTransform* rectTransform, Canvas* canvas)
    {
        if (!rectTransform)
            return Rect(0, 0, 0, 0);
        Rect r = rectTransform->rect();
        if (!SnappingRoot(canvas))
            return r;
        Vector2 lo = PixelAdjustPoint(Vector2(r.xMin(), r.yMin()), rectTransform, canvas);
        Vector2 hi = PixelAdjustPoint(Vector2(r.xMax(), r.yMax()), rectTransform, canvas);
        return Rect(lo.x, lo.y, hi.x - lo.x, hi.y - lo.y);
    }
}
