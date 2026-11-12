#pragma once

#include <Helix/Engine/Asset/Resources.h>
#include <Helix/Engine/Async/AsyncOperation.h>
#include <Helix/Engine/Event/Event.h>
#include <Helix/Engine/GameObject/GameObject.h>
#include <Helix/Engine/Scene/SceneManager.h>

// The members of the C# value types and classes over handles that are not engine objects (Scene, AsyncOperation and
// ResourceRequest, the operations of SceneManager and Resources), internal in C#; C++ scripts use the engine's value
// types (Scene, AsyncOperation) themselves.
namespace helix::engine
{
    HX_CLASS(Namespace = "HelixEngine.Internal", Static, Internal)
    class SceneNative
    {
    public:
        // Scene
        HX_FUNCTION(Leaf) static bool SceneIsValid(Scene scene) { return scene.IsValid(); }
        HX_FUNCTION() static StringView SceneName(Scene scene) { return scene.name(); }
        HX_FUNCTION() static StringView ScenePath(Scene scene) { return scene.path(); }
        HX_FUNCTION(Leaf) static i32 SceneBuildIndex(Scene scene) { return scene.buildIndex(); }
        HX_FUNCTION(Leaf) static bool SceneIsLoaded(Scene scene) { return scene.isLoaded(); }
        HX_FUNCTION(Leaf) static bool SceneIsDirty(Scene scene) { return scene.isDirty(); }
        HX_FUNCTION(Leaf) static bool SceneIsSubScene(Scene scene) { return scene.isSubScene(); }
        HX_FUNCTION(Leaf) static void SceneSetIsSubScene(Scene scene, bool value) { scene.SetIsSubScene(value); }
        HX_FUNCTION(Leaf) static i32 SceneRootCount(Scene scene) { return i32(scene.rootCount()); }
        HX_FUNCTION() static Array<GameObject*> SceneRoots(Scene scene) { return scene.GetRootGameObjects(); }

        // The operations of SceneManager and Resources, as the operations' ids.
        HX_FUNCTION() static u32 LoadSceneAsync(StringView sceneName, LoadSceneParameters parameters)
        {
            return SceneManager::LoadSceneAsync(sceneName, parameters).id;
        }
        HX_FUNCTION() static u32 LoadSceneAsync(i32 sceneBuildIndex, LoadSceneParameters parameters)
        {
            return SceneManager::LoadSceneAsync(sceneBuildIndex, parameters).id;
        }
        HX_FUNCTION() static u32 UnloadSceneAsync(Scene scene, UnloadSceneOptions options) { return SceneManager::UnloadSceneAsync(scene, options).id; }
        HX_FUNCTION() static u32 UnloadSceneAsync(StringView sceneName, UnloadSceneOptions options)
        {
            return SceneManager::UnloadSceneAsync(sceneName, options).id;
        }
        HX_FUNCTION() static u32 UnloadSceneAsync(i32 sceneBuildIndex, UnloadSceneOptions options)
        {
            return SceneManager::UnloadSceneAsync(sceneBuildIndex, options).id;
        }
        HX_FUNCTION() static u32 LoadAsync(StringView path, scripting::TypeId type);
        HX_FUNCTION() static u32 UnloadUnusedAssets() { return Resources::UnloadUnusedAssets().id; }

        // AsyncOperation and ResourceRequest by id.
        HX_FUNCTION(Leaf) static bool IsDone(u32 operation) { return AsyncOperation{operation}.isDone(); }
        HX_FUNCTION(Leaf) static f32 Progress(u32 operation) { return AsyncOperation{operation}.progress(); }
        HX_FUNCTION(Leaf) static i32 Priority(u32 operation) { return AsyncOperation{operation}.priority(); }
        HX_FUNCTION(Leaf) static void SetPriority(u32 operation, i32 value) { AsyncOperation{operation}.SetPriority(value); }
        HX_FUNCTION(Leaf) static bool AllowSceneActivation(u32 operation) { return AsyncOperation{operation}.allowSceneActivation(); }
        HX_FUNCTION(Leaf) static void SetAllowSceneActivation(u32 operation, bool value) { AsyncOperation{operation}.SetAllowSceneActivation(value); }
        HX_FUNCTION() static Object* Asset(u32 operation)
        {
            ResourceRequest r;
            r.id = operation;
            return r.asset();
        }
        // Completed fires the event below with the operation's id (at once when it is done already).
        HX_FUNCTION() static void Watch(u32 operation);
        HX_EVENT() static Event<u32>& completed();
    };
}
