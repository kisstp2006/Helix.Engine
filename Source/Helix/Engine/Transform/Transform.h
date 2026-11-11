#pragma once

#include <Helix/Engine/GameObject/GameObject.h>
#include <Helix/Engine/Transform/Hierarchy.h>

// Transform: position, rotation and scale in a hierarchy. Its data lives in the hierarchy blocks of Hierarchy.h; the
// object keeps its block and node. World values are cached and recomputed when read after a change.
namespace helix::engine
{
    enum class Space : u8
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
        extern Array<TransformLinks>* gTransformLinks;
    }

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
        Transform* parent() const
        {
            i32 p = hierarchy_->parent[index_];
            return p < 0 ? nullptr : hierarchy_->transforms[p];
        }
        // Under `parent` (null: a root) as its last child (SetParent(p) is the parent setter). With worldPositionStays the world position, rotation and
        // scale stay (as far as the new parent's scale allows); otherwise the local values do. False for cycles and
        // while the hierarchy cannot change (objects being destroyed, activation hooks running).
        bool SetParent(Transform* parent, bool worldPositionStays = true);
        Transform* root() const { return hierarchy_->transforms[0]; }
        u32 childCount() const { return hierarchy_->children[index_]; }
        Transform* GetChild(u32 index) const;
        // Every child becomes a root, keeping its world placement.
        void DetachChildren();
        // The place among the parent's children (among the roots for a root).
        i32 GetSiblingIndex() const;
        void SetSiblingIndex(i32 index);
        void SetAsFirstSibling() { SetSiblingIndex(0); }
        void SetAsLastSibling();
        // This transform is `parent` or below it.
        bool IsChildOf(const Transform* parent) const
        {
            if (!parent || parent->hierarchy_ != hierarchy_)
                return false;
            return index_ - parent->index_ < hierarchy_->size[parent->index_];
        }
        // A descendant by name or path ("a/b").
        Transform* Find(StringView path) const;
        // Nodes the hierarchy has room for before it repacks, and nodes in it.
        u32 hierarchyCapacity() const { return hierarchy_->capacity; }
        void SetHierarchyCapacity(u32 capacity);
        u32 hierarchyCount() const { return hierarchy_->count; }
        // Set by every change of the transform or an ancestor; only user code clears it.
        bool hasChanged() const { return (hierarchy_->flags[index_] & TransformFlags::HasChanged) != 0; }
        void SetHasChanged(bool value);

        // ---------------------------------------------------------------------------------------------- local
        const LocalTRS& Local() const { return hierarchy_->local[index_]; }
        Vector3 localPosition() const { return Local().position; }
        Quaternion localRotation() const { return Local().rotation; }
        Vector3 localScale() const { return Local().scale; }
        Vector3 localEulerAngles() const { return Local().rotation.eulerAngles(); }
        void SetLocalPosition(Vector3 value);
        void SetLocalRotation(Quaternion value);
        void SetLocalScale(Vector3 value);
        void SetLocalEulerAngles(Vector3 degrees) { SetLocalRotation(Quaternion::Euler(degrees)); }
        void GetLocalPositionAndRotation(Vector3& position, Quaternion& rotation) const
        {
            position = Local().position;
            rotation = Local().rotation;
        }
        void SetLocalPositionAndRotation(Vector3 position, Quaternion rotation);
        void SetLocal(const LocalTRS& value);

        // ---------------------------------------------------------------------------------------------- world
        const Matrix4x4& localToWorldMatrix() const
        {
            if (hierarchy_->flags[index_] & TransformFlags::WorldDirty) [[unlikely]]
                RefreshWorld();
            return hierarchy_->world[index_];
        }
        Matrix4x4 worldToLocalMatrix() const { return localToWorldMatrix().inverse(); }
        Vector3 position() const { return localToWorldMatrix().GetPosition(); }
        Quaternion rotation() const
        {
            if (hierarchy_->flags[index_] & TransformFlags::WorldDirty) [[unlikely]]
                RefreshWorld();
            return hierarchy_->worldRotation[index_];
        }
        Vector3 eulerAngles() const { return rotation().eulerAngles(); }
        // The world scale with the world rotation taken out (skew from rotated non-uniform parents is lost).
        Vector3 lossyScale() const;
        Vector3 right() const { return rotation() * Vector3::right(); }
        Vector3 up() const { return rotation() * Vector3::up(); }
        Vector3 forward() const { return rotation() * Vector3::forward(); }
        void SetPosition(Vector3 value);
        void SetRotation(Quaternion value);
        void SetEulerAngles(Vector3 degrees) { SetRotation(Quaternion::Euler(degrees)); }
        void SetRight(Vector3 value) { SetRotation(Quaternion::FromToRotation(Vector3::right(), value)); }
        void SetUp(Vector3 value) { SetRotation(Quaternion::FromToRotation(Vector3::up(), value)); }
        void SetForward(Vector3 value) { SetRotation(Quaternion::LookRotation(value)); }
        void GetPositionAndRotation(Vector3& position, Quaternion& rotation) const
        {
            position = this->position();
            rotation = this->rotation();
        }
        // One change instead of two.
        void SetPositionAndRotation(Vector3 position, Quaternion rotation);

        // ---------------------------------------------------------------------------------------------- movement
        // Turns the forward axis to the point; the up axis toward worldUp.
        void LookAt(Vector3 worldPosition, Vector3 worldUp = Vector3::up());
        void LookAt(const Transform* target, Vector3 worldUp = Vector3::up());
        // Euler angles in degrees, around the local axes (Self) or the world axes (World).
        void Rotate(Vector3 eulers, Space relativeTo = Space::Self);
        void Rotate(f32 xAngle, f32 yAngle, f32 zAngle, Space relativeTo = Space::Self) { Rotate(Vector3(xAngle, yAngle, zAngle), relativeTo); }
        void Rotate(Vector3 axis, f32 angle, Space relativeTo);
        // Around the axis through the world point.
        void RotateAround(Vector3 point, Vector3 axis, f32 angle);
        void Translate(Vector3 translation, Space relativeTo = Space::Self);
        void Translate(f32 x, f32 y, f32 z, Space relativeTo = Space::Self) { Translate(Vector3(x, y, z), relativeTo); }
        // Along the axes of relativeTo (the world axes when null).
        void Translate(Vector3 translation, const Transform* relativeTo);
        void Translate(f32 x, f32 y, f32 z, const Transform* relativeTo) { Translate(Vector3(x, y, z), relativeTo); }

        // ---------------------------------------------------------------------------------------------- conversions
        // Points take position, rotation and scale; directions only rotation; vectors rotation and scale.
        Vector3 TransformPoint(Vector3 point) const { return localToWorldMatrix().MultiplyPoint3x4(point); }
        Vector3 TransformDirection(Vector3 direction) const { return rotation() * direction; }
        Vector3 TransformVector(Vector3 vector) const { return localToWorldMatrix().MultiplyVector(vector); }
        Vector3 InverseTransformPoint(Vector3 point) const { return worldToLocalMatrix().MultiplyPoint3x4(point); }
        Vector3 InverseTransformDirection(Vector3 direction) const { return Quaternion::Inverse(rotation()) * direction; }
        Vector3 InverseTransformVector(Vector3 vector) const { return worldToLocalMatrix().MultiplyVector(vector); }
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
            Value<4> r{&l.rotation.x};
            Value<3> p{&l.position.x}, s{&l.scale.x};
            v.Field("m_LocalRotation", r);
            v.Field("m_LocalPosition", p);
            v.Field("m_LocalScale", s);
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
                if (detail::gTransformLinks)
                    detail::gTransformLinks->Push({this, father, Move(children)});
            }
        }

    private:
        friend struct TransformAccess;
        // A vector or quaternion as scene files write it: {x: 0, y: 0, z: 0[, w: 1]}.
        template <u32 N>
        struct Value
        {
            static constexpr StringView kTypeName = N == 4 ? StringView("Quaternionf") : StringView("Vector3f");
            static constexpr bool kFlow = true;
            f32* f;
            template <class V>
            void Transfer(V& v)
            {
                const char* names[4] = {"x", "y", "z", "w"};
                for (u32 i = 0; i < N; i++)
                    v.Field(names[i], f[i]);
            }
        };
        void RefreshWorld() const;

        TransformHierarchy* hierarchy_ = nullptr;
        u32 index_ = 0;
    };
}

#include <Helix/Engine/Transform/TransformHandle.h>
