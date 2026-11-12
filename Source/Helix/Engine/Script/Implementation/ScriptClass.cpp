#include <Helix/Engine/Script/ScriptClass.h>

namespace helix::engine
{
    namespace
    {
        constexpr StringView kNames[] = {
            "Awake", "OnEnable", "Start", "FixedUpdate", "Update", "LateUpdate", "OnDisable", "OnDestroy", "OnApplicationFocus", "OnApplicationPause",
            "OnApplicationQuit", "OnTransformParentChanged", "OnTransformChildrenChanged", "OnBecameVisible", "OnBecameInvisible", "OnValidate", "Reset",
        };
        static_assert(sizeof(kNames) / sizeof(kNames[0]) == usize(ScriptMessage::Count));

        Array<ScriptClass*>& Scripts()
        {
            static struct Holder
            {
                Array<ScriptClass*> all;
                ~Holder()
                {
                    for (ScriptClass* s : all)
                        delete s;
                }
            } h;
            return h.all;
        }
    }

    StringView ScriptMessageName(ScriptMessage message) { return message < ScriptMessage::Count ? kNames[u32(message)] : StringView(); }

    ScriptMessage ScriptMessageOf(StringView name)
    {
        for (u32 i = 0; i < u32(ScriptMessage::Count); i++)
            if (kNames[i] == name)
                return ScriptMessage(i);
        return ScriptMessage::Count;
    }

    u32 RegisterScriptClass(const ScriptClass& script)
    {
        auto* s = new ScriptClass(script);
        s->index = u32(Scripts().Size());
        Scripts().Push(s);
        return s->index;
    }

    const ScriptClass* GetScriptClass(u32 index) { return index < Scripts().Size() ? Scripts()[index] : nullptr; }

    const ScriptClass* FindScriptClass(scripting::TypeId type)
    {
        for (const ScriptClass* s : Scripts())
            if (s->alive && s->type == type)
                return s;
        return nullptr;
    }

    const ScriptClass* FindScriptClass(StringView assembly, StringView nameSpace, StringView name)
    {
        for (const ScriptClass* s : Scripts())
            if (s->alive && s->name == name && s->nameSpace == nameSpace && (assembly.Empty() || s->assembly == assembly))
                return s;
        return nullptr;
    }

    const ScriptClass* FindScriptClassOfEngineClass(u32 klass)
    {
        for (const ScriptClass* s : Scripts())
            if (s->alive && s->language == ScriptLanguage::Cpp && s->engineClass == klass)
                return s;
        return nullptr;
    }

    u32 ScriptClassCount() { return u32(Scripts().Size()); }

    // Internal: the registry's entries (to retire the scripts of an unloaded module).
    ScriptClass* MutableScriptClass(u32 index) { return index < Scripts().Size() ? Scripts()[index] : nullptr; }
}
