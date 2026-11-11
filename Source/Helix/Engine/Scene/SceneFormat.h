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

    // A prefab instance as a file keeps it: the source prefab and what is changed in this copy of it. References point
    // at objects of the source by their fileIDs there (target, removed objects) or at objects of this file.
    struct PropertyModification
    {
        static constexpr StringView kTypeName = "PropertyModification";
        serialization::AssetReference target;
        String propertyPath;
        String value;
        serialization::AssetReference objectReference;

        template <class V>
        void Transfer(V& v)
        {
            v.Field("target", target);
            v.Field("propertyPath", propertyPath);
            v.Field("value", value);
            v.Field("objectReference", objectReference);
        }
    };

    // An object of this file added to an object of the instance (a child GameObject or a component).
    struct AddedObject
    {
        static constexpr StringView kTypeName = "AddedObject";
        serialization::AssetReference targetCorrespondingSourceObject;
        i32 insertIndex = -1;
        serialization::AssetReference addedObject;

        template <class V>
        void Transfer(V& v)
        {
            v.Field("targetCorrespondingSourceObject", targetCorrespondingSourceObject);
            v.Field("insertIndex", insertIndex);
            v.Field("addedObject", addedObject);
        }
    };

    struct PrefabModification
    {
        static constexpr StringView kTypeName = "PrefabModification";
        static constexpr i32 kVersion = 3;
        serialization::AssetReference m_TransformParent;
        Array<PropertyModification> m_Modifications = Array<PropertyModification>();
        Array<serialization::AssetReference> m_RemovedComponents = Array<serialization::AssetReference>();
        Array<serialization::AssetReference> m_RemovedGameObjects = Array<serialization::AssetReference>();
        Array<AddedObject> m_AddedGameObjects = Array<AddedObject>();
        Array<AddedObject> m_AddedComponents = Array<AddedObject>();

        template <class V>
        void Transfer(V& v)
        {
            v.Field("m_TransformParent", m_TransformParent);
            v.Field("m_Modifications", m_Modifications);
            v.Field("m_RemovedComponents", m_RemovedComponents);
            v.Field("m_RemovedGameObjects", m_RemovedGameObjects);
            v.Field("m_AddedGameObjects", m_AddedGameObjects);
            v.Field("m_AddedComponents", m_AddedComponents);
        }
    };

    struct PrefabInstanceData
    {
        static constexpr StringView kTypeName = "PrefabInstance";
        static constexpr i32 kVersion = 2;
        u32 m_ObjectHideFlags = 0;
        PrefabModification m_Modification;
        serialization::AssetReference m_SourcePrefab;

        template <class V>
        void Transfer(V& v)
        {
            v.Field("m_ObjectHideFlags", m_ObjectHideFlags);
            v.Field("m_Modification", m_Modification);
            v.Field("m_SourcePrefab", m_SourcePrefab);
        }
    };

    inline constexpr i32 kPrefabInstanceClassId = 1001;

    // The fileID of an object of a prefab instance: (instance XOR source object) without the sign bit.
    constexpr i64 InstanceObjectFileId(i64 instance, i64 source) { return (instance ^ source) & 0x7FFFFFFFFFFFFFFF; }

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
        // A placeholder (`stripped`) for an object of a prefab instance that other objects of the file refer to.
        virtual void AddStripped(i64 fileId, const serialization::AssetReference& source, i64 instanceFileId) = 0;
        virtual void AddPrefabInstance(i64 fileId, PrefabInstanceData& instance, u32 line) = 0;
        // Makes the copies of the prefab instances (after every object was created, before their fields are read).
        virtual void ResolvePrefabInstances() = 0;
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
