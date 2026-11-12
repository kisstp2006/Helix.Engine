#pragma once

#include <Helix/Engine/GameObject/GameObject.h>
#include <Helix/Engine/Math/MathTransfer.h>
#include <Helix/Engine/Transform/Hierarchy.h>

// Transform: position, rotation and scale in a hierarchy. Its data lives in the hierarchy blocks of Hierarchy.h; the
// object keeps its block and node. World values are cached and recomputed when read after a change.
namespace helix::engine
{
    HX_ENUM(Namespace = "HelixEngine")
    enum class Space : i32
    {
        World = 0,
        Self = 1,
    };

    struct TransformHandle;
    class Transform;

    // The parent and children a Transform read from a file, for the scene builder to link.
    struct TransformLinks
    {
        Transform* transform;
        PPtr<Transform> father;
        Array<PPtr<Transform>> children;
    };

    namespace detail
    {
        // Set by the scene builder while it reads a file (main thread only).
        extern HX_IMPORT_DATA Array<TransformLinks>* gTransformLinks;
        // Called after SetParent moved a Transform (the script runtime's OnTransformParentChanged / ChildrenChanged).
        extern HX_IMPORT_DATA void (*gHierarchyChanged)(Transform* moved, Transform* oldParent, Transform* newParent);
    }

    HX_CLASS()
    class Transform final : public Component
    {
        HX_ENGINE_CLASS(Transform, Component, ClassId::Transform, ClassDisallowMultiple)

    public:
        // A root of its own.
        Transform();
        // The copy gets its node from Instantiate.
        Transform(const Transform& other) : Component(other) {}
        Transform& operator=(const Transform&) = delete;
        ~Transform() override;

        // ---------------------------------------------------------------------------------------------- hierarchy
        HX_PROPERTY(Setter = "SetParentTransform") Transform* parent() const
        {
            i32 p = hierarchy_->parent[index_];
            return p < 0 ? nullptr : hierarchy_->transforms[p];
        }
        // Under `parent` (null: a root) as its last child (SetParent(p) is the parent setter). With worldPositionStays the world position, rotation and
        // scale stay (as far as the new parent's scale allows); otherwise the local values do. False for cycles and
        // while the hierarchy cannot change (objects being destroyed, activation hooks running).
        bool SetParent(Transform* parent, bool worldPositionStays = true);
        void SetParentTransform(Transform* value) { SetParent(value, true); }
        HX_FUNCTION(Name = "SetParent") void SetParentApi(Transform* p) { SetParent(p, true); }
        HX_FUNCTION(Name = "SetParent") void SetParentApi(Transform* p, bool worldPositionStays) { SetParent(p, worldPositionStays); }
        HX_PROPERTY(ReadOnly) Transform* root() const { return hierarchy_->transforms[0]; }
        u32 childCount() const { return hierarchy_->children[index_]; }
        HX_PROPERTY(Name = "childCount", ReadOnly) i32 ChildCountApi() const { return i32(childCount()); }
        Transform* GetChild(u32 index) const;
        HX_FUNCTION(Name = "GetChild") Transform* GetChildApi(i32 index) const { return index >= 0 ? GetChild(u32(index)) : nullptr; }
        // Every child becomes a root, keeping its world placement.
        HX_FUNCTION() void DetachChildren();
        // The place among the parent's children (among the roots for a root).
        HX_FUNCTION() i32 GetSiblingIndex() const;
        HX_FUNCTION() void SetSiblingIndex(i32 index);
        HX_FUNCTION() void SetAsFirstSibling() { SetSiblingIndex(0); }
        HX_FUNCTION() void SetAsLastSibling();
        // This transform is `parent` or below it.
        HX_FUNCTION() bool IsChildOf(const Transform* parent) const
        {
            if (!parent || parent->hierarchy_ != hierarchy_)
                return false;
            return index_ - parent->index_ < hierarchy_->size[parent->index_];
        }
        // A descendant by name or path ("a/b").
        HX_FUNCTION() Transform* Find(StringView path) const;
        // Nodes the hierarchy has room for before it repacks, and nodes in it.
        u32 hierarchyCapacity() const { return hierarchy_->capacity; }
        HX_PROPERTY(Name = "hierarchyCapacity", Setter = "SetHierarchyCapacityApi") i32 HierarchyCapacityApi() const { return i32(hierarchyCapacity()); }
        void SetHierarchyCapacityApi(i32 value) { SetHierarchyCapacity(value > 0 ? u32(value) : 0u); }
        void SetHierarchyCapacity(u32 capacity);
        u32 hierarchyCount() const { return hierarchy_->count; }
        HX_PROPERTY(Name = "hierarchyCount", ReadOnly) i32 HierarchyCountApi() const { return i32(hierarchyCount()); }
        // Set by every change of the transform or an ancestor; only user code clears it.
        HX_PROPERTY() bool hasChanged() const { return (hierarchy_->flags[index_] & TransformFlags::HasChanged) != 0; }
        void SetHasChanged(bool value);

