#pragma once

#include <Helix/Engine/Object/Object.h>
#include <Helix/Engine/Script/ScriptClass.h>

// The script asset (class 115): the assembly or game module, namespace and class of a script, and the execution
// order the project gives it (the Script Execution Order settings; 0 leaves the class's DefaultExecutionOrder).
namespace helix::engine
{
    class MonoScript final : public Object
    {
        HX_ENGINE_CLASS(MonoScript, Object, ClassId::MonoScript, ClassNone)

    public:
        String className;
        String nameSpace;
        String assemblyName;
        i32 executionOrder = 0;

        // The script class it names (null while that is not loaded).
        const ScriptClass* GetScriptClass() const { return FindScriptClass(assemblyName, nameSpace, className); }
        // A script asset of a script class (persistent, as assets are).
        static MonoScript* FromScriptClass(const ScriptClass& script);

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("m_Name", name_);
            v.Field("m_ExecutionOrder", executionOrder);
            v.Field("m_ClassName", className);
            v.Field("m_Namespace", nameSpace);
            v.Field("m_AssemblyName", assemblyName);
        }
    };
}
