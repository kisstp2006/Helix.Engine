#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Engine/Object/Implementation/ScriptingTypes.h>
#include <Helix/Engine/Object/Object.h>
#include <Helix/Engine/Script/ScriptClass.h>
#include <Helix/Engine/Script/GameBehaviour.h>
#include <Helix/Engine/Script/ScriptRuntime.h>
#include <Helix/Engine/Transform/Transform.h>

// The statics of Object the script API has, over the engine's lifecycle and find functions.
namespace helix::engine
{
    Object* Object::Instantiate(Object* original) { return engine::Instantiate(original); }
    Object* Object::Instantiate(Object* original, Transform* parent) { return engine::Instantiate(original, parent, false); }
    Object* Object::Instantiate(Object* original, Transform* parent, bool instantiateInWorldSpace)
    {
        return engine::Instantiate(original, parent, instantiateInWorldSpace);
    }
    Object* Object::Instantiate(Object* original, Vector3 position, Quaternion rotation) { return engine::Instantiate(original, position, rotation); }
    Object* Object::Instantiate(Object* original, Vector3 position, Quaternion rotation, Transform* parent)
    {
        return engine::Instantiate(original, position, rotation, parent);
    }
    void Object::Destroy(Object* obj) { engine::Destroy(obj); }
    void Object::Destroy(Object* obj, f32 t) { engine::Destroy(obj, t); }
    void Object::DestroyImmediate(Object* obj) { engine::DestroyImmediate(obj); }
    void Object::DestroyImmediate(Object* obj, bool allowDestroyingAssets) { engine::DestroyImmediate(obj, allowDestroyingAssets); }
    void Object::DontDestroyOnLoad(Object* target) { engine::DontDestroyOnLoad(target); }

    namespace
    {
        // The engine class of a type and, for a script type, the script type the objects must have.
        u32 ClassOfType(scripting::TypeId type, scripting::TypeId& script)
        {
            script = scripting::TypeId();
            const scripting::TypeInfo* t = scripting::GetType(type);
            if (t && t->module == detail::EngineModule() && t->kind == scripting::TypeKind::Class)
                return u32(t->user);
            if (const ScriptClass* s = FindScriptClass(type))
            {
                script = type;
                return s->engineClass;
            }
            return kNoClass;
        }

        bool OfScript(Object* o, scripting::TypeId script)
        {
            if (!script.IsValid())
                return true;
            const ScriptClass* s = ScriptClassOfObject(o);
            return s && scripting::IsAssignableFrom(script, s->type);
        }

        Array<Object*> Find(scripting::TypeId type, FindObjectsInactive inactive, FindObjectsSortMode sortMode)
        {
            Array<Object*> out;
            scripting::TypeId script;
            u32 klass = ClassOfType(type, script);
            if (klass != kNoClass)
                engine::FindObjectsByType(klass, inactive, out);
            for (usize i = out.Size(); i-- > 0;)
                if (!OfScript(out[i], script))
                    out.RemoveAt(i);
            if (sortMode == FindObjectsSortMode::InstanceID)
                for (usize i = 1; i < out.Size(); i++)
                    for (usize j = i; j > 0 && out[j]->GetEntityId().value < out[j - 1]->GetEntityId().value; j--)
                        Swap(out[j], out[j - 1]);
            return out;
        }
    }

    Array<Object*> Object::FindObjectsByType(scripting::TypeId type, FindObjectsSortMode sortMode)
    {
        return Find(type, FindObjectsInactive::Exclude, sortMode);
    }
    Array<Object*> Object::FindObjectsByType(scripting::TypeId type, FindObjectsInactive findObjectsInactive, FindObjectsSortMode sortMode)
    {
        return Find(type, findObjectsInactive, sortMode);
    }
    Object* Object::FindAnyObjectByType(scripting::TypeId type) { return FindAnyObjectByType(type, FindObjectsInactive::Exclude); }
    Object* Object::FindAnyObjectByType(scripting::TypeId type, FindObjectsInactive findObjectsInactive)
    {
        Array<Object*> all = Find(type, findObjectsInactive, FindObjectsSortMode::None);
        return all.Empty() ? nullptr : all[0];
    }
    // The first by id (the reference's first by instance id).
    Object* Object::FindFirstObjectByType(scripting::TypeId type) { return FindFirstObjectByType(type, FindObjectsInactive::Exclude); }
    Object* Object::FindFirstObjectByType(scripting::TypeId type, FindObjectsInactive findObjectsInactive)
    {
        Array<Object*> all = Find(type, findObjectsInactive, FindObjectsSortMode::InstanceID);
        return all.Empty() ? nullptr : all[0];
    }
}
