#include <Helix/Engine/Application/Application.h>
#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Engine/PlayerLoop/PlayerLoop.h>
#include <Helix/Engine/Scene/SceneManager.h>
#include <Helix/Engine/Time/Time.h>
#include <Helix/Engine/Transform/Hierarchy.h>

namespace helix::engine
{
    namespace
    {
        // A compiled loop: calls, and loops that jump back while their condition holds.
        struct Op
        {
            enum Kind : u8
            {
                Call,
                LoopBegin,
                LoopEnd,
            };
            Kind kind;
            void (*function)(void*) = nullptr;
            bool (*condition)(void*) = nullptr;
            void* user = nullptr;
            // LoopBegin: the op after its LoopEnd; LoopEnd: its LoopBegin.
            u32 jump = 0;
        };

        struct LoopState
        {
            PlayerLoopSystem current;
            Array<Op> ops;
            bool compiled = false;
            bool running = false;
            // A loop set while a frame runs takes over after it.
            bool pending = false;
            PlayerLoopSystem next;
        };

        LoopState& Loop()
        {
            static LoopState s;
            return s;
        }

        void Compile(const PlayerLoopSystem& s, Array<Op>& ops)
        {
            u32 begin = 0;
            if (s.loopConditionFunction)
            {
                begin = u32(ops.Size());
                Op b;
                b.kind = Op::LoopBegin;
                b.condition = s.loopConditionFunction;
                b.user = s.loopUser;
                ops.Push(b);
            }
            if (s.updateFunction)
            {
                Op c;
                c.kind = Op::Call;
                c.function = s.updateFunction;
                c.user = s.updateUser;
                ops.Push(c);
            }
            for (const PlayerLoopSystem& sub : s.subSystemList)
                Compile(sub, ops);
            if (s.loopConditionFunction)
            {
                Op e;
                e.kind = Op::LoopEnd;
                e.jump = begin;
                ops.Push(e);
                ops[begin].jump = u32(ops.Size());
            }
        }

        void Apply(const PlayerLoopSystem& loop)
        {
            LoopState& s = Loop();
            s.current = loop;
            s.ops.Clear();
            Compile(s.current, s.ops);
            s.compiled = true;
        }

        PlayerLoopSystem* FindSystem(PlayerLoopSystem& s, StringView type)
        {
            if (s.type == type)
                return &s;
            for (PlayerLoopSystem& sub : s.subSystemList)
                if (PlayerLoopSystem* found = FindSystem(sub, type))
                    return found;
            return nullptr;
        }

        bool RemoveFrom(PlayerLoopSystem& s, StringView type)
        {
            for (usize i = 0; i < s.subSystemList.Size(); i++)
            {
                if (s.subSystemList[i].type == type)
                {
                    s.subSystemList.RemoveAt(i);
                    return true;
                }
                if (RemoveFrom(s.subSystemList[i], type))
                    return true;
            }
            return false;
        }

        PlayerLoopSystem System(StringView type, void (*function)(void*) = nullptr)
        {
            PlayerLoopSystem s;
            s.type = type;
            s.updateFunction = function;
            return s;
        }

        PlayerLoopSystem Phase(StringView type, std::initializer_list<PlayerLoopSystem> systems)
        {
            PlayerLoopSystem s;
            s.type = type;
            for (const PlayerLoopSystem& sub : systems)
                s.subSystemList.Push(sub);
            return s;
        }

        // The engine's own systems.
        void UpdateTime(void*) { Time::BeginFrame(); }
        void PaceFrame(void*) { Application::PaceFrame(); }
        void SceneLoads(void*) { SceneManager::ProcessSceneLoads(); }
        void AsyncInstantiations(void*) { ProcessAsyncInstantiations(); }
        bool FixedStep(void*) { return Time::NextFixedStep(); }

        void DestroyQueue(void*)
        {
            SetLifecycleTime(Time::timeAsDouble());
            ProcessDestroyQueue();
        }

        void WorldMatrices(void*) { UpdateWorldMatrices(); }
        void BeforeRender(void*) { Application::onBeforeRender().Invoke(); }
    }

