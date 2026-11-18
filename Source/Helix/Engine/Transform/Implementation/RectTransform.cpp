#include <Helix/Engine/Transform/RectTransform.h>

#include <Helix/Engine/Script/ScriptRuntime.h>

#include <cmath>

namespace helix::engine
{
    namespace
    {
        Vector2 Mul(Vector2 a, Vector2 b) { return Vector2(a.x * b.x, a.y * b.y); }
        Vector2 Lerp(Vector2 a, Vector2 b, Vector2 t) { return Vector2(a.x + (b.x - a.x) * t.x, a.y + (b.y - a.y) * t.y); }
        f32 Get(Vector2 v, i32 axis) { return axis == 0 ? v.x : v.y; }
        void Set(Vector2& v, i32 axis, f32 value) { (axis == 0 ? v.x : v.y) = value; }
    }

    // ------------------------------------------------------------------------------------------------ the rectangle

    Rect RectTransform::ParentRect() const
    {
        const Transform* p = parent();
        if (p && p->Is<RectTransform>())
            return static_cast<const RectTransform*>(p)->rect();
        return Rect(0, 0, 0, 0);
    }

    Rect RectTransform::rect() const
    {
        Rect parent = ParentRect();
        Vector2 size(parent.width * (anchorMax_.x - anchorMin_.x) + sizeDelta_.x, parent.height * (anchorMax_.y - anchorMin_.y) + sizeDelta_.y);
        return Rect(-pivot_.x * size.x, -pivot_.y * size.y, size.x, size.y);
    }

    Vector2 RectTransform::AnchorReference(const Rect& parent) const
    {
        Vector2 anchor = Lerp(anchorMin_, anchorMax_, pivot_);
        return Vector2(parent.x + parent.width * anchor.x, parent.y + parent.height * anchor.y);
    }

    Vector2 RectTransform::anchoredPosition() const
    {
        Vector3 local = localPosition();
        Vector2 ref = AnchorReference(ParentRect());
        return Vector2(local.x - ref.x, local.y - ref.y);
    }

    void RectTransform::SetAnchoredPosition(Vector2 value)
    {
        Vector2 ref = AnchorReference(ParentRect());
        Vector3 local = localPosition();
        SetLocalPosition(Vector3(ref.x + value.x, ref.y + value.y, local.z));
    }

    Vector3 RectTransform::anchoredPosition3D() const
    {
        Vector2 a = anchoredPosition();
        return Vector3(a.x, a.y, localPosition().z);
    }

    void RectTransform::SetAnchoredPosition3D(Vector3 value)
    {
        Vector2 ref = AnchorReference(ParentRect());
        SetLocalPosition(Vector3(ref.x + value.x, ref.y + value.y, value.z));
    }

    template <class F>
    void RectTransform::Change(F&& f)
    {
        Rect old = rect();
        Vector2 anchored = anchoredPosition();
        f();
        SetAnchoredPosition(anchored);
        Rect now = rect();
        if (now.x != old.x || now.y != old.y || now.width != old.width || now.height != old.height)
            RectChanged(old);
    }

    void RectTransform::SetAnchorMin(Vector2 value)
    {
        Change([&] { anchorMin_ = value; });
    }

    void RectTransform::SetAnchorMax(Vector2 value)
    {
        Change([&] { anchorMax_ = value; });
    }

    void RectTransform::SetSizeDelta(Vector2 value)
    {
        Change([&] { sizeDelta_ = value; });
    }

    void RectTransform::SetPivot(Vector2 value)
    {
        Change([&] { pivot_ = value; });
    }

    Vector2 RectTransform::offsetMin() const
    {
        Vector2 a = anchoredPosition();
        return Vector2(a.x - sizeDelta_.x * pivot_.x, a.y - sizeDelta_.y * pivot_.y);
    }

