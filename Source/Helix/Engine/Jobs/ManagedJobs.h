#pragma once

#include <Helix/Engine/Object/EntityId.h>
#include <Helix/Engine/Transform/Transform.h>
#include <Helix/Core/Basics/Tags.h>
#include <Helix/Math/Quaternion.h>
#include <Helix/Math/Matrix4x4.h>

// Jobs and native memory for C# (the C# job system and containers of the script API). A job is a block of bytes the
// C# side wrote (its job struct after a header) that is copied and run on Helix.Jobs workers through one entry point
// of the C# side (an UnmanagedCallersOnly function taking the block and a range); the copy goes when the job is done.
// Handles pack a Helix.Jobs handle into 64 bits (0 is a completed handle). Memory comes per allocator: Temp from an
// arena rewound at the end of each frame, TempJob and Persistent from the heap (TempJob allocations older than four
// frames are reported). Transform jobs run the transforms of one root hierarchy on one worker, so roots run in
// parallel; their reads compute world values without touching shared state and their writes mark the hierarchy.
namespace helix::engine
{
    // The allocators of the script API (Allocator's values).
    enum class ScriptAllocator : i32
    {
        Invalid = 0,
        None = 1,
        Temp = 2,
        TempJob = 3,
        Persistent = 4,
        AudioKernel = 5,
        Domain = 6,
    };

    HX_CLASS(Namespace = "HelixEngine.Internal", Static, Internal)
    class JobsNative
    {
    public:
        // `size` bytes of the block are copied; the entry gets (copy, begin, end): one call over [0, 1) unless parallel,
        // else batches of `batch` over [0, count).
        HX_FUNCTION() static u64 Schedule(u64 entry, u64 block, i32 size, i32 count, i32 batch, bool parallel, u64 dependency);
        // A transform job: one call per transform of the array (begin is the item, see TransformItems), the transforms
        // of one root on one worker; the block's 64 bit word at `itemsOffset` gets the items of the copy.
        HX_FUNCTION() static u64 ScheduleTransforms(u64 entry, u64 block, i32 size, i32 itemsOffset, u64 transforms, u64 dependency);
        HX_FUNCTION() static u64 Combine(u64 a, u64 b);
        HX_FUNCTION() static u64 CombineMany(u64 handles, i32 count);
        HX_FUNCTION() static void Complete(u64 handle);
        HX_FUNCTION(Leaf) static bool IsCompleted(u64 handle);
        HX_FUNCTION(Leaf) static i32 WorkerCount();
        HX_FUNCTION(Leaf) static i32 MaximumWorkerCount();

        HX_FUNCTION(Leaf) static u64 Allocate(i64 size, i32 alignment, i32 allocator);
        HX_FUNCTION(Leaf) static void Free(u64 pointer, i32 allocator);
        // The arena of Temp is rewound (the end of the frame; the player loop calls it); TempJob ages are checked.
        static void EndFrame();
        // Live allocations of an allocator (tests).
        HX_FUNCTION(Leaf) static i32 LiveAllocations(i32 allocator);
    };

    // An item of a transform job: the node, and its place in the TransformAccessArray.
    struct TransformJobItem
    {
        TransformHierarchy* hierarchy;
        u32 node;
        i32 index;
    };

    // TransformAccessArray and TransformAccess (C# keeps the native array's address).
    HX_CLASS(Namespace = "HelixEngine.Internal", Static, Internal)
    class TransformAccessNative
    {
    public:
        HX_FUNCTION() static u64 Create(i32 capacity);
        HX_FUNCTION() static void Destroy(u64 array);
        HX_FUNCTION(Leaf) static i32 Length(u64 array);
        HX_FUNCTION(Leaf) static i32 Capacity(u64 array);
        HX_FUNCTION() static void SetCapacity(u64 array, i32 capacity);
        HX_FUNCTION() static void Add(u64 array, Transform* transform);
        HX_FUNCTION() static void AddId(u64 array, EntityId transform);
        HX_FUNCTION() static void RemoveAtSwapBack(u64 array, i32 index);
        HX_FUNCTION() static Transform* Get(u64 array, i32 index);
        HX_FUNCTION() static EntityId GetId(u64 array, i32 index);
        HX_FUNCTION() static void Set(u64 array, i32 index, Transform* transform);
        HX_FUNCTION() static void SetId(u64 array, i32 index, EntityId transform);
        HX_FUNCTION() static void Clear(u64 array);

        // A transform's hierarchy and node (the main thread's Run of a transform job).
        HX_FUNCTION() static u64 HierarchyOf(Transform* transform) { return transform ? u64(uptr(&transform->Hierarchy())) : 0; }
        HX_FUNCTION() static u32 NodeOf(Transform* transform) { return transform ? transform->NodeIndex() : 0; }

        // From a worker (the node's hierarchy is the worker's for the job): pure reads, and writes that mark it.
        HX_FUNCTION(Leaf) static bool IsValid(u64 hierarchy, u32 node);
        HX_FUNCTION(Leaf) static Vector3 LocalPosition(u64 hierarchy, u32 node);
        HX_FUNCTION(Leaf) static Quaternion LocalRotation(u64 hierarchy, u32 node);
        HX_FUNCTION(Leaf) static Vector3 LocalScale(u64 hierarchy, u32 node);
        HX_FUNCTION(Leaf) static Matrix4x4 LocalToWorld(u64 hierarchy, u32 node);
        HX_FUNCTION(Leaf) static Quaternion WorldRotation(u64 hierarchy, u32 node);
        HX_FUNCTION() static void SetLocal(u64 hierarchy, u32 node, Vector3 position, Quaternion rotation, Vector3 scale);
        HX_FUNCTION() static void SetWorldPositionAndRotation(u64 hierarchy, u32 node, Vector3 position, Quaternion rotation);
    };
}