        // ---------------------------------------------------------------------------------------------- local
        const LocalTRS& Local() const { return hierarchy_->local[index_]; }
        HX_PROPERTY() Vector3 localPosition() const { return Local().position; }
        HX_PROPERTY() Quaternion localRotation() const { return Local().rotation; }
        HX_PROPERTY() Vector3 localScale() const { return Local().scale; }
        HX_PROPERTY() Vector3 localEulerAngles() const { return Local().rotation.eulerAngles(); }
        void SetLocalPosition(Vector3 value);
        void SetLocalRotation(Quaternion value);
        void SetLocalScale(Vector3 value);
        void SetLocalEulerAngles(Vector3 degrees) { SetLocalRotation(Quaternion::Euler(degrees)); }
        HX_FUNCTION(Out = "position, rotation") void GetLocalPositionAndRotation(Vector3& position, Quaternion& rotation) const
        {
            position = Local().position;
            rotation = Local().rotation;
        }
        HX_FUNCTION() void SetLocalPositionAndRotation(Vector3 position, Quaternion rotation);
        void SetLocal(const LocalTRS& value);

        // ---------------------------------------------------------------------------------------------- world
        HX_PROPERTY(ReadOnly) const Matrix4x4& localToWorldMatrix() const
        {
            if (hierarchy_->flags[index_] & TransformFlags::WorldDirty) [[unlikely]]
                RefreshWorld();
            return hierarchy_->world[index_];
        }
        HX_PROPERTY(ReadOnly) Matrix4x4 worldToLocalMatrix() const { return localToWorldMatrix().inverse(); }
        HX_PROPERTY() Vector3 position() const { return localToWorldMatrix().GetPosition(); }
        HX_PROPERTY() Quaternion rotation() const
        {
            if (hierarchy_->flags[index_] & TransformFlags::WorldDirty) [[unlikely]]
                RefreshWorld();
            return hierarchy_->worldRotation[index_];
        }
        HX_PROPERTY() Vector3 eulerAngles() const { return rotation().eulerAngles(); }
        // The world scale with the world rotation taken out (skew from rotated non-uniform parents is lost).
        HX_PROPERTY(ReadOnly) Vector3 lossyScale() const;
        HX_PROPERTY() Vector3 right() const { return rotation() * Vector3::right; }
        HX_PROPERTY() Vector3 up() const { return rotation() * Vector3::up; }
        HX_PROPERTY() Vector3 forward() const { return rotation() * Vector3::forward; }
        void SetPosition(Vector3 value);
        void SetRotation(Quaternion value);
        void SetEulerAngles(Vector3 degrees) { SetRotation(Quaternion::Euler(degrees)); }
        void SetRight(Vector3 value) { SetRotation(Quaternion::FromToRotation(Vector3::right, value)); }
        void SetUp(Vector3 value) { SetRotation(Quaternion::FromToRotation(Vector3::up, value)); }
        void SetForward(Vector3 value) { SetRotation(Quaternion::LookRotation(value)); }
        HX_FUNCTION(Out = "position, rotation") void GetPositionAndRotation(Vector3& position, Quaternion& rotation) const
        {
            position = this->position();
            rotation = this->rotation();
        }
        // One change instead of two.
        HX_FUNCTION() void SetPositionAndRotation(Vector3 position, Quaternion rotation);