    void RectTransform::SetOffsetMin(Vector2 value)
    {
        Vector2 offset = Vector2(value.x - offsetMin().x, value.y - offsetMin().y);
        Vector2 a = anchoredPosition();
        SetSizeDelta(Vector2(sizeDelta_.x - offset.x, sizeDelta_.y - offset.y));
        SetAnchoredPosition(Vector2(a.x + offset.x * (1 - pivot_.x), a.y + offset.y * (1 - pivot_.y)));
    }

    Vector2 RectTransform::offsetMax() const
    {
        Vector2 a = anchoredPosition();
        return Vector2(a.x + sizeDelta_.x * (1 - pivot_.x), a.y + sizeDelta_.y * (1 - pivot_.y));
    }

    void RectTransform::SetOffsetMax(Vector2 value)
    {
        Vector2 offset = Vector2(value.x - offsetMax().x, value.y - offsetMax().y);
        Vector2 a = anchoredPosition();
        SetSizeDelta(Vector2(sizeDelta_.x + offset.x, sizeDelta_.y + offset.y));
        SetAnchoredPosition(Vector2(a.x + offset.x * pivot_.x, a.y + offset.y * pivot_.y));
    }

    void RectTransform::RectChanged(const Rect& old)
    {
        // The children keep their anchored positions (their rectangles may change with this one).
        for (u32 i = 0, n = childCount(); i < n; i++)
        {
            Transform* c = GetChild(i);
            if (!c || !c->Is<RectTransform>())
                continue;
            auto* child = static_cast<RectTransform*>(c);
            Vector2 oldRef = child->AnchorReference(old);
            Vector3 local = child->localPosition();
            Vector2 anchored(local.x - oldRef.x, local.y - oldRef.y);
            // The child's own rectangle as it was (its size from the old parent rectangle).
            Vector2 oldSize(old.width * (child->anchorMax_.x - child->anchorMin_.x) + child->sizeDelta_.x,
                            old.height * (child->anchorMax_.y - child->anchorMin_.y) + child->sizeDelta_.y);
            Rect childOld(-child->pivot_.x * oldSize.x, -child->pivot_.y * oldSize.y, oldSize.x, oldSize.y);
            child->SetAnchoredPosition(anchored);
            Rect childNow = child->rect();
            if (childNow.width != childOld.width || childNow.height != childOld.height || childNow.x != childOld.x || childNow.y != childOld.y)
                child->RectChanged(childOld);
        }
        dimensionsChanged().Invoke(this);
        SendHierarchyMessage(gameObject(), ScriptMessage::OnRectTransformDimensionsChange);
        // The parent hears a child's change when it asks to.
        if (Transform* p = parent(); p && p->Is<RectTransform>() && static_cast<RectTransform*>(p)->sendChildDimensionsChange_)
            SendHierarchyMessage(p->gameObject(), ScriptMessage::OnRectTransformDimensionsChange);
    }

    Event<RectTransform*>& RectTransform::reapplyDrivenPropertiesInternal()
    {
        static Event<RectTransform*>* e = new Event<RectTransform*>();
        return *e;
    }

    Event<RectTransform*>& RectTransform::dimensionsChanged()
    {
        static Event<RectTransform*>* e = new Event<RectTransform*>();
        return *e;
    }

    void RectTransform::SetDriven(Object* driver, DrivenTransformProperties properties)
    {
        drivenBy_ = PPtr<Object>(properties == DrivenTransformProperties::None ? nullptr : driver);
        driven_ = driver ? properties : DrivenTransformProperties::None;
    }

    void RectTransform::ApplyLoaded()
    {
        if (!hasLoadedAnchored_)
            return;
        hasLoadedAnchored_ = false;
        Vector3 local = localPosition();
        SetAnchoredPosition3D(Vector3(loadedAnchored_.x, loadedAnchored_.y, local.z));
        if (driven_ != DrivenTransformProperties::None)
            reapplyDrivenPropertiesInternal().Invoke(this);
    }

    // ------------------------------------------------------------------------------------------------ corners and edges

