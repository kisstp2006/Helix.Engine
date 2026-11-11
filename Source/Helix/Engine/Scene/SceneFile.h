#pragma once

#include <Helix/Core/Strings/String.h>
#include <Helix/Engine/Scene/Scene.h>

// Scene files: one document per object (`--- !h!<classID> &<fileID>`, the class's fields below its name), references
// as {fileID: N}, the roots in order in the last document (SceneRoots). The binary form is a serialized file of the
// same objects. Objects read from a file keep their fileIDs when the scene is written again; objects with
// DontSaveInEditor (and their children) are left out; references to objects outside the scene are written as 0.
namespace helix::engine
{
    inline constexpr i32 kSceneRootsClassId = 1660057539;
    inline constexpr i64 kSceneRootsFileId = 9223372036854775807;

    bool WriteSceneYaml(Scene scene, String& out, String& error);
    // Player files leave the type trees out.
    bool WriteSceneBinary(Scene scene, Array<u8>& out, String& error, bool typeTrees = false);

    // Adds the objects of a scene file (YAML or binary, told apart by the first bytes) to a loaded scene and enables
    // them parents first. Problems go to `errors` with their line (YAML); what could be read stays. False when the
    // file could not be read at all or something in it was wrong.
    bool ReadSceneFile(Span<const u8> bytes, Scene scene, Array<String>& errors);
    inline bool ReadSceneFile(StringView text, Scene scene, Array<String>& errors)
    {
        return ReadSceneFile(Span<const u8>(reinterpret_cast<const u8*>(text.Data()), text.Size()), scene, errors);
    }
}
