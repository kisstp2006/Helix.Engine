#include <Scripts/TestScripts.h>
#include <Support/TestClasses.h>
#include <Support/TestShaders.h>

#include <Helix/Engine/Application/Application.h>
#include <Helix/Engine/Time/Time.h>
#include <Helix/Engine/Asset/Asset.h>
#include <Helix/Engine/Material/Material.h>
#include <Helix/Engine/Mesh/Mesh.h>
#include <Helix/Engine/Rendering/Camera.h>
#include <Helix/Engine/Rendering/Graphics.h>
#include <Helix/Engine/Rendering/Renderer.h>
#include <Helix/Engine/Scene/SceneFile.h>
#include <Helix/Engine/Scene/SceneManager.h>
#include <Helix/Engine/Script/ScriptRuntime.h>
#include <Helix/Scripting/Bindings/Interop.h>
#include <Helix/Core/Logging/Log.h>
#include <Helix/Testing/Test.h>

#include <initializer_list>

HX_BINDINGS_EXPORT void HxBindings_EngineTests_Publish();

using namespace helix;
using namespace helix::engine;
using namespace helix::engine::testing;

namespace
{
    // The test module registered (through its exports by name, as a module linked into a player is).
    const ScriptClass& Script(const char* name)
    {
        static bool registered = false;
        if (!registered)
        {
            HxBindings_EngineTests_Publish();
            String error;
            registered = RegisterLinkedGameModule("EngineTests", error);
            if (!registered)
                std::printf("    %.*s\n", int(error.Size()), error.Data());
        }
        const ScriptClass* s = FindScriptClass("EngineTests", "EngineTests", name);
        HX_ASSERT(s != nullptr);
        return *s;
    }

    template <class T>
    T* Add(GameObject* g, const char* name)
    {
        return static_cast<T*>(AddScript(g, Script(name)));
    }

    bool Calls(std::initializer_list<const char*> expected)
    {
        Array<String>& calls = ScriptCalls();
        bool same = calls.Size() == expected.size();
        usize i = 0;
        for (const char* e : expected)
            same = same && i < calls.Size() && calls[i++] == e;
        if (!same)
        {
            std::printf("    calls:");
            for (const String& c : calls)
                std::printf(" %.*s", int(c.Size()), c.Data());
            std::printf("\n");
        }
        calls.Clear();
        return same;
    }

    void Frame()
    {
        RunScriptPhase(ScriptMessage::FixedUpdate);
        RunScriptPhase(ScriptMessage::Update);
        RunScriptPhase(ScriptMessage::LateUpdate);
    }
}

HX_TEST(Scripts, CppScriptsGetTheReferencesOrder)
{
    ScriptCalls().Clear();
    GameObject* g = GameObject::Create("A");
    auto* r = Add<Recorder>(g, "Recorder");
    HX_REQUIRE(r != nullptr && r->script() == &Script("Recorder") && g->GetComponent(Script("Recorder").type) == r);
    HX_CHECK(Calls({"A:Awake", "A:OnEnable"}));
    // Start before the first FixedUpdate, once.
    Frame();
    HX_CHECK(Calls({"A:Start", "A:FixedUpdate", "A:Update", "A:LateUpdate"}));
    Frame();
    HX_CHECK(Calls({"A:FixedUpdate", "A:Update", "A:LateUpdate"}));
    r->SetEnabled(false);
    Frame();
    r->SetEnabled(true);
    HX_CHECK(Calls({"A:OnDisable", "A:OnEnable"}));
    // Deactivating the GameObject disables, activating enables (no second Awake).
    g->SetActive(false);
    g->SetActive(true);
    HX_CHECK(Calls({"A:OnDisable", "A:OnEnable"}));
    DestroyImmediate(g);
    HX_CHECK(Calls({"A:OnDisable", "A:OnDestroy"}));
    Cleanup();
}

HX_TEST(Scripts, InactiveAndDisabledScripts)
{
    ScriptCalls().Clear();
    GameObject* g = GameObject::Create("B");
    g->SetActive(false);
    auto* r = Add<Recorder>(g, "Recorder");
    Frame();
    HX_CHECK(Calls({}));
    // Awake when the GameObject first becomes active; a disabled script is awake too, but gets no OnEnable or Start.
    r->SetEnabled(false);
    g->SetActive(true);
    Frame();
    HX_CHECK(Calls({"B:Awake"}));
    r->SetEnabled(true);
    Frame();
    HX_CHECK(Calls({"B:OnEnable", "B:Start", "B:FixedUpdate", "B:Update", "B:LateUpdate"}));
    // A script never awake gets no OnDestroy.
    GameObject* never = GameObject::Create("Never");
    never->SetActive(false);
    Add<Recorder>(never, "Recorder");
    DestroyImmediate(never);
    DestroyImmediate(g);
    HX_CHECK(Calls({"B:OnDisable", "B:OnDestroy"}));
    Cleanup();
}

