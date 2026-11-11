#pragma once

#include <Helix/Engine/GameObject/GameObject.h>

// Instantiate, Destroy and their timing: destroys queue until ProcessDestroyQueue, which runs after the update loop.
namespace helix::engine
{
    struct InstantiateParameters
    {
        Transform* parent = nullptr;
        // Keep the world placement under the new parent (the world math comes with the hierarchy storage).
        bool worldSpace = false;
        // The original does not change while the clone is made (lets the clone be made off the main thread later).
        bool originalImmutable = false;
    };

    // Clones the object: a GameObject or component with the whole GameObject hierarchy (references inside the clone
    // point into the clone, others stay), any other object by itself. The clone of the root is named "<name>(Clone)"
    // and returned for the original passed (the clone of a component is that component's clone).
    Object* Instantiate(Object* original, const InstantiateParameters& parameters = {});
    Object* Instantiate(Object* original, Transform* parent, bool worldPositionStays = false);
    template <class T>
    T* Instantiate(T* original, const InstantiateParameters& parameters = {})
    {
        return static_cast<T*>(Instantiate(static_cast<Object*>(original), parameters));
    }
    // `count` clones of a GameObject; ids and transformIds (count entries each) receive the clones and their Transforms.
    bool InstantiateGameObjects(EntityId source, u32 count, Span<EntityId> ids, Span<EntityId> transformIds);

    // The object goes after the current Update loop (when the player loop calls ProcessDestroyQueue), or `delay`
    // seconds of scaled time later. A GameObject takes its components and its transform children; a component leaves
    // its GameObject; repeated, late or null destroys do nothing. A Transform cannot be destroyed on its own.
    void Destroy(Object* object, float delay = 0);
    // Now. Assets only with allowDestroyingAssets. False when nothing was destroyed.
    bool DestroyImmediate(Object* object, bool allowDestroyingAssets = false);
    // Root GameObjects (or components on them) and their children survive scene loads.
    bool DontDestroyOnLoad(Object* target);

    // The scaled time Destroy delays count from (the player loop sets it each frame).
    void SetLifecycleTime(double scaledTime);
    double LifecycleTime();
    // Destroys every queued object whose time has come; returns how many roots were destroyed.
    u32 ProcessDestroyQueue();
    u32 PendingDestroyCount();
}
