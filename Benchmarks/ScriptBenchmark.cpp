#include <Scripts/BenchScripts.h>

#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Engine/Script/ScriptRuntime.h>
#include <Helix/Scripting/Bindings/Interop.h>
#include <Helix/Testing/Bench.h>

HX_BINDINGS_EXPORT void HxBindings_EngineBench_Publish();

using namespace helix;
using namespace helix::engine;

// One Update phase over 10 000 C++ scripts (the M6 exit asks under 0.3 ms for the dispatch).
HX_BENCH(ScriptsUpdate10kCpp)
{
    HxBindings_EngineBench_Publish();
    String error;
    RegisterLinkedGameModule("EngineBench", error);
    const ScriptClass* spinner = FindScriptClass("EngineBench", "EngineBench", "Spinner");
    if (!spinner)
        return;
    Array<GameObject*> objects;
    for (u32 i = 0; i < 10000; i++)
    {
        GameObject* g = GameObject::Create("Spinner");
        AddScript(g, *spinner);
        objects.Push(g);
    }
    // Starts out of the way.
    RunScriptPhase(ScriptMessage::Update);
    while (state.KeepRunning())
        RunScriptPhase(ScriptMessage::Update);
    helix::testing::DoNotOptimize(bench::Spinner::updates);
    for (GameObject* g : objects)
        DestroyImmediate(g);
    UnloadGameModule("EngineBench");
}
