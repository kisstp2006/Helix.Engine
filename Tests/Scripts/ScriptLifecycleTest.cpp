#include <Scripts/TestScripts.h>
#include <Support/TestClasses.h>
#include <Support/TestShaders.h>

#include <Helix/Engine/Application/Application.h>
#include <Helix/Engine/Time/Time.h>
#include <Helix/Engine/PlayerLoop/PlayerLoop.h>
#include <Helix/Engine/Script/Coroutines.h>
#include <Helix/Engine/Asset/Asset.h>
#include <Helix/Engine/Material/Material.h>
#include <Helix/Engine/Mesh/Mesh.h>
#include <Helix/Engine/Prefab/Prefab.h>
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

namespace
{
    // A scene file with one script of `className` (its MonoScript the asset of `guid`) holding `fields`, on Holder;
    // Holder has a child Pin.
    String ScriptScene(const char* className, const char* guidText, StringView fields)
    {
        Guid guid;
        Guid::TryParse(guidText, guid);
        RegisterAssetObject(guid, 11500000, MonoScript::FromScriptClass(Script(className)), true);
        String text = String(R"(%YAML 1.1
%TAG !h! tag:helixengine,2026:
--- !h!1 &100
GameObject:
  m_Component:
  - component: {fileID: 101}
  - component: {fileID: 102}
  m_Name: Holder
  m_IsActive: 1
--- !h!4 &101
Transform:
  m_GameObject: {fileID: 100}
  m_Children:
  - {fileID: 201}
  m_Father: {fileID: 0}
--- !h!114 &102
GameBehaviour:
  m_GameObject: {fileID: 100}
  m_Enabled: 1
  m_Script: {fileID: 11500000, guid: )");
        text.Append(guidText);
        text.Append(", type: 3}\n");
        text.Append(fields);
        text.Append(R"(--- !h!1 &200
GameObject:
  m_Component:
  - component: {fileID: 201}
  m_Name: Pin
  m_IsActive: 1
--- !h!4 &201
Transform:
  m_GameObject: {fileID: 200}
  m_Children: []
  m_Father: {fileID: 101}
)");
        return text;
    }

    constexpr const char* kFields = R"(  count: 9
  speed: 2.5
  on: 0
  pace: 0
  title: read
  numbers:
  - 1
  - 2
  words:
  - a
  - b c
  offset: {x: 1, y: 2, z: 3}
  tint: {r: 1, g: 0.5, b: 0, a: 1}
  home:
    position: {x: 4, y: 5, z: 6}
    wait: 4
  route:
  - position: {x: 0, y: 0, z: 1}
    wait: 1
  - position: {x: 0, y: 0, z: 2}
    wait: 2
  target: {fileID: 200}
  points:
  - {fileID: 201}
  - {fileID: 101}
  scratch: 5
  secret: 11
)";

    // The fields of kFields on the Fields of `holder` (references into holder's own hierarchy).
    bool FieldsAsRead(GameObject* holder, const char* className = "Fields")
    {
        auto* f = static_cast<Fields*>(holder->GetComponent(Script(className).type));
        if (!f)
            return false;
        GameObject* pin = holder->transform()->GetChild(0)->gameObject();
        bool ok = f->count == 9 && f->speed == 2.5f && !f->on && f->pace == Pace::Slow && f->title == StringView("read") &&
                  f->numbers.Size() == 2 && f->numbers[1] == 2 && f->words.Size() == 2 && f->words[1] == StringView("b c") &&
                  f->offset == Vector3(1, 2, 3) && f->tint == Color(1, 0.5f, 0, 1) && f->home.position == Vector3(4, 5, 6) && f->home.wait == 4 &&
                  f->route.Size() == 2 && f->route[1].position.z == 2 && f->route[1].wait == 2 && f->target == pin && f->points.Size() == 2 &&
                  f->points[0] == pin->transform() && f->points[1] == holder->transform() && f->scratch == 0 && f->Secret() == 11;
        if (!ok)
            std::printf("    fields: count %d speed %g title %.*s route %d target %p pin %p secret %d\n", f->count, double(f->speed), int(f->title.Size()),
                        f->title.Data(), int(f->route.Size()), static_cast<void*>(f->target), static_cast<void*>(pin), f->Secret());
        return ok;
    }