    void RectTransform::GetLocalCorners(Vector3 (&corners)[4]) const
    {
        Rect r = rect();
        corners[0] = Vector3(r.x, r.y, 0);
        corners[1] = Vector3(r.x, r.y + r.height, 0);
        corners[2] = Vector3(r.x + r.width, r.y + r.height, 0);
        corners[3] = Vector3(r.x + r.width, r.y, 0);
    }

    void RectTransform::GetWorldCorners(Vector3 (&corners)[4]) const
    {
        GetLocalCorners(corners);
        const Matrix4x4& m = localToWorldMatrix();
        for (Vector3& c : corners)
            c = m.MultiplyPoint3x4(c);
    }

    Array<Vector3> RectTransform::LocalCornersInternal() const
    {
        Vector3 c[4];
        GetLocalCorners(c);
        Array<Vector3> out;
        for (const Vector3& v : c)
            out.Push(v);
        return out;
    }

    Array<Vector3> RectTransform::WorldCornersInternal() const
    {
        Vector3 c[4];
        GetWorldCorners(c);
        Array<Vector3> out;
        for (const Vector3& v : c)
            out.Push(v);
        return out;
    }

    Rect RectTransform::GetWorldRect() const
    {
        Vector3 c[4];
        GetWorldCorners(c);
        f32 x0 = c[0].x, y0 = c[0].y, x1 = c[0].x, y1 = c[0].y;
        for (const Vector3& v : c)
        {
            x0 = v.x < x0 ? v.x : x0, y0 = v.y < y0 ? v.y : y0;
            x1 = v.x > x1 ? v.x : x1, y1 = v.y > y1 ? v.y : y1;
        }
        return Rect(x0, y0, x1 - x0, y1 - y0);
    }

    bool RectTransform::Contains(const RectTransform* other) const
    {
        if (!other)
            return false;
        Rect a = GetWorldRect(), b = other->GetWorldRect();
        return b.x >= a.x && b.y >= a.y && b.x + b.width <= a.x + a.width && b.y + b.height <= a.y + a.height;
    }

    f32 RectTransform::GetLocalTop() const { return localPosition().y + rect().y + rect().height; }
    f32 RectTransform::GetLocalBottom() const { return localPosition().y + rect().y; }
    f32 RectTransform::GetLocalLeft() const { return localPosition().x + rect().x; }
    f32 RectTransform::GetLocalRight() const { return localPosition().x + rect().x + rect().width; }

    void RectTransform::SetLocalTop(f32 top, bool preserveSize)
    {
        f32 d = top - GetLocalTop();
        Vector2 a = anchoredPosition();
        if (preserveSize)
            return SetAnchoredPosition(Vector2(a.x, a.y + d));
        SetSizeDelta(Vector2(sizeDelta_.x, sizeDelta_.y + d));
        SetAnchoredPosition(Vector2(a.x, a.y + d * pivot_.y));
    }

    void RectTransform::SetLocalBottom(f32 bottom, bool preserveSize)
    {
        f32 d = bottom - GetLocalBottom();
        Vector2 a = anchoredPosition();
        if (preserveSize)
            return SetAnchoredPosition(Vector2(a.x, a.y + d));
        SetSizeDelta(Vector2(sizeDelta_.x, sizeDelta_.y - d));
        SetAnchoredPosition(Vector2(a.x, a.y + d * (1 - pivot_.y)));
    }

    void RectTransform::SetLocalLeft(f32 left, bool preserveSize)
    {
        f32 d = left - GetLocalLeft();
        Vector2 a = anchoredPosition();
        if (preserveSize)
            return SetAnchoredPosition(Vector2(a.x + d, a.y));
        SetSizeDelta(Vector2(sizeDelta_.x - d, sizeDelta_.y));
        SetAnchoredPosition(Vector2(a.x + d * (1 - pivot_.x), a.y));
    }

    void RectTransform::SetLocalRight(f32 right, bool preserveSize)
    {
        f32 d = right - GetLocalRight();
        Vector2 a = anchoredPosition();
        if (preserveSize)
            return SetAnchoredPosition(Vector2(a.x + d, a.y));
        SetSizeDelta(Vector2(sizeDelta_.x + d, sizeDelta_.y));
        SetAnchoredPosition(Vector2(a.x + d * pivot_.x, a.y));
    }

