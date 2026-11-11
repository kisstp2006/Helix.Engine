#pragma once

#include <Helix/Core/Strings/StringView.h>
#include <Helix/Math/Matrix4x4.h>
#include <Helix/Math/Quaternion.h>
#include <Helix/Math/Vector.h>

// Transform data: every root and its descendants live in one block, the nodes in depth first order, so a subtree is one
// contiguous range and parents come before their children. Systems read the blocks directly (world matrices, changes);
// only the object model's thread changes their structure.
namespace helix::engine
{
    class Transform;

    struct LocalTRS
    {
        Vector3 position = Vector3(0, 0, 0);
        Quaternion rotation = Quaternion::identity();
        Vector3 scale = Vector3(1, 1, 1);
    };

    namespace TransformFlags
    {
        // The cached world matrix and rotation are stale.
        inline constexpr u8 WorldDirty = 1;
        // Transform.hasChanged.
        inline constexpr u8 HasChanged = 2;
        // Destroyed together with an ancestor: the node goes when the ancestor's range is removed.
        inline constexpr u8 Dead = 4;
    }

    struct TransformHierarchy
    {
        // Side by side arrays of `capacity` nodes, `count` in use.
        Matrix4x4* world = nullptr;
        Quaternion* worldRotation = nullptr;
        LocalTRS* local = nullptr;
        Transform** transforms = nullptr;
        // The parent's node, -1 for the root.
        i32* parent = nullptr;
        // Nodes in the subtree, the node itself included (the next sibling is at index + size).
        u32* size = nullptr;
        // Live direct children.
        u32* children = nullptr;
        // Bits of the systems that want to hear about changes of the node, and the ones that have not yet.
        u32* interest = nullptr;
        u32* changed = nullptr;
        u8* flags = nullptr;
        u32 count = 0;
        u32 capacity = 0;
        // The OR of `changed`.
        u32 changedSystems = 0;
        // Some node has WorldDirty.
        bool worldDirty = false;
        bool linked = false;
        // Places in the lists of hierarchies with stale world matrices and with pending changes.
        u32 worldSlot = ~0u;
        u32 changeSlot = ~0u;
        // The roots in their sibling order.
        TransformHierarchy* previousRoot = nullptr;
        TransformHierarchy* nextRoot = nullptr;
        void* block = nullptr;
    };

    // Brings the stale world matrices of one hierarchy up to date (a forward loop: parents are done before children).
    void UpdateWorldMatrices(TransformHierarchy& hierarchy);
    // The same for every hierarchy with stale matrices, hierarchies in parallel on Helix.Jobs when it runs.
    void UpdateWorldMatrices();

    inline constexpr u32 kMaxTransformSystems = 32;
    inline constexpr u32 kNoTransformSystem = ~0u;

    // A system that wants to hear about transform changes (renderers, physics, audio): an interest bit. The same name
    // gives the same bit; kNoTransformSystem once all 32 are taken.
    u32 RegisterTransformSystem(StringView name);
    void SetTransformInterest(Transform* transform, u32 system, bool interested);
    bool TransformInterest(const Transform* transform, u32 system);

    using TransformVisitor = void (*)(TransformHierarchy& hierarchy, u32 index, void* user);
    // Calls `visit` for every transform the system is interested in that changed (itself or an ancestor) since the
    // system's last dispatch, with the world matrices up to date, and clears those changes. With `parallel` the
    // hierarchies are spread over Helix.Jobs: `visit` then runs on worker threads, may read the hierarchy and must
    // write only data of its own per node.
    void DispatchTransformChanges(u32 system, TransformVisitor visit, void* user, bool parallel = true);
    template <class F>
    void DispatchTransformChanges(u32 system, const F& f, bool parallel = true)
    {
        DispatchTransformChanges(
            system, [](TransformHierarchy& h, u32 i, void* user) { (*static_cast<const F*>(user))(h, i); },
            const_cast<F*>(&f), parallel);
    }

    // Root hierarchies, in their sibling order.
    TransformHierarchy* FirstRootHierarchy();
    u32 RootHierarchyCount();
}
