#pragma once

#include <Helix/Engine/Rendering/Camera.h>
#include <Helix/Math/Geometry.h>

// CullingGroup: spheres of the caller tested against a camera's view and sorted into distance bands each time that
// camera culls; changes of a sphere's state are reported through a callback (C# routes it to onStateChanged).
namespace helix::engine
{
    struct BoundingSphere
    {
        Vector3 position;
        f32 radius = 0;

        BoundingSphere() = default;
        BoundingSphere(Vector3 p, f32 r) : position(p), radius(r) {}
        explicit BoundingSphere(Vector4 v) : position(v.x, v.y, v.z), radius(v.w) {}
    };
    static_assert(sizeof(BoundingSphere) == 16, "BoundingSphere: the layout C# hands over");

    // A sphere's change: its state bytes hold visibility in the top bit and the distance band below it.
    struct CullingGroupEvent
    {
        static constexpr u8 kVisibleFlag = 0x80;
        static constexpr u8 kDistanceMask = 0x7F;
        i32 index = 0;
        u8 prevState = 0;
        u8 thisState = 0;

        bool isVisible() const { return (thisState & kVisibleFlag) != 0; }
        bool wasVisible() const { return (prevState & kVisibleFlag) != 0; }
        bool hasBecomeVisible() const { return isVisible() && !wasVisible(); }
        bool hasBecomeInvisible() const { return !isVisible() && wasVisible(); }
        i32 currentDistance() const { return thisState & kDistanceMask; }
        i32 previousDistance() const { return prevState & kDistanceMask; }
    };

    class CullingGroup
    {
    public:
        using StateChanged = void (*)(void* user, const CullingGroupEvent& e);

        CullingGroup();
        ~CullingGroup();
        CullingGroup(const CullingGroup&) = delete;
        CullingGroup& operator=(const CullingGroup&) = delete;

        static CullingGroup* Find(u64 id);
        u64 Id() const { return id_; }

        void SetStateChanged(StateChanged callback, void* user)
        {
            callback_ = callback;
            user_ = user;
        }
        bool enabled() const { return enabled_; }
        void SetEnabled(bool value) { enabled_ = value; }
        Camera* targetCamera() const;
        void SetTargetCamera(Camera* camera);

        // The spheres stay the caller's: they are read each time the target camera culls.
        void SetBoundingSpheres(const BoundingSphere* spheres, i32 capacity);
        void SetBoundingSphereCount(i32 count);
        // The last sphere's state moves into the erased one's place (the caller moves the sphere the same way).
        void EraseSwapBack(i32 index);
        // Indices of spheres by visibility, by distance band or both, from the first index; returns how many were written.
        i32 QueryIndices(bool visible, Span<i32> result, i32 firstIndex) const;
        i32 QueryIndices(bool visible, i32 distanceIndex, Span<i32> result, i32 firstIndex) const;
        i32 QueryIndices(i32 distanceIndex, Span<i32> result, i32 firstIndex) const;
        bool IsVisible(i32 index) const;
        i32 GetDistance(i32 index) const;
        void SetBoundingDistances(Span<const f32> distances);
        void SetDistanceReferencePoint(Vector3 point);
        void SetDistanceReferencePoint(Transform* transform);

        // The target camera culled with these planes: new states, the callback for each change.
        void Evaluate(const Plane* planes, i32 count);

    private:
        bool Valid(i32 index, const char* what) const;

        u64 id_ = 0;
        bool enabled_ = true;
        EntityId camera_;
        const BoundingSphere* spheres_ = nullptr;
        i32 capacity_ = 0;
        i32 count_ = 0;
        Array<u8> states_ = Array<u8>();
        Array<f32> distances_ = Array<f32>();
        Vector3 referencePoint_;
        EntityId referenceTransform_;
        StateChanged callback_ = nullptr;
        void* user_ = nullptr;
    };

    namespace render
    {
        // The groups whose target is this camera, after it culled.
        void EvaluateCullingGroups(EntityId camera, const Plane* planes, i32 count);
    }
}
