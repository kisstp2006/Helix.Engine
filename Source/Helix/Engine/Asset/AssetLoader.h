#pragma once

#include <Helix/Engine/Asset/Asset.h>
#include <Helix/Engine/Object/EntityId.h>

// Asset loaders: registered by file extension, each in two halves. decode runs on a job (or the IO thread): bytes to
// data, no object model access. integrate runs on the main thread and makes the objects. The built in loaders
// (images through Helix.Image's decoders, models through Helix.Model, the engine's serialized files) register on first
// use; a loader with the same name replaces them.
namespace helix::engine
{
    struct AssetDecodeInput
    {
        Guid guid;
        StringView path;
        AssetSource* source = nullptr;
        Span<const u8> bytes;
        // The array of `bytes`: a loader whose result points into them moves them out to keep them.
        Array<u8>* owner = nullptr;
        // The .meta next to a source file, empty when there is none.
        Span<const u8> meta;
    };

    // What integrate makes: the asset's objects under their fileIDs.
    class AssetObjects
    {
    public:
        const Guid& guid() const { return guid_; }
        StringView path() const { return path_; }
        // The object (made in its class's pool, persistent); the first main one is what loads return.
        void Add(i64 fileID, Object* object, bool main = false);

        struct Entry
        {
            i64 fileID;
            EntityId id;
        };
        Array<Entry> objects = Array<Entry>();
        i64 mainFileID = 0;

    private:
        friend struct AssetRuntimeAccess;
        Guid guid_;
        StringView path_;
    };

    struct AssetLoader
    {
        StringView name;
        // Lower case, without the dot.
        Array<String> extensions = Array<String>();
        i32 referenceType = kAssetTypeImported;
        // Any thread; null with `error` when the file cannot load.
        void* (*decode)(const AssetDecodeInput& input, String& error) = nullptr;
        // Main thread; `errors` are logged with the asset's path.
        bool (*integrate)(void* decoded, AssetObjects& out, Array<String>& errors) = nullptr;
        void (*release)(void* decoded) = nullptr;
    };

    // Adds a loader, or replaces the one with the same name; the catalog picks loaders again.
    void RegisterAssetLoader(const AssetLoader& loader);
    // A copy of the loader of an extension (lower case, without the dot).
    bool FindAssetLoader(StringView extension, AssetLoader& out);

    // The built in loaders, registered by the runtime on first use.
    AssetLoader TextureAssetLoader();
    AssetLoader ModelAssetLoader();
    AssetLoader SerializedAssetLoader();
    AssetLoader ShaderAssetLoader();
}