HX_TEST(Scripts, ExecutionOrderThenCreation)
{
    ScriptCalls().Clear();
    GameObject* g = GameObject::Create("C");
    Add<Late>(g, "Late");
    auto* first = Add<Recorder>(g, "Recorder");
    Add<Early>(g, "Early");
    GameObject* h = GameObject::Create("D");
    auto* second = Add<Recorder>(h, "Recorder");
    (void)first;
    (void)second;
    Frame();
    ScriptCalls().Clear();
    RunScriptPhase(ScriptMessage::Update);
    HX_CHECK(Calls({"C:Early.Update", "C:Update", "D:Update", "C:Late.Update"}));
    HX_CHECK(Script("Early").defaultOrder == -10 && Script("Late").defaultOrder == 10 && ScriptPhaseCount(ScriptMessage::Update) == 4);
    DestroyImmediate(g);
    DestroyImmediate(h);
    Cleanup();
}

HX_TEST(Scripts, ScriptsChangedDuringAPhase)
{
    ScriptCalls().Clear();
    GameObject* g = GameObject::Create("E");
    auto* d = Add<Disabler>(g, "Disabler");
    auto* r = Add<Recorder>(g, "Recorder");
    Frame();
    ScriptCalls().Clear();
    // Disabled by a script before it in the same phase: not called.
    d->target = r;
    RunScriptPhase(ScriptMessage::Update);
    HX_CHECK(Calls({"E:Disabler.Update", "E:OnDisable"}));
    r->SetEnabled(true);
    ScriptCalls().Clear();
    // Destroyed by a script before it: not called either.
    d->target = r;
    d->destroy = true;
    RunScriptPhase(ScriptMessage::Update);
    HX_CHECK(Calls({"E:Disabler.Update", "E:OnDisable", "E:OnDestroy"}));
    // A script that disables itself in its Update.
    auto* self = Add<Recorder>(g, "Recorder");
    self->disableInUpdate = true;
    Frame();
    Frame();
    HX_CHECK(Calls({"E:Awake", "E:OnEnable", "E:Start", "E:FixedUpdate", "E:Disabler.Update", "E:Update", "E:OnDisable", "E:Disabler.Update"}));
    DestroyImmediate(g);
    Cleanup();
}

HX_TEST(Scripts, ApplicationAndHierarchyMessages)
{
    ScriptCalls().Clear();
    GameObject* parent = GameObject::Create("P");
    GameObject* child = GameObject::Create("K");
    Add<Recorder>(parent, "Recorder");
    Add<Recorder>(child, "Recorder");
    Frame();
    ScriptCalls().Clear();
    Application::SetFocused(false);
    Application::SetFocused(true);
    Application::SetPaused(true);
    HX_CHECK(Calls({"P:OnApplicationFocus(false)", "K:OnApplicationFocus(false)", "P:OnApplicationFocus(true)", "K:OnApplicationFocus(true)",
                    "P:OnApplicationPause(true)", "K:OnApplicationPause(true)"}));
    Application::SetPaused(false);
    ScriptCalls().Clear();
    child->transform()->SetParent(parent->transform());
    HX_CHECK(Calls({"K:OnTransformParentChanged", "P:OnTransformChildrenChanged"}));
    child->transform()->SetParent(nullptr);
    HX_CHECK(Calls({"K:OnTransformParentChanged", "P:OnTransformChildrenChanged"}));
    DestroyImmediate(parent);
    DestroyImmediate(child);
    ScriptCalls().Clear();
    Cleanup();
}