    PlayerLoopSystem PlayerLoop::GetDefaultPlayerLoop()
    {
        PlayerLoopSystem fixedUpdate = Phase("FixedUpdate", {
            System("ClearLines"), System("NewInputFixedUpdate"), System("AudioFixedUpdate"), System("ScriptRunBehaviourFixedUpdate"),
            System("DirectorFixedUpdate"), System("LegacyFixedAnimationUpdate"), System("XRFixedUpdate"), System("PhysicsFixedUpdate"),
            System("Physics2DFixedUpdate"), System("PhysicsClothFixedUpdate"), System("DirectorFixedUpdatePostPhysics"),
            System("ScriptRunDelayedFixedFrameRate"),
        });
        // Once per fixed step the frame's time allows.
        fixedUpdate.loopConditionFunction = &FixedStep;
        PlayerLoopSystem root;
        root.type = "PlayerLoop";
        root.subSystemList.Push(Phase("TimeUpdate", {System("FramePacing", &PaceFrame), System("WaitForLastPresentationAndUpdateTime", &UpdateTime)}));
        root.subSystemList.Push(Phase("Initialization", {
            System("ProfilerStartFrame"), System("UpdateCameraMotionVectors"), System("DirectorSampleTime"), System("AsyncUploadTimeSlicedUpdate"),
            System("SynchronizeInputs"), System("SynchronizeState"), System("XREarlyUpdate"),
        }));
        root.subSystemList.Push(Phase("EarlyUpdate", {
            System("PollPlayerConnection"), System("WebRequestUpdate"), System("ExecuteMainThreadJobs"), System("UpdatePreloading", &SceneLoads),
            System("AsyncInstantiation", &AsyncInstantiations), System("UpdateInputManager"), System("Physics2DEarlyUpdate"),
        }));
        root.subSystemList.Push(fixedUpdate);
        root.subSystemList.Push(Phase("PreUpdate", {
            System("PhysicsUpdate"), System("Physics2DUpdate"), System("PhysicsClothUpdate"), System("CheckTexFieldInput"),
            System("IMGUISendQueuedEvents"), System("NewInputUpdate"), System("SendMouseEvents"), System("AIUpdate"), System("WindUpdate"),
            System("UpdateVideo"),
        }));
        root.subSystemList.Push(Phase("Update", {
            System("ScriptRunBehaviourUpdate"), System("ScriptRunDelayedDynamicFrameRate"), System("ScriptRunDelayedTasks"), System("DirectorUpdate"),
        }));
        root.subSystemList.Push(Phase("PreLateUpdate", {
            System("AIUpdatePostScript"), System("DirectorUpdateAnimationBegin"), System("LegacyAnimationUpdate"),
            System("DirectorUpdateAnimationEnd"), System("Physics2DLateUpdate"), System("PhysicsLateUpdate"), System("ScriptRunBehaviourLateUpdate"),
        }));
        root.subSystemList.Push(Phase("PostLateUpdate", {
            // After the update loop, before rendering.
            System("DestroyQueue", &DestroyQueue), System("PlayerSendFrameStarted"), System("DirectorLateUpdate"),
            System("UpdateWorldMatrices", &WorldMatrices), System("PlayerUpdateCanvases"), System("UIElementsRepaintPanels"), System("UpdateAudio"),
            System("VFXUpdate"), System("BeforeRender", &BeforeRender), System("FinishFrameRendering"), System("BatchModeUpdate"),
            System("PresentAfterDraw"), System("ProfilerEndFrame"), System("ObjectDispatcherPostLateUpdate"),
        }));
        return root;
    }

    PlayerLoopSystem PlayerLoop::GetCurrentPlayerLoop()
    {
        LoopState& s = Loop();
        if (!s.compiled)
            Apply(GetDefaultPlayerLoop());
        return s.pending ? s.next : s.current;
    }

    void PlayerLoop::SetPlayerLoop(const PlayerLoopSystem& loop)
    {
        LoopState& s = Loop();
        if (s.running)
        {
            s.next = loop;
            s.pending = true;
            return;
        }
        Apply(loop);
    }

    bool PlayerLoop::InsertSystem(StringView parent, const PlayerLoopSystem& system, StringView before)
    {
        PlayerLoopSystem loop = GetCurrentPlayerLoop();
        PlayerLoopSystem* p = FindSystem(loop, parent);
        if (!p)
            return false;
        usize at = p->subSystemList.Size();
        for (usize i = 0; i < p->subSystemList.Size() && !before.Empty(); i++)
            if (p->subSystemList[i].type == before)
            {
                at = i;
                break;
            }
        p->subSystemList.Insert(at, system);
        SetPlayerLoop(loop);
        return true;
    }

    bool PlayerLoop::RemoveSystem(StringView type)
    {
        PlayerLoopSystem loop = GetCurrentPlayerLoop();
        if (!RemoveFrom(loop, type))
            return false;
        SetPlayerLoop(loop);
        return true;
    }

    void PlayerLoop::RunFrame()
    {
        LoopState& s = Loop();
        if (!s.compiled)
            Apply(GetDefaultPlayerLoop());
        s.running = true;
        const Op* ops = s.ops.Data();
        u32 n = u32(s.ops.Size());
        for (u32 i = 0; i < n;)
        {
            const Op& op = ops[i];
            switch (op.kind)
            {
            case Op::Call:
                op.function(op.user);
                i++;
                break;
            case Op::LoopBegin:
                i = op.condition(op.user) ? i + 1 : op.jump;
                break;
            case Op::LoopEnd:
                i = op.jump;
                break;
            }
        }
        s.running = false;
        if (s.pending)
        {
            s.pending = false;
            Apply(s.next);
        }
    }
}
