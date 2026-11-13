#pragma once

#include <Helix/Engine/Transform/Transform.h>

namespace helix::engine
{
    // The hierarchy blocks' structure, for Transform, GameObject and Lifecycle.
    struct TransformAccess
    {
        static TransformHierarchy*& Block(Transform& t) { return t.hierarchy_; }
        static u32& Index(Transform& t) { return t.index_; }

        // A new root hierarchy holding only `t`, at the end of the roots of the scene new roots go into.
        static void CreateRoot(Transform* t);
        // The scene new roots go into (the active scene, or the scene being loaded).
        static u32& NewRootScene();
        // The hierarchy as the last root of another scene.
        static void MoveToScene(TransformHierarchy& h, u32 scene);
        // Takes the node out: marked dead when its parent is being destroyed too (the parent's range goes later),
        // otherwise its range goes now; the hierarchy goes with its root.
        static void Release(Transform* t);
        // Moves t's subtree under `parent` (null: a root) as its child number `sibling` (past the end: the last), or
        // before root number `sibling` among the roots. Local values stay; no checks, no activation.
        static void Place(Transform* t, Transform* parent, u32 sibling);
        // Sets local values so the world matrix and rotation become these (scale as far as the parent allows).
        static void SetWorld(Transform* t, const Matrix4x4& world, Quaternion rotation);
        // The changed and world dirty marks over the subtree of node i.
        static void MarkChanged(TransformHierarchy& h, u32 i);
        // The same from a worker that owns the hierarchy for a job (the lists of all hierarchies under a lock).
        static void MarkChangedFromWorker(TransformHierarchy& h, u32 i);
        // A hierarchy for `clones`, a copy of `originals` (whole subtrees of one hierarchy, in node order, live nodes
        // only): parents, local values and order; a new root at the end of the roots.
        static void BuildClone(Span<Transform* const> originals, Span<Transform* const> clones, u32 scene);
        // One block for transforms read from a file, each still a root of its own: `nodes` in depth first order,
        // `parents` their parents' places in `nodes` (-1 for the first); a new root at the end of the scene's roots.
        static void BuildLoaded(Span<Transform* const> nodes, Span<const i32> parents, u32 scene);

        // The block reallocated for `capacity` nodes (never below the nodes in use).
        static void Resize(TransformHierarchy& h, u32 capacity);
        // The scale left in m (upper 3x3) once the rotation is taken out: the diagonal of inverse(rotation) * m.
        static Vector3 ScaleOf(const Matrix4x4& m, Quaternion rotation);

        // Activation hooks are running: the hierarchy must not change.
        static u32& Activating();

        // Calls f(index) for the live direct children of node i, in order.
        template <class F>
        static void ForEachChild(const TransformHierarchy& h, u32 i, F&& f)
        {
            for (u32 j = i + 1, end = i + h.size[i]; j < end; j += h.size[j])
                if (!(h.flags[j] & TransformFlags::Dead))
                    f(j);
        }
    };
}
