#pragma once

#include <Helix/Engine/Asset/Asset.h>
#include <Helix/Engine/Object/Object.h>

// Resources: the assets below folders named Resources, by their path there (case does not matter, no extension,
// forward slashes), through the asset runtime.
namespace helix::engine
{
    class Resources
    {
    public:
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
