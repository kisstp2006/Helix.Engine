#include <Helix/Engine/GameObject/Implementation/GameObjectAccess.h>
#include <Helix/Engine/Object/Implementation/ObjectAccess.h>
#include <Helix/Engine/Transform/Implementation/TransformAccess.h>

#include <Helix/Core/Logging/Log.h>
#include <Helix/Math/Geometry.h>

namespace helix::engine
{
    Array<TransformLinks>* detail::gTransformLinks = nullptr;
    void (*detail::gHierarchyChanged)(Transform*, Transform*, Transform*) = nullptr;

    namespace
    {
        LogChannel TransformLog{"Engine"};

        bool BeingDestroyed(const Transform* t)
        {
            const GameObject* g = t ? t->gameObject() : nullptr;
            return g && ObjectAccess::Destroying(g);
        }

        // The hierarchy may change: no activation hooks running, nothing involved being destroyed.
        bool CanRestructure(const Transform* t, const Transform* other)
        {
            if (TransformAccess::Activating())
            {
                HX_LOG_ERROR(TransformLog, "Cannot change the hierarchy of {} while activating or deactivating the parent.", t->Name());
                return false;
            }
            if (BeingDestroyed(t) || BeingDestroyed(other))
            {
                HX_LOG_ERROR(TransformLog, "Cannot change the hierarchy of {} while it or its new parent is being destroyed.", t->Name());
                return false;
            }
            return true;
        }

        void Changed(Transform* t) { TransformAccess::MarkChanged(t->Hierarchy(), t->NodeIndex()); }
    }

    Transform::Transform() { TransformAccess::CreateRoot(this); }

    Transform::~Transform() { TransformAccess::Release(this); }

    // ------------------------------------------------------------------------------------------------ hierarchy

    bool Transform::SetParent(Transform* newParent, bool worldPositionStays)
    {
        if (newParent == parent())
            return true;
        if (!CanRestructure(this, newParent))
            return false;
        Transform* oldParent = parent();
        if (newParent && newParent->IsChildOf(this))
        {
            HX_LOG_ERROR(TransformLog, "SetParent: {} cannot become a child of its own child {}.", Name(), newParent->Name());
            return false;
        }
        if (worldPositionStays)
        {
            Matrix4x4 world = localToWorldMatrix();
            Quaternion r = rotation();
            TransformAccess::Place(this, newParent, ~0u);
            TransformAccess::SetWorld(this, world, r);
        }
        else
        {
            TransformAccess::Place(this, newParent, ~0u);
            Changed(this);
        }
        if (GameObject* g = gameObject())
            GameObjectAccess::UpdateActive(g);
        if (detail::gHierarchyChanged)
            detail::gHierarchyChanged(this, oldParent, newParent);
        return true;
    }

    Transform* Transform::GetChild(u32 index) const
    {
        const TransformHierarchy& h = *hierarchy_;
        for (u32 j = index_ + 1, end = index_ + h.size[index_]; j < end; j += h.size[j])
            if (!(h.flags[j] & TransformFlags::Dead) && index-- == 0)
                return h.transforms[j];
        return nullptr;
    }

    void Transform::DetachChildren()
    {
        while (Transform* c = GetChild(0))
            if (!c->SetParent(nullptr, true))
                return;
    }

    i32 Transform::GetSiblingIndex() const
    {
        const TransformHierarchy& h = *hierarchy_;
        i32 p = h.parent[index_];
        i32 k = 0;
        if (p < 0)
        {
            for (const TransformHierarchy* r = FirstRootHierarchy(h.scene); r && r != &h; r = r->nextRoot)
                k++;
            return k;
        }
        for (u32 j = u32(p) + 1; j < index_; j += h.size[j])
            if (!(h.flags[j] & TransformFlags::Dead))
                k++;
        return k;
    }

    void Transform::SetSiblingIndex(i32 index)
    {
        u32 target = index < 0 ? 0u : u32(index);
        if (i32(target) == GetSiblingIndex() || !CanRestructure(this, nullptr))
            return;
        // Out and back in at the new place: world values stay valid, the parent is the same.
        TransformAccess::Place(this, parent(), target);
    }

    void Transform::SetAsLastSibling()
    {
        Transform* p = parent();
        u32 last = p ? p->childCount() - 1 : RootHierarchyCount(hierarchy_->scene) - 1;
        SetSiblingIndex(i32(last));
    }

    Transform* Transform::Find(StringView path) const
    {
        const Transform* t = this;
        while (t && !path.Empty())
        {
            usize slash = path.Find('/');
            StringView name = slash == StringView::npos ? path : path.Substr(0, slash);
            path = slash == StringView::npos ? StringView() : path.Substr(slash + 1);
            const TransformHierarchy& h = *t->hierarchy_;
            const Transform* found = nullptr;
            for (u32 j = t->index_ + 1, end = t->index_ + h.size[t->index_]; j < end && !found; j += h.size[j])
                if (!(h.flags[j] & TransformFlags::Dead) && h.transforms[j]->Name() == name)
                    found = h.transforms[j];
            t = found;
        }
        return const_cast<Transform*>(t);
    }

    void Transform::SetHierarchyCapacity(u32 capacity) { TransformAccess::Resize(*hierarchy_, capacity); }

    void Transform::SetHasChanged(bool value)
    {
        u8& f = hierarchy_->flags[index_];
        f = value ? u8(f | TransformFlags::HasChanged) : u8(f & ~TransformFlags::HasChanged);
    }

