#pragma once

#include <Helix/Engine/Rendering/CullingGroup.h>
#include <Helix/Engine/Rendering/LODGroup.h>
#include <Helix/Engine/Rendering/Pipeline/RenderPipeline.h>

// What C#'s CullingGroup, LODGroup levels and the LOD quality settings call (internal).
namespace helix::engine
{
    HX_CLASS(Namespace = "HelixEngine.Internal", Internal, Static)
    class CullingNative
    {
    public:
        // ------------------------------------------------------------------------------------------ CullingGroup

        // `callback`: an unmanaged function (user, index, previous state, state) called for each change.
        HX_FUNCTION() static u64 CreateGroup(u64 callback, u64 user)
        {
            auto* route = new Route{callback, user};
            auto* g = new CullingGroup();
            g->SetStateChanged(
                [](void* r, const CullingGroupEvent& e) {
                    auto* to = static_cast<Route*>(r);
                    if (to->callback)
                        reinterpret_cast<void (*)(u64, i32, u8, u8)>(to->callback)(to->user, e.index, e.prevState, e.thisState);
                },
                route);
            Routes().Set(g->Id(), route);
            return g->Id();
        }
        HX_FUNCTION() static void DisposeGroup(u64 id)
        {
            delete CullingGroup::Find(id);
            if (Route** r = Routes().Find(id))
            {
                delete *r;
                Routes().Remove(id);
            }
        }
        HX_FUNCTION() static bool GroupEnabled(u64 id) { return Group(id) && Group(id)->enabled(); }
        HX_FUNCTION() static void SetGroupEnabled(u64 id, bool value)
        {
            if (CullingGroup* g = Group(id))
                g->SetEnabled(value);
        }
        HX_FUNCTION() static Camera* GroupCamera(u64 id) { return Group(id) ? Group(id)->targetCamera() : nullptr; }
        HX_FUNCTION() static void SetGroupCamera(u64 id, Camera* camera)
        {
            if (CullingGroup* g = Group(id))
                g->SetTargetCamera(camera);
        }
        // `spheres`: pinned memory of `capacity` spheres (position and radius, 16 bytes each).
        HX_FUNCTION() static void SetBoundingSpheres(u64 id, u64 spheres, i32 capacity)
        {
            if (CullingGroup* g = Group(id))
                g->SetBoundingSpheres(reinterpret_cast<const BoundingSphere*>(spheres), capacity);
        }
        HX_FUNCTION() static void SetBoundingSphereCount(u64 id, i32 count)
        {
            if (CullingGroup* g = Group(id))
                g->SetBoundingSphereCount(count);
        }
        HX_FUNCTION() static void EraseSwapBack(u64 id, i32 index)
        {
            if (CullingGroup* g = Group(id))
                g->EraseSwapBack(index);
        }
        // `mode` 0: by visibility, 1: by visibility and distance, 2: by distance; `result` holds `length` ints.
        HX_FUNCTION() static i32 QueryIndices(u64 id, i32 mode, bool visible, i32 distanceIndex, u64 result, i32 length, i32 firstIndex)
        {
            CullingGroup* g = Group(id);
            if (!g || length <= 0)
                return 0;
            Span<i32> out(reinterpret_cast<i32*>(result), usize(length));
            return mode == 0 ? g->QueryIndices(visible, out, firstIndex)
                   : mode == 1 ? g->QueryIndices(visible, distanceIndex, out, firstIndex)
                               : g->QueryIndices(distanceIndex, out, firstIndex);
        }
        HX_FUNCTION() static bool IsVisible(u64 id, i32 index) { return Group(id) && Group(id)->IsVisible(index); }
        HX_FUNCTION() static i32 GetDistance(u64 id, i32 index) { return Group(id) ? Group(id)->GetDistance(index) : 0; }
        HX_FUNCTION() static void SetBoundingDistances(u64 id, Span<const f32> distances)
        {
            if (CullingGroup* g = Group(id))
                g->SetBoundingDistances(distances);
        }
        HX_FUNCTION() static void SetReferencePoint(u64 id, Vector3 point)
        {
            if (CullingGroup* g = Group(id))
                g->SetDistanceReferencePoint(point);
        }
        HX_FUNCTION() static void SetReferenceTransform(u64 id, Transform* transform)
        {
            if (CullingGroup* g = Group(id))
                g->SetDistanceReferencePoint(transform);
        }

        // ------------------------------------------------------------------------------------------ LODGroup levels

        // x: the transition height, y: the fade width.
        HX_FUNCTION() static Vector2 LODValues(LODGroup* group, i32 index)
        {
            if (!group || index < 0 || index >= group->lodCount())
                return {};
            const LOD& l = group->GetLODs()[u32(index)];
            return {l.screenRelativeTransitionHeight, l.fadeTransitionWidth};
        }
        HX_FUNCTION() static Array<Renderer*> LODRenderers(LODGroup* group, i32 index)
        {
            Array<Renderer*> out;
            if (group && index >= 0 && index < group->lodCount())
                for (const PPtr<Renderer>& r : group->GetLODs()[u32(index)].renderers)
                    out.Push(r.Get());
            return out;
        }
        // SetLODs from C#: the levels one by one, then applied.
        HX_FUNCTION() static void BeginLODs() { Staged().Clear(); }
        HX_FUNCTION() static void AddLOD(f32 height, f32 fade)
        {
            LOD& l = Staged().Emplace();
            l.screenRelativeTransitionHeight = height;
            l.fadeTransitionWidth = fade;
        }
        HX_FUNCTION() static void AddLODRenderer(Renderer* renderer)
        {
            if (!Staged().Empty())
                Staged().Back().renderers.Push(PPtr<Renderer>(renderer));
        }
        HX_FUNCTION() static void ApplyLODs(LODGroup* group)
        {
            if (group)
                group->SetLODs(Staged());
            Staged().Clear();
        }

        // ------------------------------------------------------------------------------------------ quality

        HX_FUNCTION() static bool Batching() { return GraphicsSettings::useScriptableRenderPipelineBatching(); }
        HX_FUNCTION() static void SetBatching(bool value) { GraphicsSettings::SetUseScriptableRenderPipelineBatching(value); }
        HX_FUNCTION() static i32 SortMode() { return i32(GraphicsSettings::transparencySortMode()); }
        HX_FUNCTION() static void SetSortMode(i32 mode) { GraphicsSettings::SetTransparencySortMode(TransparencySortMode(mode)); }
        HX_FUNCTION() static Vector3 SortAxis() { return GraphicsSettings::transparencySortAxis(); }
        HX_FUNCTION() static void SetSortAxis(Vector3 axis) { GraphicsSettings::SetTransparencySortAxis(axis); }
        HX_FUNCTION() static f32 LodBias() { return QualitySettings::lodBias(); }
        HX_FUNCTION() static void SetLodBias(f32 value) { QualitySettings::SetLodBias(value); }
        HX_FUNCTION() static i32 MaximumLODLevel() { return QualitySettings::maximumLODLevel(); }
        HX_FUNCTION() static void SetMaximumLODLevel(i32 value) { QualitySettings::SetMaximumLODLevel(value); }

    private:
        struct Route
        {
            u64 callback;
            u64 user;
        };
        static HashMap<u64, Route*>& Routes()
        {
            static HashMap<u64, Route*> routes;
            return routes;
        }
        static Array<LOD>& Staged()
        {
            static Array<LOD> staged;
            return staged;
        }
        static CullingGroup* Group(u64 id) { return CullingGroup::Find(id); }
    };
}