    GameObject* Loaded(Scene s, const String& text)
    {
        Array<String> errors;
        bool read = ReadSceneFile(StringView(text), s, errors);
        for (const String& e : errors)
            std::printf("    %.*s\n", int(e.Size()), e.Data());
        if (!read || !errors.Empty())
            return nullptr;
        Array<GameObject*> roots = s.GetRootGameObjects();
        return roots.Size() == 1 ? roots[0] : nullptr;
    }
}

HX_TEST(Scripts, CppScriptFieldsInSceneFiles)
{
    ScriptCalls().Clear();
    Scene s = SceneManager::CreateScene("FieldsSource");
    GameObject* holder = Loaded(s, ScriptScene("Fields", "d0000000000000000000000000000005", kFields));
    HX_REQUIRE(holder != nullptr);
    HX_CHECK(FieldsAsRead(holder));
    auto* f = static_cast<Fields*>(holder->GetComponent(Script("Fields").type));
    HX_CHECK(f->deserialized == 1 && f->serialized == 0);
    // Written again: the same values, NonSerialized and untagged members left out.
    String text, error;
    HX_REQUIRE(WriteSceneYaml(s, text, error));
    HX_CHECK(text.View().Contains("  count: 9\n  speed: 2.5\n  on: 0\n  pace: 0\n  title: read\n"));
    HX_CHECK(text.View().Contains("  offset: {x: 1, y: 2, z: 3}\n  tint: {r: 1, g: 0.5, b: 0, a: 1}\n"));
    HX_CHECK(text.View().Contains("  target: {fileID: 200}\n") && text.View().Contains("  secret: 11\n"));
    HX_CHECK(!text.View().Contains("scratch") && !text.View().Contains("untagged") && !text.View().Contains("serialized"));
    HX_CHECK(f->serialized == 1 && f->deserialized == 1);
    // AddComponent: the members' initializers.
    GameObject* added = GameObject::Create("Added");
    auto* fresh = static_cast<Fields*>(AddScript(added, Script("Fields")));
    HX_REQUIRE(fresh != nullptr);
    HX_CHECK(fresh->count == 3 && fresh->speed == 1.5f && fresh->on && fresh->title == StringView("fields") && fresh->Secret() == 7 && fresh->tint == Color(1, 1, 1, 1));
    DestroyImmediate(added);
    // Into another scene: references go to the copy's objects.
    Scene t = SceneManager::CreateScene("FieldsCopy");
    GameObject* copy = Loaded(t, text);
    HX_REQUIRE(copy != nullptr && copy != holder);
    HX_CHECK(FieldsAsRead(copy));
    // Binary, with and without type trees.
    for (bool trees : {false, true})
    {
        Array<u8> bytes;
        HX_REQUIRE(WriteSceneBinary(s, bytes, error, trees));
        Scene b = SceneManager::CreateScene(trees ? "FieldsTrees" : "FieldsBinary");
        Array<String> errors;
        HX_CHECK(ReadSceneFile(Span<const u8>(bytes), b, errors) && errors.Empty());
        for (const String& e : errors)
            std::printf("    %.*s\n", int(e.Size()), e.Data());
        Array<GameObject*> roots = b.GetRootGameObjects();
        HX_REQUIRE(roots.Size() == 1);
        HX_CHECK(FieldsAsRead(roots[0]));
        SceneManager::UnloadSceneAsync(b);
    }
    // Instantiate copies the fields; references inside the copied hierarchy go to the clones.
    GameObject* clone = Instantiate(holder);
    HX_REQUIRE(clone != nullptr);
    HX_CHECK(FieldsAsRead(clone));
    SceneManager::UnloadSceneAsync(s);
    SceneManager::UnloadSceneAsync(t);
    SceneManager::ProcessSceneLoads();
    ScriptCalls().Clear();
    Cleanup();
}

