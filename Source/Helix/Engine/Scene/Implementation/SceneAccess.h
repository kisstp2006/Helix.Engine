#pragma once

#include <Helix/Engine/Scene/SceneFormat.h>
#include <Helix/Engine/Scene/SceneManager.h>

namespace helix::engine
{
    // The slot of the objects of prefab assets: in no scene.
    inline constexpr u32 kAssetSlot = 1023;

    struct SceneBuildOptions
    {
        // Assets: persistent objects, not enabled.
        bool persistent = false;
        bool enable = true;
    };

    // What the scene files reach inside the SceneManager.
    struct SceneAccess
    {
        // The fileIDs of the scene's objects as its file named them (kept for the next write).
        static HashMap<EntityId, i64>* FileIds(u32 slot);
        // A fileID the scene has not used: deterministic per scene, never 0 or the SceneRoots fileID.
        static i64 NewFileId(u32 slot, const HashMap<i64, EntityId>& used);

        // Creates, reads, checks, links and enables the objects of a parsed file in the scene; false when something in
        // it was wrong (`errors` say what).
        // `created` gets every object made with its fileID.
        static bool Build(const SceneFormat& format, ParsedScene& parsed, u32 slot, Array<String>& errors,
                          const SceneBuildOptions& options = {}, Array<EntityId>* roots = nullptr, Array<EntityId>* created = nullptr,
                          Array<i64>* createdFileIds = nullptr);
        // The objects as documents under the given fileIDs (an asset file): references among them by fileID, to objects
        // of assets by {fileID, guid, type}.
        static bool WriteObjects(Span<Object* const> objects, Span<const i64> fileIds, bool binary, Array<u8>& out, String& error);
    };
}
