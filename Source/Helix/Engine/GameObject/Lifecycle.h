#pragma once

#include <Helix/Engine/GameObject/GameObject.h>
#include <Helix/Engine/Scene/Scene.h>

// Instantiate, Destroy and their timing: destroys queue until ProcessDestroyQueue, which runs after the update loop.
namespace helix::engine
{
    struct InstantiateParameters
    {
        Transform* parent = nullptr;
        // Under a parent: true keeps the original's world placement, false its local values. Without a parent the
        // clone always takes the original's world placement.
        bool worldSpace = false;
        // The scene of the clone when it has no parent (invalid: the active scene).
        Scene scene;
        // The original does not change while the clone is made (lets the clone be made off the main thread later).
        bool originalImmutable = false;
    };

    // Clones the object: a GameObject or component with the whole GameObject hierarchy (references inside the clone
    // point into the clone, others stay), any other object by itself. The clone of the root is named "<name>(Clone)"
    // and returned for the original passed (the clone of a component is that component's clone).
    Object* Instantiate(Object* original, const InstantiateParameters& parameters = {});
    Object* Instantiate(Object* original, Transform* parent, bool instantiateInWorldSpace = false);
    // At a world position and rotation (also under a parent).
    Object* Instantiate(Object* original, Vector3 position, Quaternion rotation, Transform* parent = nullptr);
    // As a root of `scene`.
    Object* Instantiate(Object* original, Scene scene);
    template <class T>
    T* Instantiate(T* original, const InstantiateParameters& parameters = {})
    {
        return static_cast<T*>(Instantiate(static_cast<Object*>(original), parameters));
    }
    template <class T>
    T* Instantiate(T* original, Transform* parent, bool instantiateInWorldSpace = false)
    {
        return static_cast<T*>(Instantiate(static_cast<Object*>(original), parent, instantiateInWorldSpace));
    }
    template <class T>
    T* Instantiate(T* original, Vector3 position, Quaternion rotation, Transform* parent = nullptr)
    {
        return static_cast<T*>(Instantiate(static_cast<Object*>(original), position, rotation, parent));
    }
    template <class T>
    T* Instantiate(T* original, Scene scene)
    {
        return static_cast<T*>(Instantiate(static_cast<Object*>(original), scene));
    }
    // A clone made over frames: copies integrate on the main thread in ProcessAsyncInstantiations, as many per frame as
    // the integration time allows (each copy enabled when it is made). A handle: an operation finished long ago reads
    // as done.
    struct AsyncInstantiateOperation
    {
        u32 id = 0;

        bool IsValid() const { return id != 0; }
        bool isDone() const;
        // Copies made / count (0.9 while waiting for allowSceneActivation with nothing made).
        f32 progress() const;
        i32 priority() const;
        void SetPriority(i32 value) const;
        bool allowSceneActivation() const;
        void SetAllowSceneActivation(bool value) const;
        // Held by allowSceneActivation with everything else done.
        bool IsWaitingForSceneActivation() const;
        // Makes every copy now (whatever allowSceneActivation says).
        void WaitForCompletion() const;
        // Destroys the copies made so far; the operation is done with no result.
        void Cancel() const;
        // The copies (count of them once done), in order.
        void Result(Array<Object*>& out) const;
        // Called when done; at once when it already is.
        void AddCompleted(void (*callback)(void* user, AsyncInstantiateOperation op), void* user = nullptr) const;

        // The time per frame for making copies, in milliseconds (2 by default).
        static void SetIntegrationTimeMS(f32 milliseconds);
        static f32 GetIntegrationTimeMS();
    };

    // `count` copies; with positions (and rotations) copy i goes to positions[i % size] in world space; the parent and
    // scene of the parameters as for Instantiate.
    AsyncInstantiateOperation InstantiateAsync(Object* original, u32 count, Span<const Vector3> positions, Span<const Quaternion> rotations,
                                               const InstantiateParameters& parameters = {});
    inline AsyncInstantiateOperation InstantiateAsync(Object* original, const InstantiateParameters& parameters = {})
    {
        return InstantiateAsync(original, 1, {}, {}, parameters);
    }
    inline AsyncInstantiateOperation InstantiateAsync(Object* original, u32 count, const InstantiateParameters& parameters = {})
    {
        return InstantiateAsync(original, count, {}, {}, parameters);
    }
    inline AsyncInstantiateOperation InstantiateAsync(Object* original, u32 count, Transform* parent)
    {
        InstantiateParameters p;
        p.parent = parent;
        return InstantiateAsync(original, count, {}, {}, p);
    }
    inline AsyncInstantiateOperation InstantiateAsync(Object* original, Transform* parent) { return InstantiateAsync(original, 1, parent); }
    inline AsyncInstantiateOperation InstantiateAsync(Object* original, u32 count, Vector3 position, Quaternion rotation, Transform* parent = nullptr)
    {
        InstantiateParameters p;
        p.parent = parent;
        return InstantiateAsync(original, count, Span<const Vector3>(&position, 1), Span<const Quaternion>(&rotation, 1), p);
    }
    inline AsyncInstantiateOperation InstantiateAsync(Object* original, Vector3 position, Quaternion rotation, Transform* parent = nullptr)
    {
        return InstantiateAsync(original, 1, position, rotation, parent);
    }
    // Makes the copies that are due within the integration time; the player loop calls it once a frame.
    void ProcessAsyncInstantiations();

    // `count` clones of a GameObject as roots of `scene` (invalid: the active scene); ids and transformIds (count entries
    // each) receive the clones and their Transforms.
    bool InstantiateGameObjects(EntityId source, u32 count, Span<EntityId> ids, Span<EntityId> transformIds, Scene scene = Scene());

    // The object goes after the current Update loop (when the player loop calls ProcessDestroyQueue), or `delay`
    // seconds of scaled time later. A GameObject takes its components and its transform children; a component leaves
    // its GameObject; repeated, late or null destroys do nothing. A Transform cannot be destroyed on its own.
    void Destroy(Object* object, float delay = 0);
    // Now. Assets only with allowDestroyingAssets. False when nothing was destroyed.
    bool DestroyImmediate(Object* object, bool allowDestroyingAssets = false);
    // Root GameObjects (or components on them) and their children move to the DontDestroyOnLoad scene, which Single
    // loads keep.
    bool DontDestroyOnLoad(Object* target);

    // The scaled time Destroy delays count from (the player loop sets it each frame).
    void SetLifecycleTime(double scaledTime);
    double LifecycleTime();
    // Destroys every queued object whose time has come; returns how many roots were destroyed.
    u32 ProcessDestroyQueue();
    u32 PendingDestroyCount();
}
