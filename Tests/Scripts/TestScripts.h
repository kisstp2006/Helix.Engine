#pragma once

#include <Helix/Core/Strings/Format.h>
#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Engine/Script/GameBehaviour.h>
#include <HelixEngine/HelixEngineApi.h>

// C++ scripts of the tests (a game module of their own, EngineTests): each writes the messages it gets to one list.
namespace helix::engine::testing
{
    inline Array<String>& ScriptCalls()
    {
        static Array<String> calls;
        return calls;
    }

    inline void Note(const GameBehaviour* b, const char* message)
    {
        ScriptCalls().Push(Format("{}:{}", b->gameObject() ? b->gameObject()->Name() : StringView("?"), message));
    }

    // Every message (private, as scripts write them).
    HX_CLASS(Namespace = "EngineTests")
    class Recorder : public GameBehaviour
    {
    public:
        // Disables itself in its first Update when set.
        bool disableInUpdate = false;

    private:
        void Awake() { Note(this, "Awake"); }
        void OnEnable() { Note(this, "OnEnable"); }
        void Start() { Note(this, "Start"); }
        void FixedUpdate() { Note(this, "FixedUpdate"); }
        void Update()
        {
            Note(this, "Update");
            if (disableInUpdate)
                SetEnabled(false);
        }
        void LateUpdate() { Note(this, "LateUpdate"); }
        void OnDisable() { Note(this, "OnDisable"); }
        void OnDestroy() { Note(this, "OnDestroy"); }
        void OnApplicationFocus(bool focus) { Note(this, focus ? "OnApplicationFocus(true)" : "OnApplicationFocus(false)"); }
        void OnApplicationPause(bool paused) { Note(this, paused ? "OnApplicationPause(true)" : "OnApplicationPause(false)"); }
        void OnApplicationQuit() { Note(this, "OnApplicationQuit"); }
        void OnTransformParentChanged() { Note(this, "OnTransformParentChanged"); }
        void OnTransformChildrenChanged() { Note(this, "OnTransformChildrenChanged"); }
    };

    // The visibility messages of the renderers of its GameObject.
    HX_CLASS(Namespace = "EngineTests")
    class Watcher : public GameBehaviour
    {
        void OnBecameVisible() { Note(this, "OnBecameVisible"); }
        void OnBecameInvisible() { Note(this, "OnBecameInvisible"); }
    };

    HX_CLASS(Namespace = "EngineTests", Order = -10)
    class Early : public GameBehaviour
    {
    public:
        void Update() { Note(this, "Early.Update"); }
    };

    HX_CLASS(Namespace = "EngineTests", Order = 10)
    class Late : public GameBehaviour
    {
    public:
        void Update() { Note(this, "Late.Update"); }
    };

    // Disables (or destroys) another script in its Update: the other one is not called after that.
    HX_CLASS(Namespace = "EngineTests")
    class Disabler : public GameBehaviour
    {
    public:
        GameBehaviour* target = nullptr;
        bool destroy = false;

        void Update()
        {
            Note(this, "Disabler.Update");
            if (target && destroy)
                DestroyImmediate(target);
            else if (target)
                target->SetEnabled(false);
            target = nullptr;
        }
    };

    // Methods SendMessage reaches by name (not messages of the engine).
    HX_CLASS(Namespace = "EngineTests")
    class Receiver : public GameBehaviour
    {
        void Ping() { Note(this, "Ping"); }
        void Toggle(bool on) { Note(this, on ? "Toggle(true)" : "Toggle(false)"); }
    };

    // A script on the C++ script API (value handles, properties as in C#).
    HX_CLASS(Namespace = "EngineTests")
    class ApiScript : public ::HelixEngine::GameBehaviour
    {
    public:
        void Start()
        {
            ::HelixEngine::GameObject g = gameObject;
            ScriptCalls().Push(Format("api.Start:{}:{}", String(g.name), enabled ? "on" : "off"));
            ::HelixEngine::Behaviour self = g.GetComponent<::HelixEngine::Behaviour>();
            ScriptCalls().Push(Format("api.self:{}", self.GetEntityId() == GetEntityId() ? "same" : "other"));
            enabled = false;
        }
        void OnDisable() { ScriptCalls().Push(Format("api.OnDisable:{}", isActiveAndEnabled ? "active" : "inactive")); }
    };

    HX_ENUM(Namespace = "EngineTests")
    enum class Pace : i32
    {
        Slow,
        Fast,
    };

    // A struct of script fields (by value and in arrays).
    HX_STRUCT(Namespace = "EngineTests")
    struct Waypoint
    {
        HX_FIELD() Vector3 position;
        HX_FIELD() i32 wait = 0;
    };

    // Serialized fields of every kind a C++ script has.
    HX_CLASS(Namespace = "EngineTests")
    class Fields : public GameBehaviour
    {
    public:
        HX_FIELD() i32 count = 3;
        HX_FIELD() f32 speed = 1.5f;
        HX_FIELD() bool on = true;
        HX_FIELD() Pace pace = Pace::Fast;
        HX_FIELD() String title = String("fields");
        HX_FIELD() Array<i32> numbers = Array<i32>();
        HX_FIELD() Array<String> words = Array<String>();
        HX_FIELD() Vector3 offset;
        HX_FIELD() Color tint = Color(1, 1, 1, 1);
        HX_FIELD() Waypoint home;
        HX_FIELD() Array<Waypoint> route = Array<Waypoint>();
        HX_FIELD() GameObject* target = nullptr;
        HX_FIELD() Array<Transform*> points = Array<Transform*>();
        HX_FIELD(NonSerialized) i32 scratch = 0;
        i32 untagged = 0;
        // OnBeforeSerialize and OnAfterDeserialize, counted.
        i32 serialized = 0;
        i32 deserialized = 0;

        i32 Secret() const { return secret_; }

    private:
        HX_FIELD(SerializeField, Name = "secret") i32 secret_ = 7;

        void OnBeforeSerialize() { serialized++; }
        void OnAfterDeserialize() { deserialized++; }
    };

    // A derived script: its base's fields first.
    HX_CLASS(Namespace = "EngineTests")
    class MoreFields : public Fields
    {
    public:
        HX_FIELD() i32 extra = 1;
    };

    // Value handles of the C++ script API as fields.
    HX_CLASS(Namespace = "EngineTests")
    class ApiFields : public ::HelixEngine::GameBehaviour
    {
    public:
        HX_FIELD() ::HelixEngine::GameObject other;
        HX_FIELD() Array<::HelixEngine::Transform> marks = Array<::HelixEngine::Transform>();
        HX_FIELD() i32 level = 1;
    };

    // A script that counts its Updates (the benchmark).
    HX_CLASS(Namespace = "EngineTests")
    class Counter : public GameBehaviour
    {
    public:
        static inline u64 updates = 0;
        void Update() { updates++; }
    };
}
