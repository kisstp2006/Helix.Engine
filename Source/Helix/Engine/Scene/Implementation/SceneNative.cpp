#include <Helix/Engine/Object/Implementation/ScriptingTypes.h>
#include <Helix/Engine/Scene/SceneNative.h>

namespace helix::engine
{
    namespace
    {
        // The engine class of a type of the HelixEngine module (kNoClass for other types).
        u32 EngineClassOf(scripting::TypeId type)
        {
            const scripting::TypeInfo* t = scripting::GetType(type);
            return t && t->module == detail::EngineModule() && t->kind == scripting::TypeKind::Class ? u32(t->user) : kNoClass;
        }
    }

    u32 SceneNative::LoadAsync(StringView path, scripting::TypeId type) { return Resources::LoadAsync(path, EngineClassOf(type)).id; }

    void SceneNative::Watch(u32 operation)
    {
        AsyncOperation{operation}.AddCompleted([](void*, AsyncOperation op) { completed().Invoke(op.id); });
    }

    Event<u32>& SceneNative::completed()
    {
        static Event<u32> event;
        return event;
    }

    Object* Resources::LoadOfType(StringView path, scripting::TypeId systemTypeInstance)
    {
        u32 klass = EngineClassOf(systemTypeInstance);
        return klass != kNoClass ? Load(path, klass) : nullptr;
    }

    Array<Object*> Resources::LoadAllApi(StringView path)
    {
        Array<Object*> out;
        LoadAll(path, out);
        return out;
    }

    Array<Object*> Resources::LoadAllOfType(StringView path, scripting::TypeId systemTypeInstance)
    {
        Array<Object*> out;
        u32 klass = EngineClassOf(systemTypeInstance);
        if (klass != kNoClass)
            LoadAll(path, klass, out);
        return out;
    }

    Array<Object*> Resources::FindObjectsOfTypeAllApi(scripting::TypeId type)
    {
        Array<Object*> out;
        u32 klass = EngineClassOf(type);
        if (klass != kNoClass)
            FindObjectsOfTypeAll(klass, out);
        return out;
    }
}
