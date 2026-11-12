#include <Helix/Engine/Object/Implementation/ScriptingTypes.h>
#include <Helix/Engine/Object/Object.h>

#include <Helix/Scripting/Types/TypeBuilder.h>
#include <Helix/Serialization/Transfer/Traits.h>

namespace helix::engine::detail
{
    namespace
    {
        using namespace scripting;

        struct EngineTypes
        {
            ModuleId module;
            TypeId classId;

            EngineTypes()
            {
                module = RegisterModule("HelixEngine", ModuleKind::Engine);
                classId = DeclareType(module, "HelixEngine.Internal", "ClassIdAttribute", TypeKind::Class);
                TypeBuilder(classId).Field("id", TypeOf<i32>(), 0u);
            }
        };

        EngineTypes& Types()
        {
            static EngineTypes t;
            return t;
        }

        Object* AsObject(void* self) { return static_cast<Object*>(self); }

        // Object.ReadSerializedProperty(path): one serialized field as text, by its property path.
        bool ReadSerialized(const void*, void* self, Span<const Variant> args, Variant& result, String& error)
        {
            Object* o = AsObject(self);
            const ClassInfo& c = o->Class();
            String text;
            if (!c.readProperty)
            {
                error = Format("{} has no serialized fields", c.name);
                return false;
            }
            if (!c.readProperty(*o, args[0].AsText(), text, error))
                return false;
            result = Variant::Text(text);
            return true;
        }

        // Object.ApplySerializedProperty(path, value): one serialized field from text, as a prefab modification.
        bool ApplySerialized(const void*, void* self, Span<const Variant> args, Variant&, String& error)
        {
            Object* o = AsObject(self);
            const ClassInfo& c = o->Class();
            if (!c.applyProperty)
            {
                error = Format("{} has no serialized fields", c.name);
                return false;
            }
            serialization::AssetReference none;
            return c.applyProperty(*o, args[0].AsText(), args[1].AsText(), none, nullptr, error);
        }
    }

    ModuleId EngineModule() { return Types().module; }
    TypeId ClassIdAttribute() { return Types().classId; }

    TypeId DeclareScriptingType(u32 classIndex)
    {
        EngineTypes& t = Types();
        const ClassInfo& info = GetClass(classIndex);
        TypeId base = info.base != kNoClass ? GetClass(info.base).type : TypeId();
        TypeId type = DeclareType(t.module, "HelixEngine", info.name, TypeKind::Class, base, (info.flags & ClassAbstract) ? TypeAbstract : TypeNone);
        if (!type.IsValid())
            return type;
        TypeBuilder b(type);
        b.Size(info.size, info.alignment).User(classIndex).Attribute(t.classId, {Variant::From(info.classId)});
        // The root class: the serialized fields of every object by property path.
        if (info.base == kNoClass)
        {
            const TypeId text = TypeOf<String>();
            b.Method("ReadSerializedProperty", text, {ParameterInfo{"path", text}}, &ReadSerialized)
                .Method("ApplySerializedProperty", TypeId(), {ParameterInfo{"path", text}, ParameterInfo{"value", text}}, &ApplySerialized);
        }
        return b.Finish();
    }
}
