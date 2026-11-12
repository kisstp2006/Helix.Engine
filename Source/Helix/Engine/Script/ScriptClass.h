#pragma once

#include <Helix/Core/Containers/Array.h>
#include <Helix/Core/Strings/String.h>
#include <Helix/Scripting/Types/Type.h>

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
        // The engine class its objects are made as: GameBehaviour for C# scripts, the C++ class for C++ scripts.
        u32 engineClass = ~0u;
        u32 messages = 0;
        // DefaultExecutionOrder.
        i32 defaultOrder = 0;
        // C++ scripts: the message functions (the object as its root class's address).
        void (*call[u32(ScriptMessage::Count)])(void* instance) = {};
        void (*callBool[u32(ScriptMessage::Count)])(void* instance, bool value) = {};
        // Its index in the registry (what the C# side knows it by).
        u32 index = 0;
        // False once its module is unloaded (objects of it are gone).
        bool alive = true;

        bool Has(ScriptMessage m) const { return (messages >> u32(m)) & 1u; }
    };

    // Adds a script class (a copy); the index.
    u32 RegisterScriptClass(const ScriptClass& script);
    const ScriptClass* GetScriptClass(u32 index);
    const ScriptClass* FindScriptClass(scripting::TypeId type);
    const ScriptClass* FindScriptClass(StringView assembly, StringView nameSpace, StringView name);
    const ScriptClass* FindScriptClassOfEngineClass(u32 klass);
    u32 ScriptClassCount();
}