HX_TEST(Scripts, SceneFilesMakeCppScriptsFromTheirMonoScript)
{
    ScriptCalls().Clear();
    // The script asset, as the asset of a guid.
    MonoScript* asset = MonoScript::FromScriptClass(Script("Recorder"));
    Guid guid;
    Guid::TryParse("d0000000000000000000000000000004", guid);
    RegisterAssetObject(guid, 11500000, asset, true);
    const char* file = R"(%YAML 1.1
%TAG !h! tag:helixengine,2026:
--- !h!1 &100
GameObject:
  m_Component:
  - component: {fileID: 101}
  - component: {fileID: 102}
  m_Name: Loaded
  m_IsActive: 1
--- !h!4 &101
Transform:
  m_GameObject: {fileID: 100}
  m_Children: []
  m_Father: {fileID: 0}
--- !h!114 &102
GameBehaviour:
  m_GameObject: {fileID: 100}
  m_Enabled: 1
  m_Script: {fileID: 11500000, guid: d0000000000000000000000000000004, type: 3}
)";
    Scene s = SceneManager::CreateScene("Scripted");
    Array<String> errors;
    HX_CHECK(ReadSceneFile(Span<const u8>(reinterpret_cast<const u8*>(file), std::strlen(file)), s, errors) && errors.Empty());
    for (const String& e : errors)
        std::printf("    %.*s\n", int(e.Size()), e.Data());
    GameObject* loaded = GameObject::Find("Loaded");
    HX_REQUIRE(loaded != nullptr);
    auto* r = static_cast<GameBehaviour*>(loaded->GetComponent(Script("Recorder").type));
    HX_REQUIRE(r != nullptr);
    HX_CHECK(r->ClassIndex() == Script("Recorder").engineClass && r->scriptAsset() == asset);
    HX_CHECK(Calls({"Loaded:Awake", "Loaded:OnEnable"}));
    // A copy is a script of the same class, with its own lifecycle.
    Instantiate(loaded);
    HX_CHECK(Calls({"Loaded(Clone):Awake", "Loaded(Clone):OnEnable"}));
    SceneManager::UnloadSceneAsync(s);
    SceneManager::ProcessSceneLoads();
    ScriptCalls().Clear();
    Cleanup();
}

HX_TEST(Scripts, UnloadingTheModuleDestroysItsScripts)
{
    ScriptCalls().Clear();
    GameObject* g = GameObject::Create("U");
    Add<Recorder>(g, "Recorder");
    u32 klass = Script("Recorder").engineClass;
    ScriptCalls().Clear();
    UnloadGameModule("EngineTests");
    HX_CHECK(Calls({"U:OnDisable", "U:OnDestroy"}) && g->GetComponentCount() == 1);
    HX_CHECK(FindScriptClass("EngineTests", "EngineTests", "Recorder") == nullptr && NewObjectOfClass(klass) == nullptr);
    // Registered again: new classes, scripts work again.
    String error;
    HX_CHECK(RegisterLinkedGameModule("EngineTests", error));
    const ScriptClass* again = FindScriptClass("EngineTests", "EngineTests", "Recorder");
    HX_REQUIRE(again != nullptr);
    HX_CHECK(again->engineClass != klass && AddScript(g, *again) != nullptr);
    HX_CHECK(Calls({"U:Awake", "U:OnEnable"}));
    DestroyImmediate(g);
    ScriptCalls().Clear();
    Cleanup();
}

namespace
{
    u32 gErrors = 0;
    void CountErrors(const LogMessage& m, void*)
    {
        if (m.level >= LogLevel::Error)
            gErrors++;
    }
}

HX_TEST(Scripts, SendMessageReachesCppScriptsByName)
{
    ScriptCalls().Clear();
    GameObject* top = GameObject::Create("Top");
    GameObject* mid = GameObject::Create("Mid");
    GameObject* low = GameObject::Create("Low");
    mid->transform()->SetParent(top->transform());
    low->transform()->SetParent(mid->transform());
    for (GameObject* g : {top, mid, low})
        Add<Receiver>(g, "Receiver");
    ScriptCalls().Clear();
    HX_CHECK(SendScriptMessage(mid, "Ping") && Calls({"Mid:Ping"}));
    HX_CHECK(SendScriptMessage(mid, "Ping", scripting::Variant(), SendMessageOptions::RequireReceiver, MessageReach::Children) && Calls({"Mid:Ping", "Low:Ping"}));
    HX_CHECK(SendScriptMessage(mid, "Toggle", scripting::Variant::Bool(true), SendMessageOptions::RequireReceiver, MessageReach::Parents) &&
             Calls({"Mid:Toggle(true)", "Top:Toggle(true)"}));
    // Inactive GameObjects are left out; RequireReceiver logs when no script has the method.
    low->SetActive(false);
    HX_CHECK(SendScriptMessage(mid, "Ping", scripting::Variant(), SendMessageOptions::RequireReceiver, MessageReach::Children) && Calls({"Mid:Ping"}));
    gErrors = 0;
    AddLogSink(&CountErrors, nullptr);
    HX_CHECK(!SendScriptMessage(top, "Nothing"));
    HX_CHECK(!SendScriptMessage(top, "Nothing", scripting::Variant(), SendMessageOptions::DontRequireReceiver));
    RemoveLogSink(&CountErrors, nullptr);
    HX_CHECK(gErrors == 1);
    DestroyImmediate(top);
    ScriptCalls().Clear();
    Cleanup();
}