    // ------------------------------------------------------------------------------------------------ setters

    void RectTransform::SetAnchors(Vector2 anchor)
    {
        Change([&] { anchorMin_ = anchorMax_ = anchor; });
    }

    void RectTransform::SetPivotAndAnchors(Vector2 value)
    {
        Change([&] { pivot_ = anchorMin_ = anchorMax_ = value; });
    }

    void RectTransform::SetInsetAndSizeFromParentEdge(Edge edge, f32 inset, f32 size)
    {
        i32 axis = edge == Edge::Top || edge == Edge::Bottom ? 1 : 0;
        bool end = edge == Edge::Top || edge == Edge::Right;
        f32 anchorValue = end ? 1.0f : 0.0f;
        Vector2 amin = anchorMin_, amax = anchorMax_, delta = sizeDelta_, a = anchoredPosition();
        Set(amin, axis, anchorValue);
        Set(amax, axis, anchorValue);
        Set(delta, axis, size);
        f32 position = end ? -inset - size * (1 - Get(pivot_, axis)) : inset + size * Get(pivot_, axis);
        Set(a, axis, position);
        Change([&] {
            anchorMin_ = amin;
            anchorMax_ = amax;
            sizeDelta_ = delta;
        });
        SetAnchoredPosition(a);
    }

    void RectTransform::SetSizeWithCurrentAnchors(Axis axis, f32 size)
    {
        i32 i = i32(axis);
        Rect parent = ParentRect();
        Vector2 parentSize(parent.width, parent.height);
        Vector2 delta = sizeDelta_;
        Set(delta, i, size - Get(parentSize, i) * (Get(anchorMax_, i) - Get(anchorMin_, i)));
        SetSizeDelta(delta);
    }

    void RectTransform::SetPivotWithCounterAdjust(Vector2 value, bool adjustChildren)
    {
        Rect r = rect();
        Vector2 shift = Mul(Vector2(value.x - pivot_.x, value.y - pivot_.y), Vector2(r.width, r.height));
        // The pivot's move in the parent's space (through rotation and scale).
        Vector3 local = localPosition();
        Vector3 moved = localRotation() * Vector3(shift.x * localScale().x, shift.y * localScale().y, 0);
        Array<Matrix4x4> childWorld;
        if (adjustChildren)
            for (u32 i = 0, n = childCount(); i < n; i++)
                childWorld.Push(GetChild(i)->localToWorldMatrix());
        Rect old = r;
        pivot_ = value;
        SetLocalPosition(Vector3(local.x + moved.x, local.y + moved.y, local.z + moved.z));
        if (adjustChildren)
        {
            // The children stay where they were in the world.
            for (u32 i = 0, n = childCount(); i < n; i++)
            {
                Transform* c = GetChild(i);
                Vector3 p = childWorld[i].GetPosition();
                c->SetPosition(p);
            }
        }
        else
            RectChanged(old);
    }

    bool RectTransform::IsCoplanarWith(const RectTransform* other) const
    {
        if (!other)
            return false;
        Vector3 n = rotation() * Vector3::forward, m = other->rotation() * Vector3::forward;
        f32 dot = n.x * m.x + n.y * m.y + n.z * m.z;
        if (std::fabs(std::fabs(dot) - 1) > 1e-4f)
            return false;
        Vector3 d = other->position() - position();
        return std::fabs(d.x * n.x + d.y * n.y + d.z * n.z) < 1e-3f;
    }

    namespace
    {
        // The other rectangle's corners in this one's parent space, and the move that puts this one inside it.
        struct Fit
        {
            RectTransform::FitResult result;
            Vector2 move;
            Vector2 size;
        };

