#include <Helix/Engine/Rendering/ReflectionProbe.h>

#include <Helix/Engine/Transform/Transform.h>

#include <algorithm>
#include <cmath>

namespace helix::engine
{
    namespace
    {
        struct ProbeState
        {
            Array<EntityId> probes = Array<EntityId>();
            Array<ReflectionProbe*> cache = Array<ReflectionProbe*>();
        };

        ProbeState& State()
        {
            static ProbeState s;
            return s;
        }

        // How far a point is inside a box (negative: outside, the distance to it).
        f32 Depth(const Bounds& box, const Vector3& p)
        {
            Vector3 d(std::fabs(p.x - box.center.x) - box.extents.x, std::fabs(p.y - box.center.y) - box.extents.y,
                      std::fabs(p.z - box.center.z) - box.extents.z);
            f32 inside = Min(Max(d.x, Max(d.y, d.z)), 0.0f);
            Vector3 outside(Max(d.x, 0.0f), Max(d.y, 0.0f), Max(d.z, 0.0f));
            return outside.magnitude() > 0 ? -outside.magnitude() : -inside;
        }
    }

    Bounds ReflectionProbe::bounds() const
    {
        Vector3 position = transform() ? transform()->position() : Vector3();
        return Bounds(position + center_, Vector3(std::fabs(size_.x), std::fabs(size_.y), std::fabs(size_.z)));
    }

    void ReflectionProbe::Reset()
    {
        const ReflectionProbe d;
        mode_ = d.mode_, refreshMode_ = d.refreshMode_, timeSlicingMode_ = d.timeSlicingMode_, resolution_ = d.resolution_, size_ = d.size_;
        center_ = d.center_, nearClip_ = d.nearClip_, farClip_ = d.farClip_, shadowDistance_ = d.shadowDistance_, clearFlags_ = d.clearFlags_;
        backgroundColor_ = d.backgroundColor_, cullingMask_ = d.cullingMask_, intensity_ = d.intensity_, blendDistance_ = d.blendDistance_;
        hdr_ = d.hdr_, boxProjection_ = d.boxProjection_, renderDynamicObjects_ = d.renderDynamicObjects_;
        useOcclusionCulling_ = d.useOcclusionCulling_, importance_ = d.importance_;
        customBakedTexture_ = PPtr<Texture>(), bakedTexture_ = PPtr<Texture>();
    }

    void ReflectionProbe::OnEnableNative() { State().probes.Push(GetEntityId()); }

    void ReflectionProbe::OnDisableNative()
    {
        Array<EntityId>& list = State().probes;
        for (usize i = 0; i < list.Size(); i++)
            if (list[i] == GetEntityId())
            {
                list.RemoveAt(i);
                break;
            }
    }

    Span<ReflectionProbe* const> ReflectionProbe::EnabledProbes()
    {
        ProbeState& s = State();
        s.cache.Clear();
        for (EntityId id : s.probes)
            if (ReflectionProbe* p = EntityIdToObject<ReflectionProbe>(id))
                s.cache.Push(p);
        return Span<ReflectionProbe* const>(s.cache.Data(), s.cache.Size());
    }

    u32 ReflectionProbe::ClosestProbes(const Bounds& rendererBounds, ReflectionProbeUsage usage, Span<ReflectionProbe* const> candidates,
                                       ReflectionProbeBlendInfo out[2])
    {
        if (usage == ReflectionProbeUsage::Off)
            return 0;
        struct Found
        {
            ReflectionProbe* probe;
            i32 importance;
            f32 volume;
            f32 weight;
        };
        Found best[2] = {};
        u32 count = 0;
        auto before = [](const Found& a, const Found& b) { return a.importance != b.importance ? a.importance > b.importance : a.volume < b.volume; };
        for (ReflectionProbe* p : candidates)
        {
            if (!p)
                continue;
            Bounds box = p->bounds();
            f32 blend = Max(p->blendDistance(), 0.0f);
            Bounds reach(box.center, box.size() + Vector3(blend, blend, blend) * 2.0f);
            if (!reach.Intersects(rendererBounds))
                continue;
            f32 depth = Depth(box, rendererBounds.center);
            f32 weight = depth >= 0 ? 1.0f : blend > 0 ? Max(1.0f + depth / blend, 0.0f) : 0.0f;
            Vector3 s = box.size();
            Found f{p, p->importance(), s.x * s.y * s.z, weight};
            if (count < 2)
                best[count++] = f;
            else if (before(f, best[1]))
                best[1] = f;
            if (count == 2 && before(best[1], best[0]))
                std::swap(best[0], best[1]);
        }
        if (count == 0)
            return 0;
        if (usage == ReflectionProbeUsage::Simple)
        {
            out[0] = {best[0].probe, 1.0f};
            return 1;
        }
        f32 w0 = best[0].weight;
        f32 w1 = count > 1 ? (1.0f - w0) * best[1].weight : 0.0f;
        if (usage == ReflectionProbeUsage::BlendProbes)
        {
            f32 sum = w0 + w1;
            if (sum > 0)
                w0 /= sum, w1 /= sum;
            else
                w0 = 1, w1 = 0;
        }
        out[0] = {best[0].probe, w0};
        if (count < 2)
            return 1;
        out[1] = {best[1].probe, w1};
        return 2;
    }
}
