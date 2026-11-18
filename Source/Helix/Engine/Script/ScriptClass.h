#pragma once

#include <Helix/Core/Containers/Array.h>
#include <Helix/Core/Strings/String.h>
#include <Helix/Scripting/Scripts/NativeScript.h>
#include <Helix/Scripting/Types/Type.h>
#include <Helix/Serialization/Transfer/TypeTree.h>

// Script classes of both languages: what the engine knows of a script to make it, find its messages and order it.
// C++ script classes come from the generated registration of game modules (scripting::NativeScriptClass), C# script
// classes from the C# side of the engine when it loads an assembly.
namespace helix::engine
{
    // The messages scripts receive (the names are the reference's); bit i of ScriptClass::messages is message i.
    enum class ScriptMessage : u8
    {
        Awake,
        OnEnable,
        Start,
        FixedUpdate,
        Update,
        LateUpdate,
        OnDisable,
        OnDestroy,
        OnApplicationFocus,
        OnApplicationPause,
        OnApplicationQuit,
        OnTransformParentChanged,
        OnTransformChildrenChanged,
        OnBecameVisible,
        OnBecameInvisible,
        OnValidate,
        Reset,
        // The camera's scripts around its rendering; the scripts of a renderer a camera draws; every script after.
        OnPreCull,
        OnPreRender,
        OnPostRender,
        OnWillRenderObject,
        OnRenderObject,
        // The immediate GUI: once a pass (Layout, each input event, Repaint) with the GUI's current event set.
        OnGui,
        // The canvas UI's: a RectTransform's rectangle changed, before a parent change, a CanvasGroup above changed,
        // the canvas above changed, an animation set the component's properties.
        OnRectTransformDimensionsChange,
        OnBeforeTransformParentChanged,
        OnCanvasGroupChanged,
        OnCanvasHierarchyChanged,
        OnDidApplyAnimationProperties,
        Count,
    };

    StringView ScriptMessageName(ScriptMessage message);
    // Count for names that are no message.
    ScriptMessage ScriptMessageOf(StringView name);
    // The messages with one bool argument.
    inline bool ScriptMessageTakesBool(ScriptMessage m) { return m == ScriptMessage::OnApplicationFocus || m == ScriptMessage::OnApplicationPause; }

    enum class ScriptLanguage : u8
    {
        Cpp,
        CSharp,
    };

    struct ScriptClass
    {
        ScriptLanguage language = ScriptLanguage::Cpp;
        // The assembly (C#) or game module (C++), the namespace and the class: what a MonoScript names.
        String assembly;
        String nameSpace;
        String name;
        scripting::TypeId type;
        // The engine class its objects are made as: GameBehaviour (or ScriptableObject) for C# scripts, the C++ class
        // for C++ scripts, a class of its own below the C++ class for C# scripts deriving from a C++ script class.
        u32 engineClass = ~0u;
        // A script deriving from a C++ script class of another binary (a C# script, or a C++ script of another game
        // module): that class's index (~0u: none) and the messages its functions answer (those the class has none of).
        u32 nativeBase = ~0u;
        u32 nativeMessages = 0;
        // With a base: the class's own fields (a C++ class's transfer, a C# class's serialized fields); transfer is the
        // base's fields, then these.
        void (*ownTransfer)(void* instance, void* visitor) = nullptr;
        bool managedFields = false;
        u32 messages = 0;
        // DefaultExecutionOrder.
        i32 defaultOrder = 0;
        // RequireComponent: the component types AddComponent adds first when the GameObject has none of them.
        Array<scripting::TypeId> required = Array<scripting::TypeId>();
        // C++ scripts: the message functions (the object as its root class's address).
        void (*call[u32(ScriptMessage::Count)])(void* instance) = {};
        void (*callBool[u32(ScriptMessage::Count)])(void* instance, bool value) = {};
        // C++ scripts: every method without arguments or with one bool, by name (SendMessage).
        Array<scripting::NativeScriptMethod> methods = Array<scripting::NativeScriptMethod>();
        // The serialized fields through a serialization::DynamicVisitor (the object as its root class's address); null
        // without fields.
        void (*transfer)(void* instance, void* visitor) = nullptr;
        // C++ scripts: OnBeforeSerialize and OnAfterDeserialize, found by name as messages are (C# scripts take
        // ISerializationCallbackReceiver on their side).
        void (*beforeSerialize)(void* instance) = nullptr;
        void (*afterDeserialize)(void* instance) = nullptr;
        // The type tree and hash of its objects in binary files, built from the first one written or read.
        serialization::TypeTree layout = serialization::TypeTree();
        Md5Digest layoutHash = Md5Digest();
        bool hasLayout = false;
        // Its index in the registry (what the C# side knows it by).
        u32 index = 0;
        // False once its module is unloaded (objects of it are gone).
        bool alive = true;

        bool Has(ScriptMessage m) const { return (messages >> u32(m)) & 1u; }
        // The message runs a C++ function (call, callBool), not the C# object's method.
        bool NativeCall(ScriptMessage m) const { return language == ScriptLanguage::Cpp || ((nativeMessages >> u32(m)) & 1u); }
        // Its objects are of its own engine class (C++ scripts, C# scripts deriving from one).
        bool OwnClass() const { return language == ScriptLanguage::Cpp || nativeBase != ~0u; }
    };

    // Adds a script class (a copy); the index.
    u32 RegisterScriptClass(const ScriptClass& script);
    const ScriptClass* GetScriptClass(u32 index);
    const ScriptClass* FindScriptClass(scripting::TypeId type);
    const ScriptClass* FindScriptClass(StringView assembly, StringView nameSpace, StringView name);
    const ScriptClass* FindScriptClassOfEngineClass(u32 klass);
    u32 ScriptClassCount();
}