        Fit TryFit(const RectTransform& self, const RectTransform* target, bool allowShrink)
        {
            Fit f{RectTransform::FitResult::Success, Vector2(0, 0), self.sizeDelta()};
            if (!target || !self.IsCoplanarWith(target))
                return {RectTransform::FitResult::FailNotCoplanar, Vector2(0, 0), f.size};
            // A turn about the normal other than a quarter makes the rectangles' edges cross.
            Quaternion relative = Quaternion::Inverse(target->rotation()) * self.rotation();
            f32 z = relative.eulerAngles().z;
            f32 quarter = std::fmod(std::fabs(z), 90.0f);
            if (quarter > 0.01f && quarter < 89.99f)
                return {RectTransform::FitResult::FailZRotationMismatch, Vector2(0, 0), f.size};
            Rect t = target->rect();
            if (t.width <= 0 || t.height <= 0)
                return {RectTransform::FitResult::FailInvalidSizeTarget, Vector2(0, 0), f.size};
            // Both in the target's space.
            Vector3 corners[4];
            self.GetWorldCorners(corners);
            f32 x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
            for (const Vector3& c : corners)
            {
                Vector3 p = target->InverseTransformPoint(c);
                x0 = p.x < x0 ? p.x : x0, y0 = p.y < y0 ? p.y : y0, x1 = p.x > x1 ? p.x : x1, y1 = p.y > y1 ? p.y : y1;
            }
            f32 w = x1 - x0, h = y1 - y0;
            if (x0 >= t.x - 1e-3f && y0 >= t.y - 1e-3f && x1 <= t.x + t.width + 1e-3f && y1 <= t.y + t.height + 1e-3f)
                return {RectTransform::FitResult::AlreadyInside, Vector2(0, 0), f.size};
            if ((w > t.width + 1e-3f || h > t.height + 1e-3f) && !allowShrink)
                return {RectTransform::FitResult::FailLargerThanTarget, Vector2(0, 0), f.size};
            f32 nw = w > t.width ? t.width : w, nh = h > t.height ? t.height : h;
            if (nw != w || nh != h)
                f.size = Vector2(self.sizeDelta().x - (w - nw), self.sizeDelta().y - (h - nh));
            f32 cx = x0 + w * 0.5f, cy = y0 + h * 0.5f;
            f32 lo = t.x + nw * 0.5f, hi = t.x + t.width - nw * 0.5f;
            f32 nx = cx < lo ? lo : cx > hi ? hi : cx;
            lo = t.y + nh * 0.5f, hi = t.y + t.height - nh * 0.5f;
            f32 ny = cy < lo ? lo : cy > hi ? hi : cy;
            f.move = Vector2(nx - cx, ny - cy);
            return f;
        }
    }

    RectTransform::FitResult RectTransform::TryFitInsideCoplanarRectTransform(RectTransform* target, bool allowShrink) const
    {
        return TryFit(*this, target, allowShrink).result;
    }

    RectTransform::FitResult RectTransform::FitInsideCoplanarRectTransform(RectTransform* target, bool allowShrink)
    {
        Fit f = TryFit(*this, target, allowShrink);
        if (f.result != FitResult::Success)
            return f.result;
        SetSizeDelta(f.size);
        // The move in the target's plane, in the world.
        Vector3 world = target->TransformVector(Vector3(f.move.x, f.move.y, 0));
        SetPosition(position() + world);
        return FitResult::Success;
    }

    // ------------------------------------------------------------------------------------------------ tracker

    void DrivenRectTransformTracker::Add(Object* driver, RectTransform* rectTransform, DrivenTransformProperties drivenProperties)
    {
        if (!rectTransform)
            return;
        DrivenTransformProperties now = rectTransform->drivenByObject() == driver
                                            ? DrivenTransformProperties(i32(rectTransform->drivenProperties()) | i32(drivenProperties))
                                            : drivenProperties;
        rectTransform->SetDriven(driver, now);
        tracked_.Push(PPtr<RectTransform>(rectTransform));
    }

    void DrivenRectTransformTracker::Clear()
    {
        for (const PPtr<RectTransform>& t : tracked_)
            if (RectTransform* r = t.Get())
                r->SetDriven(nullptr, DrivenTransformProperties::None);
        tracked_.Clear();
    }
}
