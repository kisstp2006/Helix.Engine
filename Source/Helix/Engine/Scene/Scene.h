#pragma once

#include <Helix/Core/Basics/Tags.h>
#include <Helix/Core/Containers/Array.h>
#include <Helix/Core/Strings/StringView.h>
#include <Helix/Engine/Scene/LoadableSceneId.h>

// Scene: a value naming one scene of the SceneManager. A scene that was unloaded (or never existed) is not valid;
// its getters then return empty values.
namespace helix::engine
{
    class GameObject;

    // A scene slot and its generation: a handle of an unloaded scene never names a new one.
    HX_STRUCT(Namespace = "HelixEngine.SceneManagement", Internal)
    struct SceneHandle
    {
        HX_FIELD() u32 value = 0;
        bool operator==(const SceneHandle&) const = default;
    };

    HX_ENUM(Namespace = "HelixEngine.SceneManagement")
    enum class LoadSceneMode : i32
    {
        // Every loaded scene but the DontDestroyOnLoad scene is unloaded first.
        Single = 0,
        Additive = 1,
    };

    HX_ENUM(Namespace = "HelixEngine.SceneManagement", Attributes = "[System.Flags]")
    enum class LocalPhysicsMode : i32
    {
        None = 0,
        Physics2D = 1,
        Physics3D = 2,
    };

    HX_ENUM(Namespace = "HelixEngine.SceneManagement", Attributes = "[System.Flags]")
    enum class UnloadSceneOptions : i32
    {
        None = 0,
        UnloadAllEmbeddedSceneObjects = 1,
    };

    HX_STRUCT(Namespace = "HelixEngine.SceneManagement")
    struct LoadSceneParameters
    {
        HX_FIELD() LoadSceneMode loadSceneMode = LoadSceneMode::Single;
        HX_FIELD() LocalPhysicsMode localPhysicsMode = LocalPhysicsMode::None;
    };

    HX_STRUCT(Namespace = "HelixEngine.SceneManagement")
    struct CreateSceneParameters
    {
        HX_FIELD() LocalPhysicsMode localPhysicsMode = LocalPhysicsMode::None;
    };

    // A loaded scene by its handle (C#: the private m_Handle; its members are written in C# over SceneNative).
    HX_STRUCT(Namespace = "HelixEngine.SceneManagement")
    struct Scene
    {
        HX_FIELD(Name = "m_Handle", Private) SceneHandle handle;

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