HX_TEST(Scripts, DerivedAndApiScriptFields)
{
    ScriptCalls().Clear();
    Scene s = SceneManager::CreateScene("MoreFields");
    String more = String(kFields);
    more.Append("  extra: 5\n");
    GameObject* holder = Loaded(s, ScriptScene("MoreFields", "d0000000000000000000000000000006", more));
    HX_REQUIRE(holder != nullptr);
    auto* m = static_cast<MoreFields*>(holder->GetComponent(Script("MoreFields").type));
    HX_REQUIRE(m != nullptr);
    HX_CHECK(m->extra == 5 && FieldsAsRead(holder, "MoreFields"));
    String text, error;
    HX_REQUIRE(WriteSceneYaml(s, text, error));
    HX_CHECK(text.View().Contains("  secret: 11\n  extra: 5\n"));
    // Value handles: object references of the API.
    Scene a = SceneManager::CreateScene("ApiFields");
    GameObject* api = Loaded(a, ScriptScene("ApiFields", "d0000000000000000000000000000007", "  other: {fileID: 200}\n  marks:\n  - {fileID: 201}\n  level: 4\n"));
    HX_REQUIRE(api != nullptr);
    auto* f = static_cast<ApiFields*>(api->GetComponent(Script("ApiFields").type));
    HX_REQUIRE(f != nullptr);
    GameObject* pin = api->transform()->GetChild(0)->gameObject();
    HX_CHECK(f->level == 4 && f->other.GetEntityId() == pin->GetEntityId() && f->marks.Size() == 1 &&
             f->marks[0].GetEntityId() == pin->transform()->GetEntityId());
    GameObject* clone = Instantiate(api);
    HX_REQUIRE(clone != nullptr);
    auto* cf = static_cast<ApiFields*>(clone->GetComponent(Script("ApiFields").type));
    HX_REQUIRE(cf != nullptr);
    GameObject* clonePin = clone->transform()->GetChild(0)->gameObject();
    HX_CHECK(cf->other.GetEntityId() == clonePin->GetEntityId() && cf->marks[0].GetEntityId() == clonePin->transform()->GetEntityId());
    // A reference to an object of another type reads as none.
    Scene w = SceneManager::CreateScene("WrongType");
    GameObject* wrong = Loaded(w, ScriptScene("ApiFields", "d0000000000000000000000000000007", "  other: {fileID: 201}\n"));
    HX_REQUIRE(wrong != nullptr);
    HX_CHECK(!static_cast<ApiFields*>(wrong->GetComponent(Script("ApiFields").type))->other);
    for (Scene x : {s, a, w})
        SceneManager::UnloadSceneAsync(x);
    SceneManager::ProcessSceneLoads();
    ScriptCalls().Clear();
    Cleanup();
}

namespace
{
    // A modification of the prefab's script (fileID 102).
    String ScriptModification(const char* guid, const char* path, const char* value, i64 reference = 0)
    {
        return Format("    - target: {{fileID: 102, guid: {}, type: 3}}\n      propertyPath: {}\n      value: {}\n      objectReference: {{fileID: {}}}\n", guid, path,
                      value, reference);
    }

