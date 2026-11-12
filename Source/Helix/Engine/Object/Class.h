#pragma once

#include <Helix/Core/Containers/Array.h>
#include <Helix/Core/Strings/String.h>
#include <Helix/Scripting/Types/Type.h>

#include <type_traits>

// The class registry: one entry per engine class with its class ID (the ID scene files use),
// its base, what the object model needs to create, clone, remap and destroy it, and the classes it requires.
// Classes are numbered depth first, so "derives from" is a range test on those numbers.
namespace helix::serialization
{
    struct AssetReference;
    class ReferenceTable;
    class SerializedFile;
    class SerializedFileWriter;
    namespace yaml
    {
        class Emitter;
        class Tree;
    }
}

namespace helix::engine
{
    class Object;
    class ReferenceRemap;

    // The class IDs of the YAML scene and asset files.
    namespace ClassId
    {
        inline constexpr i32 Object = 0, GameObject = 1, Component = 2, Transform = 4, Behaviour = 8, Camera = 20, Material = 21,
                             MeshRenderer = 23, Renderer = 25, Texture = 27, Texture2D = 28, MeshFilter = 33, Mesh = 43, Shader = 48, TextAsset = 49,
                             Light = 108, MonoBehaviour = 114, RectTransform = 224, PrefabInstance = 1001;
    }

    enum ClassFlags : u32
    {
        ClassNone = 0,
        // No objects of the class itself (Object, Component, Behaviour, Renderer).
        ClassAbstract = 1,
        // Lives on a GameObject.
        ClassComponent = 2,
        // At most one per GameObject.
        ClassDisallowMultiple = 4,
        // Has the enabled flag of Behaviour.
        ClassBehaviour = 8,
    };

    inline constexpr u32 kNoClass = ~0u;

    struct ClassInfo
    {
        i32 classId = 0;
        StringView name;
        u32 base = kNoClass;
        u32 flags = ClassNone;
        // Objects live in a pool per class (objects of one class next to each other): their size and alignment.
        u32 size = 0, alignment = 0;
        // Construct in `memory`; null for abstract classes.
        Object* (*create)(void* memory) = nullptr;
        // A copy of the object's fields (not its identity) in `memory`.
        Object* (*clone)(void* memory, const Object&) = nullptr;
        // Visits every object reference of the object.
        void (*remap)(Object&, ReferenceRemap&) = nullptr;
        // Native hooks, null when the class has none: just before the object goes, and when a component starts or stops
        // taking part (a Behaviour enabled on an active GameObject).
        void (*onDestroy)(Object&) = nullptr;
        void (*onEnable)(Object&) = nullptr;
        void (*onDisable)(Object&) = nullptr;
        // Frees the object's data and keeps the object (Resources.UnloadAsset); null for classes that cannot.
        void (*unloadData)(Object&) = nullptr;
        // A component's GameObject moved to another layer.
        void (*onLayerChanged)(Object&) = nullptr;
        // Classes AddComponent adds first (RequireComponent).
        Array<u32> required = Array<u32>();
        // Scene files through the class's Transfer; null for abstract classes.
        void (*writeYaml)(Object&, serialization::yaml::Emitter&, serialization::ReferenceTable*) = nullptr;
        bool (*readYaml)(Object&, const serialization::yaml::Tree&, u32 root, Array<String>& errors, serialization::ReferenceTable*) = nullptr;
        void (*writeBinary)(Object&, serialization::SerializedFileWriter&, i64 fileID, serialization::ReferenceTable*) = nullptr;
        bool (*readBinary)(Object&, const serialization::SerializedFile&, u32 index, String& error, serialization::ReferenceTable*) = nullptr;
        // One field by its property path (prefab modifications), and read as text.
        bool (*applyProperty)(Object&, StringView path, StringView value, const serialization::AssetReference& reference,
                              serialization::ReferenceTable*, String& error) = nullptr;
        bool (*readProperty)(Object&, StringView path, String& text, String& error) = nullptr;

        // The class's type in the scripting core's registry (module HelixEngine), set by RegisterClass.
        scripting::TypeId type;

        // Filled by the registry: the depth first number and the end of the subtree.
        u32 order = 0, orderEnd = 0;
    };

    namespace detail
    {
        // The registry's arrays, for the inline lookups below (refreshed by every RegisterClass).
        struct ClassTables
        {
            const ClassInfo* classes = nullptr;
            // Per class: its depth first number and the size of its subtree.
            const u32* orders = nullptr;
            const u32* sizes = nullptr;
            // The class holding each depth first number.
            const u32* byOrder = nullptr;
            u32 count = 0;
        };
        extern ClassTables gClassTables;
    }

    // Adds a class; its base must be registered. Returns its index.
    u32 RegisterClass(ClassInfo info);
    u32 FindClassById(i32 classId);
    u32 FindClassByName(StringView name);

    inline u32 ClassCount() { return detail::gClassTables.count; }
    inline const ClassInfo& GetClass(u32 index) { return detail::gClassTables.classes[index]; }
    // One subtraction on the depth first numbers.
    inline bool IsDerivedFrom(u32 klass, u32 base)
    {
        const detail::ClassTables& t = detail::gClassTables;
        return klass < t.count && base < t.count && t.orders[klass] - t.orders[base] < t.sizes[base];
    }

    // The index of an engine class (registering it and its bases on first use).
    template <class T>
    u32 ClassOf()
    {
        return T::StaticClass();
    }
}
