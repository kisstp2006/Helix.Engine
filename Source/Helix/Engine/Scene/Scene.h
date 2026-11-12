#pragma once

#include <Helix/Core/Containers/Array.h>
#include <Helix/Core/Strings/StringView.h>
#include <Helix/Engine/Scene/LoadableSceneId.h>

// Scene: a value naming one scene of the SceneManager. A scene that was unloaded (or never existed) is not valid;
// its getters then return empty values.
namespace helix::engine
{
    class GameObject;

    // A scene slot and its generation: a handle of an unloaded scene never names a new one.
    struct SceneHandle
    {
        u32 value = 0;
        bool operator==(const SceneHandle&) const = default;
    };

    enum class LoadSceneMode : u8
    {
        // Every loaded scene but the DontDestroyOnLoad scene is unloaded first.
        Single = 0,
        Additive = 1,
    };

    enum class LocalPhysicsMode : u8
    {
        None = 0,
        Physics2D = 1,
        Physics3D = 2,
    };

    enum class UnloadSceneOptions : u8
    {
        None = 0,
        UnloadAllEmbeddedSceneObjects = 1,
    };

    struct LoadSceneParameters
    {
        LoadSceneMode loadSceneMode = LoadSceneMode::Single;
        LocalPhysicsMode localPhysicsMode = LocalPhysicsMode::None;
    };

    struct CreateSceneParameters
    {
        LocalPhysicsMode localPhysicsMode = LocalPhysicsMode::None;
    };

    struct Scene
    {
        SceneHandle handle;

        bool IsValid() const;
        StringView name() const;
        // The path it was loaded from ("" for created scenes).
        StringView path() const;
        // Its place in the build scene list, -1 outside it.
        i32 buildIndex() const;
        // True once loading finished and its objects were enabled.
        bool isLoaded() const;
        bool isDirty() const;
        bool isSubScene() const;
        // The id of the scene asset it was loaded from by LoadableSceneId; invalid for scenes loaded otherwise.
        LoadableSceneId loadableSceneId() const;
        void SetIsSubScene(bool value) const;
        u32 rootCount() const;
        // The root GameObjects in their order.
        void GetRootGameObjects(Array<GameObject*>& out) const;
        Array<GameObject*> GetRootGameObjects() const
        {
            Array<GameObject*> out;
            GetRootGameObjects(out);
            return out;
        }

        bool operator==(const Scene&) const = default;

        // The SceneManager's slot (for the engine's own code).
        u32 Slot() const { return handle.value & 0x3ff; }
    };
}
