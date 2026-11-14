#pragma once

#include <Helix/Engine/Math/MathTransfer.h>

#include <Helix/Engine/GameObject/GameObject.h>
#include <Helix/Engine/Rendering/Renderer.h>

// LODGroup: renderers grouped by levels of detail, one level drawn by its size on the screen. Culling picks the level
// per camera (Rendering/Pipeline: the renderers of other levels are not visible).
namespace helix::engine
{
    namespace ClassId
    {
        inline constexpr i32 LODGroup = 205;
    }

    HX_ENUM(Namespace = "HelixEngine")
    enum class LODFadeMode : i32
    {
        None = 0,
        CrossFade = 1,
        SpeedTree = 2,
    };

    // One level: drawn while the group's height on the screen (relative to the screen's) is at least the transition
    // height and below the previous level's.
    struct LOD
    {
        static constexpr StringView kTypeName = "LOD";
        f32 screenRelativeTransitionHeight = 0;
        f32 fadeTransitionWidth = 0;
        Array<PPtr<Renderer>> renderers = Array<PPtr<Renderer>>();

        LOD() = default;
        LOD(f32 height, Span<Renderer* const> list) : screenRelativeTransitionHeight(height)
        {
            for (Renderer* r : list)
                renderers.Push(PPtr<Renderer>(r));
        }

        template <class V>
        void Transfer(V& v)
        {
            v.Field("screenRelativeHeight", screenRelativeTransitionHeight);
            v.Field("fadeTransitionWidth", fadeTransitionWidth);
            v.Field("renderers", renderers);
        }
    };

    HX_CLASS(Namespace = "HelixEngine", Sealed)
    class LODGroup final : public Component
    {
        HX_ENGINE_CLASS(LODGroup, Component, ClassId::LODGroup, ClassDisallowMultiple)

    public:
        // The most levels a group has.
        static constexpr u32 kMaxLODs = 8;

        LODGroup() = default;
        LODGroup(const LODGroup&) = default;

        HX_PROPERTY() Vector3 localReferencePoint() const { return localReferencePoint_; }
        void SetLocalReferencePoint(Vector3 value);
        HX_PROPERTY() f32 size() const { return size_; }
        void SetSize(f32 value);
        HX_PROPERTY(ReadOnly) i32 lodCount() const { return i32(lods_.Size()); }
        HX_PROPERTY() LODFadeMode fadeMode() const { return fadeMode_; }
        void SetFadeMode(LODFadeMode value) { fadeMode_ = value; }
        HX_PROPERTY() bool animateCrossFading() const { return animateCrossFading_; }
        void SetAnimateCrossFading(bool value) { animateCrossFading_ = value; }
        HX_PROPERTY() bool lastLODBillboard() const { return lastLODBillboard_; }
        void SetLastLODBillboard(bool value) { lastLODBillboard_ = value; }
        HX_PROPERTY() bool enabled() const { return enabled_; }
        void SetEnabled(bool value);
        HX_PROPERTY() static f32 crossFadeAnimationDuration();
        static void SetCrossFadeAnimationDuration(f32 value);

        Span<const LOD> GetLODs() const { return lods_; }
        // More than kMaxLODs levels, or heights that do not fall, are refused with an error.
        void SetLODs(Span<const LOD> lods);
        // The reference point and size from the renderers' bounds.
        HX_FUNCTION() void RecalculateBounds();
        // Draws one level whatever the distance (negative: back to the distance).
        HX_FUNCTION() void ForceLOD(i32 index);
        i32 forcedLOD() const { return forced_; }

        void OnEnableNative();
        void OnDisableNative();
        void OnDestroyNative();
        void OnLoadedNative() { Assign(); }

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("m_LocalReferencePoint", localReferencePoint_);
            v.Field("m_Size", size_);
            v.Field("m_FadeMode", fadeMode_);
            v.Field("m_AnimateCrossFading", animateCrossFading_);
            v.Field("m_LastLODIsBillboard", lastLODBillboard_);
            v.Field("m_LODs", lods_);
            v.Field("m_Enabled", enabled_);
        }

    private:
        // Marks the renderers of the levels with this group's slot (or clears them).
        void Assign();
        void Unassign();

        Vector3 localReferencePoint_;
        f32 size_ = 1;
        LODFadeMode fadeMode_ = LODFadeMode::None;
        bool animateCrossFading_ = false;
        bool lastLODBillboard_ = false;
        bool enabled_ = true;
        i32 forced_ = -1;
        u32 slot_ = ~0u;
        Array<LOD> lods_ = Array<LOD>();
    };

    namespace render
    {
        // The enabled groups by slot, as culling reads them: the level a camera draws, ~0 when none (too small).
        struct LODSelection
        {
            Vector3 cameraPosition;
            f32 fieldOfView = 60;
            f32 orthoSize = 5;
            bool orthographic = false;
            f32 bias = 1;
            i32 maximumLevel = 0;
        };
        u32 LODGroupSlots();
        void SelectLODs(const LODSelection& selection, Array<u8>& levels);
    }
}
