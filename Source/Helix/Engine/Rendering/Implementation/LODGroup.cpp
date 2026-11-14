#include <Helix/Engine/Rendering/Implementation/RendererManager.h>
#include <Helix/Engine/Rendering/LODGroup.h>

#include <Helix/Core/Logging/Log.h>
#include <Helix/Engine/Transform/Transform.h>

#include <cmath>

namespace helix::engine
{
    namespace
    {
        LogChannel LODLog{"Engine"};
    }

    namespace
    {
        struct LODState
        {
            // The groups by slot (a renderer names its group's slot); freed slots are reused.
            Array<EntityId> groups = Array<EntityId>();
            Array<u32> free = Array<u32>();
            f32 crossFadeDuration = 0.5f;
        };

        LODState& S()
        {
            static LODState s;
            return s;
        }
    }

    f32 LODGroup::crossFadeAnimationDuration() { return S().crossFadeDuration; }

    void LODGroup::SetCrossFadeAnimationDuration(f32 value)
    {
        if (value <= 0)
        {
            HX_LOG_ERROR(LODLog, "LODGroup.crossFadeAnimationDuration: the duration must be positive ({}).", value);
            return;
        }
        S().crossFadeDuration = value;
    }

    void LODGroup::SetLocalReferencePoint(Vector3 value) { localReferencePoint_ = value; }
    void LODGroup::SetSize(f32 value) { size_ = value; }

    void LODGroup::SetEnabled(bool value)
    {
        if (enabled_ == value)
            return;
        enabled_ = value;
        if (value)
            Assign();
        else
            Unassign();
    }

    void LODGroup::SetLODs(Span<const LOD> lods)
    {
        if (lods.Size() > kMaxLODs)
        {
            HX_LOG_ERROR(LODLog, "LODGroup.SetLODs: {} levels, at most {} are supported.", lods.Size(), kMaxLODs);
            return;
        }
        for (usize i = 1; i < lods.Size(); i++)
            if (lods[i].screenRelativeTransitionHeight >= lods[i - 1].screenRelativeTransitionHeight)
            {
                HX_LOG_ERROR(LODLog, "LODGroup.SetLODs: the heights must fall from level to level (level {}).", i);
                return;
            }
        Unassign();
        lods_.Clear();
        for (const LOD& l : lods)
            lods_.Push(l);
        Assign();
    }

    void LODGroup::RecalculateBounds()
    {
        Bounds all;
        bool any = false;
        for (const LOD& l : lods_)
            for (const PPtr<Renderer>& p : l.renderers)
                if (Renderer* r = p.Get())
                {
                    Bounds b = r->bounds();
                    if (!any)
                        all = b;
                    else
                        all.Encapsulate(b);
                    any = true;
                }
        Transform* t = transform();
        if (!any || !t)
            return;
        localReferencePoint_ = t->InverseTransformPoint(all.center);
        Vector3 s = t->lossyScale();
        f32 scale = std::fmax(std::fabs(s.x), std::fmax(std::fabs(s.y), std::fabs(s.z)));
        f32 extent = std::fmax(all.size().x, std::fmax(all.size().y, all.size().z));
        size_ = scale > 0 ? extent / scale : extent;
    }

    void LODGroup::ForceLOD(i32 index) { forced_ = index < 0 ? -1 : index; }

    void LODGroup::OnEnableNative() { Assign(); }
    void LODGroup::OnDisableNative() { Unassign(); }

    void LODGroup::OnDestroyNative()
    {
        Unassign();
        if (slot_ != ~0u)
        {
            S().groups[slot_] = EntityId();
            S().free.Push(slot_);
            slot_ = ~0u;
        }
    }

    void LODGroup::Assign()
    {
        GameObject* g = gameObject();
        if (!enabled_ || !g || !g->activeInHierarchy())
            return;
        LODState& s = S();
        if (slot_ == ~0u)
        {
            if (!s.free.Empty())
            {
                slot_ = s.free.Back();
                s.free.Pop();
                s.groups[slot_] = GetEntityId();
            }
            else
            {
                slot_ = u32(s.groups.Size());
                s.groups.Push(GetEntityId());
            }
        }
        // A renderer on several levels draws on each of them.
        for (u32 level = 0; level < lods_.Size(); level++)
            for (const PPtr<Renderer>& p : lods_[level].renderers)
                if (Renderer* r = p.Get())
                {
                    if (RendererAccess::LodSlot(*r) != slot_)
                        RendererAccess::LodMask(*r) = 0;
                    RendererAccess::LodSlot(*r) = slot_;
                    RendererAccess::LodMask(*r) |= u8(1u << level);
                    RendererManager::Get().Touch(*r);
                }
    }

    void LODGroup::Unassign()
    {
        for (const LOD& l : lods_)
            for (const PPtr<Renderer>& p : l.renderers)
                if (Renderer* r = p.Get(); r && RendererAccess::LodSlot(*r) == slot_)
                {
                    RendererAccess::LodSlot(*r) = ~0u;
                    RendererAccess::LodMask(*r) = 0;
                    RendererManager::Get().Touch(*r);
                }
    }

    namespace render
    {
        u32 LODGroupSlots() { return u32(S().groups.Size()); }

        void SelectLODs(const LODSelection& c, Array<u8>& levels)
        {
            LODState& s = S();
            levels.Resize(s.groups.Size());
            f32 halfTan = std::tan(c.fieldOfView * 0.5f * 3.14159265f / 180.0f);
            for (u32 slot = 0; slot < s.groups.Size(); slot++)
            {
                levels[slot] = 0xFF;
                LODGroup* g = EntityIdToObject<LODGroup>(s.groups[slot]);
                Transform* t = g ? g->transform() : nullptr;
                if (!g || !t || !g->enabled())
                    continue;
                Span<const LOD> lods = g->GetLODs();
                if (g->forcedLOD() >= 0)
                {
                    if (u32(g->forcedLOD()) < lods.Size())
                        levels[slot] = u8(g->forcedLOD());
                    continue;
                }
                Vector3 scale = t->lossyScale();
                f32 size = g->size() * std::fmax(std::fabs(scale.x), std::fmax(std::fabs(scale.y), std::fabs(scale.z)));
                f32 height;
                if (c.orthographic)
                    height = size / (2 * c.orthoSize);
                else
                {
                    f32 distance = (t->TransformPoint(g->localReferencePoint()) - c.cameraPosition).magnitude();
                    height = distance > 0 ? size / (2 * distance * halfTan) : 1e30f;
                }
                height *= c.bias;
                for (u32 level = 0; level < lods.Size(); level++)
                    if (height >= lods[level].screenRelativeTransitionHeight)
                    {
                        levels[slot] = u8(Max(level, u32(Max(c.maximumLevel, 0))));
                        if (levels[slot] >= lods.Size())
                            levels[slot] = 0xFF;
                        break;
                    }
            }
        }
    }
}
