#pragma once

#include <Helix/Core/Basics/Span.h>
#include <Helix/Core/Strings/String.h>
#include <Helix/Core/Types/Guid.h>
#include <Helix/Engine/Async/AsyncOperation.h>
#include <Helix/Engine/Object/Class.h>

// The asset runtime: a catalog of assets (guid, path, the loader of their file) listed by registered sources, and
// the loaded objects of each asset by (guid, fileID). Loads read on an IO thread, decode on Helix.Jobs and make their
// objects on the main thread (UpdatePreloading, within Application::backgroundLoadingPriority's time); synchronous
// loads run the same steps at once. Scene and prefab files refer to assets as {fileID, guid, type}.
namespace helix::engine
{
    class AssetSource;

    // The type of references: assets in the engine's serialized files, and assets imported from source files.
    inline constexpr i32 kAssetTypeSerialized = 2;
    inline constexpr i32 kAssetTypeImported = 3;
    inline constexpr u32 kNoAssetLoader = ~0u;

    // The fileID of the main object of a single object asset (Texture2D 2800000, Mesh 4300000).
    constexpr i64 MainAssetFileId(i32 classId) { return i64(classId) * 100000; }
    // The fileID of an object made by an importer, from its class and its name (a path for nodes of a model).
    i64 AssetObjectFileId(i32 classId, StringView name);
    // The guid of a source file without a .meta: from its path (case and slashes do not matter).
    Guid PathGuid(StringView path);

    struct AssetEntry
    {
        Guid guid;
        // Relative to the source, forward slashes.
        String path;
        // Lower case, without the extension, below the last folder named Resources; empty outside them.
        String resourcePath;
        AssetSource* source = nullptr;
        u32 loader = kNoAssetLoader;
    };

    // Where assets come from (extensible: packed player data, the editor's asset database).
    class AssetSource
    {
    public:
        virtual ~AssetSource() = default;
        // Every asset of the source (guid and path).
        virtual void List(Array<AssetEntry>& out) = 0;
        // A file of the source by its path; called from the IO thread and from decode jobs.
        virtual bool Read(StringView path, Array<u8>& out, String& error) = 0;
    };

    // Source files in a folder (development): the guid of a file's .meta, or one from its path when it has none.
    class FolderAssetSource final : public AssetSource
    {
    public:
        explicit FolderAssetSource(StringView root) : root_(root) {}
        void List(Array<AssetEntry>& out) override;
        bool Read(StringView path, Array<u8>& out, String& error) override;
        StringView Root() const { return root_; }

    private:
        String root_;
    };

    // A player data folder: the engine's serialized files and the index that lists them (kAssetIndexFile).
    class PlayerDataSource final : public AssetSource
    {
    public:
        explicit PlayerDataSource(StringView root) : root_(root) {}
        void List(Array<AssetEntry>& out) override;
        bool Read(StringView path, Array<u8>& out, String& error) override;

    private:
        String root_;
    };
    inline constexpr StringView kAssetIndexFile = "assets.index";
    // Writes the index of a player data folder (builds, tools, tests).
    bool WriteAssetIndex(StringView folder, Span<const AssetEntry> entries, String& error);

    // ------------------------------------------------------------------------------------------------- catalog
    // Lists the source's assets; a guid listed by an earlier source stays with it.
    void AddAssetSource(AssetSource* source);
    // Forgets the source's assets (objects already loaded stay).
    void RemoveAssetSource(AssetSource* source);
    // Lists every source again (files added or removed).
    void RefreshAssets();
    const AssetEntry* FindAsset(const Guid& guid);
    // By the path as its source lists it (case does not matter).
    const AssetEntry* FindAssetAtPath(StringView path);
    // By the Resources path: lower case, no extension.
    const AssetEntry* FindResource(StringView resourcePath);
    // Every resource in a Resources folder path and below it.
    void FindResources(StringView folder, Array<const AssetEntry*>& out);

    // ------------------------------------------------------------------------------------------------- loading
    // Loads every object of the asset (now, finishing a load in flight); the main object, null when it cannot load.
    Object* LoadAsset(const Guid& guid);
    // One object of the asset; fileID 0 is the main object.
    Object* LoadAsset(const Guid& guid, i64 fileID);
    // The main object when it is of the class (or a subclass), else the asset's first object of it.
    Object* LoadAssetOfClass(const Guid& guid, u32 klass);
    // The asset's objects, the main one first.
    void LoadAllAssets(const Guid& guid, Array<Object*>& out);
    // Loads in the background; the request's asset is the main object, or with a class the first object of it.
    ResourceRequest LoadAssetAsync(const Guid& guid, u32 klass = kNoClass, i32 priority = 0);
    Object* FindLoadedAsset(const Guid& guid, i64 fileID = 0);
    bool IsAssetLoaded(const Guid& guid);
    // The reference of an object of an asset; false for objects of no asset.
    bool GetAssetReference(const Object* object, Guid& guid, i64& fileID, i32& type);
    // Makes an object made by code an object of an asset (tools, SaveAsset).
    void RegisterAssetObject(const Guid& guid, i64 fileID, Object* object, bool main = false, i32 type = kAssetTypeSerialized);

    // Resources.UnloadAsset: the object's data goes (for classes that can drop it); the object stays and loads again
    // when its asset is loaded. False for objects that cannot be unloaded (GameObjects, components, objects of no asset).
    bool UnloadAssetData(Object* object);
    // Resources.UnloadUnusedAssets: destroys every asset object (and every object made by code outside the scenes)
    // that no scene object, DontDestroyOnLoad object or DontUnloadUnusedAsset object reaches.
    AsyncOperation UnloadUnusedAssets();

    // Writes objects as an asset file (YAML or binary): the first one is the main object. References to objects of
    // other assets are written as {fileID, guid, type}.
    bool WriteAssetFile(Span<Object* const> objects, Array<u8>& out, String& error, bool binary = false);

    // The player loop's UpdatePreloading: makes the objects of finished reads and decodes.
    void ProcessAssetLoads();
    // Loads queued or running.
    u32 PendingAssetLoads();
    // Back to the state at startup (tests): loads finished, sources removed, loaded assets forgotten (not destroyed).
    void ResetAssets();
}