HX_TEST(Scripts, VisibilityMessagesFollowTheCameras)
{
    rhi::Device* device = nullptr;
    for (rhi::Backend b : {rhi::Backend::D3D11, rhi::Backend::OpenGL})
        if (!device && rhi::Device::IsBackendAvailable(b))
            device = rhi::Device::Create({.backend = b, .vsync = false});
    if (!device)
    {
        std::printf("    no device here, skipped\n");
        return;
    }
    {
        TestShaderFolder shaders;
        constexpr u32 size = 16;
        rhi::TextureHandle color =
            device->CreateTexture({.format = rhi::Format::RGBA8Unorm, .width = size, .height = size, .usage = rhi::TextureRenderTarget | rhi::TextureSampled});
        rhi::TextureHandle depth = device->CreateTexture({.format = rhi::Format::D32Float, .width = size, .height = size, .usage = rhi::TextureDepthStencil});
        Graphics::SetDevice(device);
        Graphics::SetTarget({color, depth, size, size, rhi::Format::RGBA8Unorm, rhi::Format::D32Float});
        GameObject* camera = GameObject::Create("Camera");
        camera->transform()->SetPosition(Vector3(0, 0, -10));
        Camera* c = camera->AddComponent<Camera>();
        c->SetOrthographic(true);
        c->SetOrthographicSize(2);
        Mesh* quad = NewObject<Mesh>();
        const Vector3 v[] = {{-0.5f, -0.5f, 0}, {-0.5f, 0.5f, 0}, {0.5f, 0.5f, 0}, {0.5f, -0.5f, 0}};
        const i32 t[] = {0, 1, 2, 0, 2, 3};
        quad->SetVertices(Span<const Vector3>(v, 4));
        quad->SetTriangles(Span<const i32>(t, 6));
        GameObject* g = GameObject::Create("Seen");
        g->AddComponent<MeshFilter>()->SetSharedMesh(quad);
        g->AddComponent<MeshRenderer>()->SetSharedMaterial(Material::Create(Shader::Find("Tests/Unlit")));
        Add<Watcher>(g, "Watcher");
        ScriptCalls().Clear();
        // Seen by the camera, still seen, out of its view, back.
        Graphics::RenderFrame();
        HX_CHECK(Calls({"Seen:OnBecameVisible"}));
        Graphics::RenderFrame();
        HX_CHECK(Calls({}));
        g->transform()->SetPosition(Vector3(100, 0, 0));
        Graphics::RenderFrame();
        HX_CHECK(Calls({"Seen:OnBecameInvisible"}));
        g->transform()->SetPosition(Vector3(0, 0, 0));
        Graphics::RenderFrame();
        HX_CHECK(Calls({"Seen:OnBecameVisible"}));
        Graphics::ReleaseResources();
        device->Destroy(color);
        device->Destroy(depth);
    }
    Graphics::SetDevice(nullptr);
    rhi::Device::Destroy(device);
    ScriptCalls().Clear();
    Cleanup();
}

HX_TEST(Scripts, TheScriptAssetsOrderComesBeforeTheClasses)
{
    ScriptCalls().Clear();
    // Script Execution Order (the MonoScript's) over DefaultExecutionOrder: Late (10 by its class) runs at -20.
    MonoScript* early = MonoScript::FromScriptClass(Script("Early"));
    MonoScript* late = MonoScript::FromScriptClass(Script("Late"));
    late->executionOrder = -20;
    Guid earlyGuid, lateGuid;
    Guid::TryParse("d0000000000000000000000000000005", earlyGuid);
    Guid::TryParse("d0000000000000000000000000000006", lateGuid);
    RegisterAssetObject(earlyGuid, 11500000, early, true);
    RegisterAssetObject(lateGuid, 11500000, late, true);
    const char* file = R"(%YAML 1.1
%TAG !h! tag:helixengine,2026:
--- !h!1 &100
GameObject:
  m_Component:
  - component: {fileID: 101}
  - component: {fileID: 102}
  - component: {fileID: 103}
  m_Name: Ordered
  m_IsActive: 1
--- !h!4 &101
Transform:
  m_GameObject: {fileID: 100}
  m_Children: []
  m_Father: {fileID: 0}
--- !h!114 &102
GameBehaviour:
  m_GameObject: {fileID: 100}
  m_Enabled: 1
  m_Script: {fileID: 11500000, guid: d0000000000000000000000000000005, type: 3}
--- !h!114 &103
GameBehaviour:
  m_GameObject: {fileID: 100}
  m_Enabled: 1
  m_Script: {fileID: 11500000, guid: d0000000000000000000000000000006, type: 3}
)";
    Scene s = SceneManager::CreateScene("Ordered");
    Array<String> errors;
    HX_CHECK(ReadSceneFile(Span<const u8>(reinterpret_cast<const u8*>(file), std::strlen(file)), s, errors) && errors.Empty());
    Frame();
    ScriptCalls().Clear();
    RunScriptPhase(ScriptMessage::Update);
    HX_CHECK(Calls({"Ordered:Late.Update", "Ordered:Early.Update"}));
    SceneManager::UnloadSceneAsync(s);
    SceneManager::ProcessSceneLoads();
    ScriptCalls().Clear();
    Cleanup();
}