    // A scene with Root holding an instance of the prefab of `guid` with `mods`.
    String PrefabInstanceScene(const String& guid, const String& mods)
    {
        String text = String(R"(%YAML 1.1
%TAG !h! tag:helixengine,2026:
--- !h!1 &1
GameObject:
  m_Component:
  - component: {fileID: 2}
  m_Name: Root
  m_IsActive: 1
--- !h!4 &2
Transform:
  m_GameObject: {fileID: 1}
  m_Children:
  - {fileID: 9001}
  m_Father: {fileID: 0}
--- !h!1001 &5000
PrefabInstance:
  serializedVersion: 2
  m_Modification:
    serializedVersion: 3
    m_TransformParent: {fileID: 2}
    m_Modifications:
)");
        text.Append(mods);
        text.Append(R"(    m_RemovedComponents: []
    m_RemovedGameObjects: []
    m_AddedGameObjects: []
    m_AddedComponents: []
  m_SourcePrefab: {fileID: 100100000, guid: )");
        text.Append(guid);
        text.Append(R"(, type: 3}
--- !h!4 &9001 stripped
Transform:
  m_CorrespondingSourceObject: {fileID: 101, guid: )");
        text.Append(guid);
        text.Append(R"(, type: 3}
  m_PrefabInstance: {fileID: 5000}
  m_PrefabAsset: {fileID: 0}
--- !h!1660057539 &9223372036854775807
SceneRoots:
  m_Roots:
  - {fileID: 2}
)");
        return text;
    }
}

HX_TEST(Scripts, CppScriptFieldsInPrefabs)
{
    ScriptCalls().Clear();
    const char* guid = "d0000000000000000000000000000008";
    Guid g;
    Guid::TryParse(guid, g);
    Array<String> errors;
    GameObject* prefab = LoadPrefab(StringView(ScriptScene("Fields", "d0000000000000000000000000000009", kFields)), errors, g);
    for (const String& e : errors)
        std::printf("    %.*s\n", int(e.Size()), e.Data());
    HX_REQUIRE(prefab != nullptr);
    String mods = ScriptModification(guid, "count", "12");
    mods.Append(ScriptModification(guid, "route.Array.data[1].wait", "9"));
    mods.Append(ScriptModification(guid, "title", "modified"));
    mods.Append(ScriptModification(guid, "target", "''", 1));
    Scene s = SceneManager::CreateScene("FieldsPrefab");
    String file = PrefabInstanceScene(String(guid), mods);
    errors.Clear();
    HX_CHECK(ReadSceneFile(StringView(file), s, errors) && errors.Empty());
    for (const String& e : errors)
        std::printf("    %.*s\n", int(e.Size()), e.Data());
    Array<GameObject*> roots = s.GetRootGameObjects();
    HX_REQUIRE(roots.Size() == 1 && roots[0]->transform()->childCount() == 1);
    GameObject* holder = roots[0]->transform()->GetChild(0)->gameObject();
    auto* f = static_cast<Fields*>(holder->GetComponent(Script("Fields").type));
    HX_REQUIRE(f != nullptr);
    // The modified fields; the others the prefab's, its references the instance's own objects.
    GameObject* pin = holder->transform()->GetChild(0)->gameObject();
    HX_CHECK(f->count == 12 && f->title == StringView("modified") && f->route[1].wait == 9 && f->route[0].wait == 1 && f->target == roots[0]);
    HX_CHECK(f->speed == 2.5f && f->points.Size() == 2 && f->points[0] == pin->transform() && f->points[1] == holder->transform() && f->Secret() == 11);
    SceneManager::UnloadSceneAsync(s);
    SceneManager::ProcessSceneLoads();
    ScriptCalls().Clear();
    Cleanup();
}

namespace
{
    // Frames by hand: 1/16 s each, one fixed step of 1/16 s per frame.
    struct CoroutineFrames
    {
        struct Clock final : TimeClock
        {
            double now = 10;
            double Now() override { return now; }
        } clock;

        CoroutineFrames()
        {
            Time::SetClock(&clock);
            Time::Reset();
            Time::SetFixedDeltaTime(0.0625f);
            PlayerLoop::SetPlayerLoop(PlayerLoop::GetDefaultPlayerLoop());
            PlayerLoop::RunFrame();
            ScriptCalls().Clear();
        }
        ~CoroutineFrames()
        {
            Time::SetClock(nullptr);
            Time::Reset();
            ScriptCalls().Clear();
            Cleanup();
        }
        void Frame()
        {
            clock.now += 0.0625;
            PlayerLoop::RunFrame();
        }
    };
}

HX_TEST(Scripts, CppCoroutinesResumeAtTheReferencesPoints)
{
    CoroutineFrames frames;
    GameObject* g = GameObject::Create("C");
    auto* r = Add<Routines>(g, "Routines");
    HX_REQUIRE(r != nullptr);
    frames.Frame();
    // Start runs the coroutine to its first yield; null resumes after the next frame's Update.
    HX_CHECK(Calls({"C:co.start", "C:FixedUpdate", "C:Update", "C:LateUpdate"}));
    frames.Frame();
    HX_CHECK(Calls({"C:FixedUpdate", "C:Update", "C:co.frame", "C:LateUpdate"}));
    frames.Frame();
    // The fixed update: after the step's FixedUpdates; the end of the frame: after rendering.
    HX_CHECK(Calls({"C:FixedUpdate", "C:co.fixed", "C:Update", "C:LateUpdate", "C:co.end"}));
    frames.Frame();
    HX_CHECK(Calls({"C:FixedUpdate", "C:Update", "C:LateUpdate"}));
    r->ready = true;
    frames.Frame();
    // The nested coroutine runs at once; the outer one goes on when it ends, in the same pass.
    HX_CHECK(Calls({"C:FixedUpdate", "C:Update", "C:co.ready", "C:inner.start", "C:LateUpdate"}));
    frames.Frame();
    HX_CHECK(Calls({"C:FixedUpdate", "C:Update", "C:inner.end", "C:co.done", "C:LateUpdate"}));
    HX_CHECK(!IsCoroutineRunning(r->running.handle));
    DestroyImmediate(g);
}

HX_TEST(Scripts, CppCoroutinesStopByTheReferencesRules)
{
    CoroutineFrames frames;
    GameObject* g = GameObject::Create("S");
    auto* r = Add<Routines>(g, "Routines");
    r->trace = false;
    frames.Frame();
    ScriptCalls().Clear();
    // Disabling the behaviour does not stop its coroutines.
    CoroutineRef a = r->StartCoroutine(r->Forever());
    HX_CHECK(a && Calls({"S:tick"}));
    r->SetEnabled(false);
    frames.Frame();
    HX_CHECK(Calls({"S:tick"}));
    // StopCoroutine; an inactive GameObject stops the others (enabled or not).
    r->StopCoroutine(a);
    frames.Frame();
    HX_CHECK(Calls({}) && !IsCoroutineRunning(a.handle));
    CoroutineRef b = r->StartCoroutine(r->Forever());
    HX_CHECK(Calls({"S:tick"}));
    g->SetActive(false);
    HX_CHECK(!IsCoroutineRunning(b.handle));
    // Started on an inactive GameObject: the reference's error, null.
    CoroutineRef c = r->StartCoroutine(r->Forever());
    HX_CHECK(!c && Calls({}));
    g->SetActive(true);
    r->SetEnabled(true);
    ScriptCalls().Clear();
    // StopAllCoroutines, and a coroutine that ends without yielding answers null.
    CoroutineRef d = r->StartCoroutine(r->Forever());
    CoroutineRef e = r->StartCoroutine(r->Forever());
    r->StopAllCoroutines();
    HX_CHECK(d && e && !IsCoroutineRunning(d.handle) && !IsCoroutineRunning(e.handle));
    HX_CHECK(!r->StartCoroutine(r->Ends()));
    ScriptCalls().Clear();
    // Destroying the behaviour stops its coroutines.
    CoroutineRef f = r->StartCoroutine(r->Forever());
    DestroyImmediate(r);
    HX_CHECK(!IsCoroutineRunning(f.handle));
    HX_CHECK(CoroutineCount() == 0);
    DestroyImmediate(g);
}

HX_TEST(Scripts, CppCoroutinesWaitForTimeAndPredicates)
{
    CoroutineFrames frames;
    GameObject* g = GameObject::Create("T");
    auto* r = Add<Routines>(g, "Routines");
    r->trace = false;
    frames.Frame();
    ScriptCalls().Clear();
    r->StartCoroutine(r->Seconds(0.2f));
    r->StartCoroutine(r->Timeout());
    Array<String> seen;
    for (int i = 0; i < 12; i++)
    {
        frames.Frame();
        for (const String& c : ScriptCalls())
            if (!c.View().EndsWith("Update"))
                seen.Push(Format("{}:{}", i, c));
        ScriptCalls().Clear();
        if (i == 6)
            r->ready = true;
    }
    // 0.2 s of game time is the fourth frame (1/16 s each), then 0.2 s of real time; the timeout of 0.1 s of game
    // time runs onTimeout, then WaitWhile waits for ready.
    String text;
    for (const String& s : seen)
        text.Append(Format("{} ", s));
    HX_CHECK(text == StringView("1:T:timeout 1:T:after.timeout 3:T:seconds 7:T:after.while 7:T:realtime "));
    DestroyImmediate(g);
}

HX_TEST(Scripts, CoroutineTimeoutsCountTheirTime)
{
    CoroutineFrames frames;
    GameObject* g = GameObject::Create("M");
    auto* r = Add<Routines>(g, "Routines");
    r->trace = false;
    frames.Frame();
    // With time stopped, only the real time timeout ends.
    Time::SetTimeScale(0);
    r->StartCoroutine(r->TimeoutIn(WaitTimeoutMode::Realtime, "real"));
    r->StartCoroutine(r->TimeoutIn(WaitTimeoutMode::InGameTime, "game"));
    ScriptCalls().Clear();
    for (int i = 0; i < 6; i++)
        frames.Frame();
    Array<String> seen;
    for (const String& c : ScriptCalls())
        if (!c.View().EndsWith("Update"))
            seen.Push(c);
    HX_CHECK(seen.Size() == 1 && seen[0] == StringView("M:real") && CoroutineCount() == 1);
    Time::SetTimeScale(1);
    DestroyImmediate(g);
    HX_CHECK(CoroutineCount() == 0);
}
