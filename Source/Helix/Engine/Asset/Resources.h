#pragma once

#include <Helix/Engine/Asset/Asset.h>
#include <Helix/Engine/Object/Object.h>

// Resources: the assets below folders named Resources, by their path there (case does not matter, no extension,
// forward slashes), through the asset runtime.
namespace helix::engine
{
    HX_CLASS(Namespace = "HelixEngine")
    class Resources
    {
    public:
        // The script API's members (LoadAsync and UnloadUnusedAssets are written in C# over SceneNative).
        HX_FUNCTION(Name = "Load") static Object* LoadApi(StringView path) { return Load(path); }
        HX_FUNCTION(Name = "Load", Generic = "T : Object") static Object* LoadOfType(StringView path, scripting::TypeId systemTypeInstance);
        HX_FUNCTION(Name = "LoadAll") static Array<Object*> LoadAllApi(StringView path);
        HX_FUNCTION(Name = "LoadAll", Generic = "T : Object") static Array<Object*> LoadAllOfType(StringView path, scripting::TypeId systemTypeInstance);
        HX_FUNCTION(Name = "UnloadAsset") static void UnloadAssetApi(Object* assetToUnload) { UnloadAsset(assetToUnload); }
        HX_FUNCTION(Name = "FindObjectsOfTypeAll", Generic = "T : Object") static Array<Object*> FindObjectsOfTypeAllApi(scripting::TypeId type);
        HX_FUNCTION(Name = "EntityIdToObject") static Object* EntityIdToObjectApi(EntityId entityId) { return EntityIdToObject(entityId); }

        // The asset's main object; null when there is no asset at the path.
        static Object* Load(StringView path);
        // The main object when it is of the class, else the asset's first object of it.
        static Object* Load(StringView path, u32 klass);
        template <class T>
        static T* Load(StringView path)
        {
            return static_cast<T*>(Load(path, ClassOf<T>()));
        }
        // Every object of the asset at the path, or of every asset in the folder at the path and below it.
        static void LoadAll(StringView path, Array<Object*>& out);
        static void LoadAll(StringView path, u32 klass, Array<Object*>& out);
        template <class T>
        static void LoadAll(StringView path, Array<T*>& out)
        {
            Array<Object*> all;
            LoadAll(path, ClassOf<T>(), all);
            out.Clear();
            for (Object* o : all)
                out.Push(static_cast<T*>(o));
        }
        // Loads in the background (UpdatePreloading); a request for a path with no asset is done with a null asset.
        static ResourceRequest LoadAsync(StringView path, u32 klass = kNoClass);
        template <class T>
        static ResourceRequest LoadAsync(StringView path)
        {
            return LoadAsync(path, ClassOf<T>());
        }
        // The asset's data goes; the object stays and its data loads again with the asset.
        static void UnloadAsset(Object* assetToUnload) { UnloadAssetData(assetToUnload); }
        static AsyncOperation UnloadUnusedAssets() { return engine::UnloadUnusedAssets(); }

        static void FindObjectsOfTypeAll(u32 klass, Array<Object*>& out) { engine::FindObjectsOfTypeAll(klass, out); }
        template <class T>
        static void FindObjectsOfTypeAll(Array<T*>& out)
        {
            Array<Object*> all;
            engine::FindObjectsOfTypeAll(ClassOf<T>(), all);
            out.Clear();
            for (Object* o : all)
                out.Push(static_cast<T*>(o));
        }
        static Object* EntityIdToObject(EntityId id) { return engine::EntityIdToObject(id); }
        static void EntityIdsToObjectList(Span<const EntityId> ids, Array<Object*>& out) { engine::EntityIdsToObjectList(ids, out); }
        static void EntityIdsToValidArray(Span<const EntityId> ids, Array<EntityId>& out) { engine::EntityIdsToValidArray(ids, out); }
    };
}