    // ------------------------------------------------------------------------------------------------ local

    void Transform::SetLocalPosition(Vector3 value)
    {
        hierarchy_->local[index_].position = value;
        Changed(this);
    }

    void Transform::SetLocalRotation(Quaternion value)
    {
        hierarchy_->local[index_].rotation = value;
        Changed(this);
    }

    void Transform::SetLocalScale(Vector3 value)
    {
        hierarchy_->local[index_].scale = value;
        Changed(this);
    }

    void Transform::SetLocal(const LocalTRS& value)
    {
        hierarchy_->local[index_] = value;
        Changed(this);
    }

    void Transform::SetLocalPositionAndRotation(Vector3 position, Quaternion rotation)
    {
        LocalTRS& l = hierarchy_->local[index_];
        l.position = position;
        l.rotation = rotation;
        Changed(this);
    }

    // ------------------------------------------------------------------------------------------------ world

    Vector3 Transform::lossyScale() const { return TransformAccess::ScaleOf(localToWorldMatrix(), rotation()); }

    void Transform::SetPosition(Vector3 value)
    {
        Transform* p = parent();
        hierarchy_->local[index_].position = p ? p->worldToLocalMatrix().MultiplyPoint3x4(value) : value;
        Changed(this);
    }

    void Transform::SetRotation(Quaternion value)
    {
        Transform* p = parent();
        hierarchy_->local[index_].rotation = p ? Quaternion::Inverse(p->rotation()) * value : value;
        Changed(this);
    }

    void Transform::SetPositionAndRotation(Vector3 position, Quaternion rotation)
    {
        Transform* p = parent();
        LocalTRS& l = hierarchy_->local[index_];
        if (p)
        {
            l.position = p->worldToLocalMatrix().MultiplyPoint3x4(position);
            l.rotation = Quaternion::Inverse(p->rotation()) * rotation;
        }
        else
        {
            l.position = position;
            l.rotation = rotation;
        }
        Changed(this);
    }

    // ------------------------------------------------------------------------------------------------ movement

    void Transform::LookAt(Vector3 worldPosition, Vector3 worldUp)
    {
        Vector3 forward = worldPosition - position();
        f32 length = forward.magnitude();
        if (length <= Vector3::kEpsilon)
            return;
        // Looking along the up axis: the shortest turn of the forward axis instead.
        if (Vector3::Cross(forward, worldUp).sqrMagnitude() <= 1e-12f * forward.sqrMagnitude() * worldUp.sqrMagnitude())
            SetRotation(Quaternion::FromToRotation(Vector3::forward(), forward / length));
        else
            SetRotation(Quaternion::LookRotation(forward, worldUp));
    }

    void Transform::LookAt(const Transform* target, Vector3 worldUp)
    {
        if (target)
            LookAt(target->position(), worldUp);
    }

    void Transform::Rotate(Vector3 eulers, Space relativeTo)
    {
        Quaternion q = Quaternion::Euler(eulers);
        if (relativeTo == Space::Self)
            SetLocalRotation(localRotation() * q);
        else
            SetRotation(q * rotation());
    }

    void Transform::Rotate(Vector3 axis, f32 angle, Space relativeTo)
    {
        Vector3 worldAxis = relativeTo == Space::Self ? TransformDirection(axis) : axis;
        SetRotation(Quaternion::AngleAxis(angle, worldAxis) * rotation());
    }

    void Transform::RotateAround(Vector3 point, Vector3 axis, f32 angle)
    {
        Quaternion q = Quaternion::AngleAxis(angle, axis);
        SetPositionAndRotation(point + q * (position() - point), q * rotation());
    }

    void Transform::Translate(Vector3 translation, Space relativeTo)
    {
        SetPosition(position() + (relativeTo == Space::Self ? TransformDirection(translation) : translation));
    }

    void Transform::Translate(Vector3 translation, const Transform* relativeTo)
    {
        SetPosition(position() + (relativeTo ? relativeTo->TransformDirection(translation) : translation));
    }

    // ------------------------------------------------------------------------------------------------ conversions

    namespace
    {
        void Directions(const Matrix4x4& m, Span<const Vector3> in, Span<Vector3> out)
        {
            HX_ASSERT(out.Size() >= in.Size());
            for (usize i = 0; i < in.Size(); i++)
                out[i] = m.MultiplyVector(in[i]);
        }
    }

    void Transform::TransformPoints(Span<const Vector3> points, Span<Vector3> out) const { ::helix::TransformPoints(localToWorldMatrix(), points, out); }

    void Transform::TransformDirections(Span<const Vector3> directions, Span<Vector3> out) const
    {
        Directions(Matrix4x4::Rotate(rotation()), directions, out);
    }

    void Transform::TransformVectors(Span<const Vector3> vectors, Span<Vector3> out) const { Directions(localToWorldMatrix(), vectors, out); }

    void Transform::InverseTransformPoints(Span<const Vector3> points, Span<Vector3> out) const
    {
        ::helix::TransformPoints(worldToLocalMatrix(), points, out);
    }

    void Transform::InverseTransformDirections(Span<const Vector3> directions, Span<Vector3> out) const
    {
        Directions(Matrix4x4::Rotate(Quaternion::Inverse(rotation())), directions, out);
    }

    void Transform::InverseTransformVectors(Span<const Vector3> vectors, Span<Vector3> out) const
    {
        Directions(worldToLocalMatrix(), vectors, out);
    }
}
