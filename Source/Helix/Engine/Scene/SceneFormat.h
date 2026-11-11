#pragma once

#include <Helix/Engine/Object/Object.h>

// Scene file formats (extensible): a format recognizes its bytes, parses them (on any thread) and builds the objects
// through a SceneBuilder (on the main thread). The builder owns what every format shares: objects created by class ID
// through the class registry, references by fileID, the checks of GameObjects and components, the transform blocks
// built from the parents and children the Transforms read, the root order and the enabling. YAML and the binary
// serialized file are registered by the engine.
namespace helix::engine
{
    class Transform;

    // The roots in their order: the SceneRoots document of a scene file.
    struct SceneRoots
    {
        static constexpr StringView kTypeName = "SceneRoots";
        u32 m_ObjectHideFlags = 0;
        Array<PPtr<Transform>> m_Roots = Array<PPtr<Transform>>();

        template <class V>
        void Transfer(V& v)
        {
            v.Field("m_ObjectHideFlags", m_ObjectHideFlags);
            v.Field("m_Roots", m_Roots);
        }
    };

    class SceneBuilder
    {
    public:
        virtual ~SceneBuilder() = default;
        // A new object of the class for the file's fileID; null (warned or reported) for unknown or abstract classes
        // and repeated fileIDs. `line` is for messages (0 when the format has none).
        virtual Object* Create(i32 classId, i64 fileId, u32 line) = 0;
        // How many objects the file has (room for them up front).
        virtual void Reserve(u32 objects) = 0;
        // Maps the file's fileIDs to the created objects; pass it to the class's read function.
        virtual serialization::ReferenceTable& References() = 0;
        virtual void SetRoots(SceneRoots& roots) = 0;
        virtual void Error(StringView message) = 0;
        virtual void Warning(StringView message) = 0;
    };

    // What a format's Parse keeps for Build.
    class ParsedScene
    {
    public:
        virtual ~ParsedScene() = default;
    };

    class SceneFormat
    {
    public:
        virtual ~SceneFormat() = default;
        virtual StringView Name() const = 0;
        // Whether the bytes are in this format.
        virtual bool Probe(Span<const u8> bytes) const = 0;
        // Thread safe; the bytes outlive the result. Null with `error` when they do not parse.
        virtual ParsedScene* Parse(Span<const u8> bytes, String& error) const = 0;
        virtual void Build(ParsedScene& parsed, SceneBuilder& builder) const = 0;
    };

    // Formats are asked in the order they were registered, after the engine's own; the format stays owned by the caller.
    void RegisterSceneFormat(SceneFormat* format);
    // Null when no format takes the bytes.
    const SceneFormat* FindSceneFormat(Span<const u8> bytes);
}
