#include <Support/TestClasses.h>

#include <Helix/Engine/Object/Implementation/ScriptingTypes.h>
#include <Helix/Engine/Transform/Transform.h>
#include <Helix/Scripting/Types/Invoke.h>
#include <Helix/Scripting/Types/TypeBuilder.h>
#include <Helix/Testing/Test.h>

using namespace helix;
using namespace helix::engine;
using namespace helix::engine::testing;

HX_TEST(ScriptingTypes, EngineClassesAreTypesOfTheCore)
{
    using namespace scripting;
    TypeId transform = FindType("HelixEngine.Transform");
    TypeId component = FindType("HelixEngine.Component");
    TypeId object = FindType("HelixEngine.Object");
    HX_REQUIRE(transform.IsValid() && component.IsValid() && object.IsValid());
    HX_CHECK(GetClass(ClassOf<Transform>()).type == transform);
    const TypeInfo* t = GetType(transform);
    HX_CHECK(t->base == component && t->module == engine::detail::EngineModule() && GetModule(t->module)->kind == ModuleKind::Engine);
    HX_CHECK(t->user == ClassOf<Transform>() && t->size == sizeof(Transform));
    HX_CHECK(IsAssignableFrom(object, transform) && IsAssignableFrom(component, transform) && !IsAssignableFrom(transform, component));
    HX_CHECK((GetType(object)->flags & TypeAbstract) && !(t->flags & TypeAbstract));
    // The ClassId of the files as an attribute.
    const AttributeInfo* id = FindAttribute(transform, t->attributes, engine::detail::ClassIdAttribute());
    HX_REQUIRE(id);
    HX_CHECK(AttributeArguments(transform, *id)[0].AsInt() == ClassId::Transform);
    // Classes registered later (test classes) are types too, below their bases.
    TypeId logger = GetClass(ClassOf<Logger>()).type;
    HX_CHECK(logger.IsValid() && IsAssignableFrom(FindType("HelixEngine.Behaviour"), logger));
}

HX_TEST(ScriptingTypes, SerializedFieldsByReflection)
{
    using namespace scripting;
    GameObject* g = GameObject::Create("Probe");
    Transform* t = g->transform();
    TypeId transform = GetClass(ClassOf<Transform>()).type;
    Variant result;
    String error;
    // Inherited from Object: found on every engine type, dispatched on the object's own class.
    const Variant set[] = {Variant::Text("m_LocalPosition.x"), Variant::Text("3.5")};
    HX_CHECK(Invoke(transform, "ApplySerializedProperty", t, Span<const Variant>(set, 2), result, error));
    HX_CHECK(t->localPosition().x == 3.5f);
    const Variant path = Variant::Text("m_LocalPosition.x");
    HX_CHECK(Invoke(transform, "ReadSerializedProperty", t, Span<const Variant>(&path, 1), result, error) && result.AsText() == StringView("3.5"));
    const Variant name = Variant::Text("m_Name");
    HX_CHECK(Invoke(GetClass(ClassOf<GameObject>()).type, "ReadSerializedProperty", g, Span<const Variant>(&name, 1), result, error) &&
             result.AsText() == StringView("Probe"));
    const Variant bad = Variant::Text("m_Nothing");
    HX_CHECK(!Invoke(transform, "ReadSerializedProperty", t, Span<const Variant>(&bad, 1), result, error) && error.View().Contains("no field"));
    DestroyImmediate(g);
}
