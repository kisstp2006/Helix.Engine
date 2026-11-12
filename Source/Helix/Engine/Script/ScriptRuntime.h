#pragma once

#include <Helix/Engine/Script/GameBehaviour.h>
#include <Helix/Scripting/Types/Variant.h>

// The scripts of both languages at run time: one scheduler calls their messages in the reference's order. C++ scripts
// are called through the function pointers of their class; C# scripts in one call into C# per phase and execution
// order, which calls each script in its own try. Within one execution order the C++ scripts of a phase run first,
// then the C# ones, each in their order of creation.
namespace helix::engine
{
    // A script component of either language on a GameObject (AddComponent of a script type).
    GameBehaviour* AddScript(GameObject* gameObject, const ScriptClass& script);

    // The phases the player loop runs (ScriptRunBehaviourFixedUpdate / Update / LateUpdate): the pending Starts
    // first (before the first FixedUpdate or Update), then the message to every started, active and enabled script.
    void RunScriptPhase(ScriptMessage phase);
    // A message to one script (Awake, OnEnable, ... by the scheduler; SendMessage and tools).
    void CallScriptMessage(GameBehaviour* behaviour, ScriptMessage message, bool argument = false);
    // A message to every script that is active and enabled (OnApplicationFocus, Pause, Quit).
    void BroadcastScriptMessage(ScriptMessage message, bool argument = false);
    // How many scripts the phase calls now (enabled on active GameObjects, with the message).
    u32 ScriptPhaseCount(ScriptMessage phase);

    // SendMessage and its kinds (the reference's values).
    enum class SendMessageOptions : u8
    {
        RequireReceiver = 0,
        DontRequireReceiver = 1,
    };

    enum class MessageReach : u8
    {
        // The scripts of the GameObject (SendMessage).
        Self,
        // And of its children, depth first (BroadcastMessage).
        Children,
        // And of its parents (SendMessageUpwards).
        Parents,
    };

    // Calls the method of that name of every script that has one, in either language: one that takes the value (when
    // there is one), else one without arguments. Inactive GameObjects are left out. RequireReceiver logs an error when
    // no script had it. True when one was called.
    bool SendScriptMessage(GameObject* gameObject, StringView methodName, const scripting::Variant& value = scripting::Variant(),
                           SendMessageOptions options = SendMessageOptions::RequireReceiver, MessageReach reach = MessageReach::Self);

    // The C# side of the engine: whether it handed its functions over (C# scripts need it).
    bool ManagedScriptsReady();

    // C++ game modules: a shared library whose generated registration adds its script classes (Windows, Linux,
    // Android; on the Web modules are linked in and register through their exports by name). Unloading destroys the
    // objects of its scripts.
    bool LoadGameModule(StringView path, String& error);
    // A module linked into the player: its registration by name (HxBindings_<Module>_Register).
    bool RegisterLinkedGameModule(StringView module, String& error);
    void UnloadGameModule(StringView module);
}