HX_TEST(Scripts, TheCppScriptApi)
{
    using ::HelixEngine::GameObject;
    ScriptCalls().Clear();
    (void)Script("ApiScript");
    // Handles of engine objects: properties, methods, null semantics.
    GameObject g = GameObject::New("Api");
    HX_REQUIRE(g && g != nullptr);
    HX_CHECK(String(g.name) == "Api" && g.activeSelf && g.activeInHierarchy);
    g.name = "Renamed";
    engine::GameObject* native = EntityIdToObject<engine::GameObject>(g.GetEntityId());
    HX_REQUIRE(native != nullptr);
    HX_CHECK(native->Name() == StringView("Renamed"));
    g.SetActive(false);
    HX_CHECK(!g.activeSelf);
    g.SetActive(true);
    // A script added through the API: its handle members reach the engine.
    auto script = g.AddComponent<ApiScript>();
    HX_REQUIRE(script && script.Get() != nullptr);
    RunScriptPhase(ScriptMessage::Update);
    HX_CHECK(Calls({"api.Start:Renamed:on", "api.self:same", "api.OnDisable:inactive"}));
    HX_CHECK(!script.Get()->enabled && g.GetComponent<ApiScript>().Get() == script.Get());
    GameObject copy = g;
    HX_CHECK(copy == g);
    DestroyImmediate(native);
    HX_CHECK(!g && g == nullptr && copy == GameObject() && !script);
    // A static class over the engine's generator: one sequence for the API and Helix.Math.
    ::HelixEngine::Random::InitState(77);
    f32 a = ::HelixEngine::Random::value;
    i32 b = ::HelixEngine::Random::Range(0, 100);
    Random::InitState(77);
    HX_CHECK(a == Random::value() && b == Random::Range(0, 100));
    // Events take a callback and answer a token; statics of Time, Application and Debug.
    static i32 focusChanges = 0;
    u64 token = ::HelixEngine::Application::focusChanged.Add({[](void*, bool focused) { focusChanges += focused ? 10 : 1; }, nullptr});
    HX_CHECK(token != 0);
    Application::SetFocused(false);
    Application::SetFocused(true);
    ::HelixEngine::Application::focusChanged.Remove(token);
    Application::SetFocused(false);
    Application::SetFocused(true);
    HX_CHECK(focusChanges == 11);
    HX_CHECK(f32(::HelixEngine::Time::deltaTime) == Time::deltaTime() && i32(::HelixEngine::Time::frameCount) == Time::frameCount());
    ::HelixEngine::Time::timeScale = 0.5f;
    HX_CHECK(Time::timeScale() == 0.5f);
    ::HelixEngine::Time::timeScale = 1.0f;
    HX_CHECK(String(::HelixEngine::Application::productName) == String(Application::productName()));
    struct Lines
    {
        Array<String> texts;
    } lines{Array<String>()};
    LogSink sink = [](const LogMessage& m, void* user) { static_cast<Lines*>(user)->texts.Push(String(m.text)); };
    AddLogSink(sink, &lines);
    ::HelixEngine::Debug::Log("from the api");
    ::HelixEngine::Debug::Assert(false, "checked");
    RemoveLogSink(sink, &lines);
    HX_CHECK(lines.texts.Size() == 2 && lines.texts[0] == "from the api" && lines.texts[1] == "Assertion failed: checked");
    ScriptCalls().Clear();
    Cleanup();
}
