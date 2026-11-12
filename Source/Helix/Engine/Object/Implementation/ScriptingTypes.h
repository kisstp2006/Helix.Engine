#pragma once

#include <Helix/Core/Basics/Types.h>
#include <Helix/Scripting/Types/Type.h>

// The engine's classes as types of the scripting core (module HelixEngine).
namespace helix::engine::detail
{
    // Declares the type of a registered class (its base's type is declared already).
    scripting::TypeId DeclareScriptingType(u32 classIndex);
    // The attribute that holds a class's ClassId (HelixEngine.Internal.ClassIdAttribute, one int argument).
    scripting::TypeId ClassIdAttribute();
    scripting::ModuleId EngineModule();
}
