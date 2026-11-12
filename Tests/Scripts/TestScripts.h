#pragma once

#include <Helix/Core/Strings/Format.h>
#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Engine/Script/GameBehaviour.h>

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

    // A script that counts its Updates (the benchmark).
    HX_CLASS(Namespace = "EngineTests")
    class Counter : public GameBehaviour
    {
    public:
        static inline u64 updates = 0;
        void Update() { updates++; }
    };
}