        // ---------------------------------------------------------------------------------------------- movement
        // Turns the forward axis to the point; the up axis toward worldUp.
        HX_FUNCTION() void LookAt(Vector3 worldPosition, Vector3 worldUp);
        HX_FUNCTION() void LookAt(Vector3 worldPosition) { LookAt(worldPosition, Vector3::up); }
        HX_FUNCTION() void LookAt(const Transform* target, Vector3 worldUp);
        HX_FUNCTION() void LookAt(const Transform* target) { LookAt(target, Vector3::up); }
        // Euler angles in degrees, around the local axes (Self) or the world axes (World).
        HX_FUNCTION() void Rotate(Vector3 eulers, Space relativeTo = Space::Self);
        HX_FUNCTION() void Rotate(f32 xAngle, f32 yAngle, f32 zAngle, Space relativeTo = Space::Self) { Rotate(Vector3(xAngle, yAngle, zAngle), relativeTo); }
        HX_FUNCTION() void Rotate(Vector3 axis, f32 angle, Space relativeTo);
        // Around the axis through the world point.
        HX_FUNCTION() void RotateAround(Vector3 point, Vector3 axis, f32 angle);
        HX_FUNCTION() void Translate(Vector3 translation, Space relativeTo = Space::Self);
        HX_FUNCTION() void Translate(f32 x, f32 y, f32 z, Space relativeTo = Space::Self) { Translate(Vector3(x, y, z), relativeTo); }
        // Along the axes of relativeTo (the world axes when null).
        HX_FUNCTION() void Translate(Vector3 translation, const Transform* relativeTo);
        HX_FUNCTION() void Translate(f32 x, f32 y, f32 z, const Transform* relativeTo) { Translate(Vector3(x, y, z), relativeTo); }

        // ---------------------------------------------------------------------------------------------- conversions
        // Points take position, rotation and scale; directions only rotation; vectors rotation and scale.
        HX_FUNCTION() Vector3 TransformPoint(Vector3 point) const { return localToWorldMatrix().MultiplyPoint3x4(point); }
        HX_FUNCTION() Vector3 TransformDirection(Vector3 direction) const { return rotation() * direction; }
        HX_FUNCTION() Vector3 TransformVector(Vector3 vector) const { return localToWorldMatrix().MultiplyVector(vector); }
        HX_FUNCTION() Vector3 InverseTransformPoint(Vector3 point) const { return worldToLocalMatrix().MultiplyPoint3x4(point); }
        HX_FUNCTION() Vector3 InverseTransformDirection(Vector3 direction) const { return Quaternion::Inverse(rotation()) * direction; }
        HX_FUNCTION() Vector3 InverseTransformVector(Vector3 vector) const { return worldToLocalMatrix().MultiplyVector(vector); }
        // In place, or from one span into another of the same size.
        void TransformPoints(Span<Vector3> points) const { TransformPoints(points, points); }
        void TransformPoints(Span<const Vector3> points, Span<Vector3> out) const;
        void TransformDirections(Span<Vector3> directions) const { TransformDirections(directions, directions); }
        void TransformDirections(Span<const Vector3> directions, Span<Vector3> out) const;
        void TransformVectors(Span<Vector3> vectors) const { TransformVectors(vectors, vectors); }
        void TransformVectors(Span<const Vector3> vectors, Span<Vector3> out) const;
        void InverseTransformPoints(Span<Vector3> points) const { InverseTransformPoints(points, points); }
        void InverseTransformPoints(Span<const Vector3> points, Span<Vector3> out) const;
        void InverseTransformDirections(Span<Vector3> directions) const { InverseTransformDirections(directions, directions); }
        void InverseTransformDirections(Span<const Vector3> directions, Span<Vector3> out) const;
        void InverseTransformVectors(Span<Vector3> vectors) const { InverseTransformVectors(vectors, vectors); }
        void InverseTransformVectors(Span<const Vector3> vectors, Span<Vector3> out) const;

        // The block and node of this transform (they move when the hierarchy changes).
        TransformHierarchy& Hierarchy() const { return *hierarchy_; }
        u32 NodeIndex() const { return index_; }

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            LocalTRS l = Local();
            v.Field("m_LocalRotation", l.rotation);
            v.Field("m_LocalPosition", l.position);
            v.Field("m_LocalScale", l.scale);
            // Scene files list the children and the parent; the scene builder links them after reading.
            Array<PPtr<Transform>> children;
            PPtr<Transform> father;
            if constexpr (!V::kReading)
            {
                father = PPtr<Transform>(parent());
                for (u32 i = 0; i < childCount(); i++)
                    children.Push(PPtr<Transform>(GetChild(i)));
            }
            v.Field("m_Children", children);
            v.Field("m_Father", father);
            if constexpr (V::kReading)
            {
                SetLocal(l);
                if constexpr (!requires { V::kPartial; })
                    if (detail::gTransformLinks)
                        detail::gTransformLinks->Push({this, father, Move(children)});
            }
        }

    private:
        friend struct TransformAccess;
        void RefreshWorld() const;

        TransformHierarchy* hierarchy_ = nullptr;
        u32 index_ = 0;
    };
}

#include <Helix/Engine/Transform/TransformHandle.h>
