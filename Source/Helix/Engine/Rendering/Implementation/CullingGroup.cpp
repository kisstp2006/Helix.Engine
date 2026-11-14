#include <Helix/Engine/Rendering/CullingGroup.h>

#include <Helix/Core/Containers/HashMap.h>
#include <Helix/Core/Logging/Log.h>
#include <Helix/Engine/Transform/Transform.h>

namespace helix::engine
{
    namespace
    {
        LogChannel CullingGroupLog{"Engine"};

        struct Groups
        {
            HashMap<u64, CullingGroup*> byId;
            Array<CullingGroup*> all = Array<CullingGroup*>();
            u64 next = 0;
        };

        Groups& G()
        {
            static Groups g;
            return g;
        }
    }

    CullingGroup::CullingGroup()
    {
        id_ = ++G().next;
        G().byId.Set(id_, this);
        G().all.Push(this);
    }

    CullingGroup::~CullingGroup()
    {
        G().byId.Remove(id_);
        for (usize i = 0; i < G().all.Size(); i++)
            if (G().all[i] == this)
            {
                G().all.RemoveAt(i);
                break;
            }
    }

    CullingGroup* CullingGroup::Find(u64 id)
    {
        CullingGroup** g = G().byId.Find(id);
        return g ? *g : nullptr;
    }

    Camera* CullingGroup::targetCamera() const { return EntityIdToObject<Camera>(camera_); }
    void CullingGroup::SetTargetCamera(Camera* camera) { camera_ = camera ? camera->GetEntityId() : EntityId(); }

    void CullingGroup::SetBoundingSpheres(const BoundingSphere* spheres, i32 capacity)
    {
        spheres_ = spheres;
        capacity_ = spheres ? Max(capacity, 0) : 0;
        count_ = capacity_;
        states_.Resize(usize(capacity_));
        for (u8& s : states_)
            s = 0;
    }

    void CullingGroup::SetBoundingSphereCount(i32 count)
    {
        if (count < 0 || count > capacity_)
        {
            HX_LOG_ERROR(CullingGroupLog, "CullingGroup.SetBoundingSphereCount: {} is outside 0 to {} (the array's length).", count, capacity_);
            return;
        }
        count_ = count;
    }

    bool CullingGroup::Valid(i32 index, const char* what) const
    {
        if (index >= 0 && index < count_)
            return true;
        HX_LOG_ERROR(CullingGroupLog, "CullingGroup.{}: index {} is outside 0 to {}.", what, index, count_ - 1);
        return false;
    }

    void CullingGroup::EraseSwapBack(i32 index)
    {
        if (!Valid(index, "EraseSwapBack"))
            return;
        states_[u32(index)] = states_[u32(count_ - 1)];
        count_--;
    }

    i32 CullingGroup::QueryIndices(bool visible, Span<i32> result, i32 firstIndex) const
    {
        i32 n = 0;
        for (i32 i = Max(firstIndex, 0); i < count_ && usize(n) < result.Size(); i++)
            if (((states_[u32(i)] & CullingGroupEvent::kVisibleFlag) != 0) == visible)
                result[usize(n++)] = i;
        return n;
    }

    i32 CullingGroup::QueryIndices(bool visible, i32 distanceIndex, Span<i32> result, i32 firstIndex) const
    {
        i32 n = 0;
        for (i32 i = Max(firstIndex, 0); i < count_ && usize(n) < result.Size(); i++)
        {
            u8 s = states_[u32(i)];
            if (((s & CullingGroupEvent::kVisibleFlag) != 0) == visible && (s & CullingGroupEvent::kDistanceMask) == distanceIndex)
                result[usize(n++)] = i;
        }
        return n;
    }

    i32 CullingGroup::QueryIndices(i32 distanceIndex, Span<i32> result, i32 firstIndex) const
    {
        i32 n = 0;
        for (i32 i = Max(firstIndex, 0); i < count_ && usize(n) < result.Size(); i++)
            if ((states_[u32(i)] & CullingGroupEvent::kDistanceMask) == distanceIndex)
                result[usize(n++)] = i;
        return n;
    }

    bool CullingGroup::IsVisible(i32 index) const { return Valid(index, "IsVisible") && (states_[u32(index)] & CullingGroupEvent::kVisibleFlag) != 0; }

    i32 CullingGroup::GetDistance(i32 index) const { return Valid(index, "GetDistance") ? states_[u32(index)] & CullingGroupEvent::kDistanceMask : 0; }

    void CullingGroup::SetBoundingDistances(Span<const f32> distances)
    {
        if (distances.Size() >= CullingGroupEvent::kDistanceMask)
        {
            HX_LOG_ERROR(CullingGroupLog, "CullingGroup.SetBoundingDistances: {} distances, at most {}.", distances.Size(), CullingGroupEvent::kDistanceMask - 1);
            return;
        }
        distances_.Clear();
        distances_.Append(distances.Data(), distances.Size());
    }

    void CullingGroup::SetDistanceReferencePoint(Vector3 point)
    {
        referencePoint_ = point;
        referenceTransform_ = EntityId();
    }

    void CullingGroup::SetDistanceReferencePoint(Transform* transform) { referenceTransform_ = transform ? transform->GetEntityId() : EntityId(); }

    void CullingGroup::Evaluate(const Plane* planes, i32 count)
    {
        if (!enabled_ || !spheres_)
            return;
        Vector3 reference = referencePoint_;
        if (Transform* t = EntityIdToObject<Transform>(referenceTransform_))
            reference = t->position();
        for (i32 i = 0; i < count_; i++)
        {
            const BoundingSphere& s = spheres_[i];
            bool visible = true;
            for (i32 k = 0; visible && k < count; k++)
                visible = Vector3::Dot(planes[k].normal, s.position) + planes[k].distance + s.radius >= 0;
            f32 distance = (s.position - reference).magnitude() - s.radius;
            u8 band = 0;
            while (band < distances_.Size() && distance >= distances_[band])
                band++;
            u8 state = u8(band | (visible ? CullingGroupEvent::kVisibleFlag : 0));
            u8 previous = states_[u32(i)];
            if (state == previous)
                continue;
            states_[u32(i)] = state;
            if (callback_)
                callback_(user_, CullingGroupEvent{i, previous, state});
        }
    }

    namespace render
    {
        void EvaluateCullingGroups(EntityId camera, const Plane* planes, i32 count)
        {
            if (G().all.Empty() || !camera.IsValid())
                return;
            // A callback may make or dispose groups: walk a copy.
            Array<u64> ids;
            for (CullingGroup* g : G().all)
                if (g->targetCamera() && g->targetCamera()->GetEntityId() == camera)
                    ids.Push(g->Id());
            for (u64 id : ids)
                if (CullingGroup* g = CullingGroup::Find(id))
                    g->Evaluate(planes, count);
        }
    }
}
