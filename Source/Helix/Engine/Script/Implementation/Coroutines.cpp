#include <Helix/Engine/Script/Coroutines.h>

#include <Helix/Core/Containers/Array.h>
#include <Helix/Core/Logging/Log.h>
#include <Helix/Engine/Script/GameBehaviour.h>
#include <Helix/Engine/Time/Time.h>
#include <Helix/Scripting/Bindings/Interop.h>

#include <bit>

namespace helix::engine
{
    namespace
    {
        LogChannel gCoroutineLog{"Scripts"};

        struct Record
        {
            u64 owner = 0;
            u64 state = 0;
            u32 language = 0;
            u32 generation = 1;
            CoroutineYield wait;
            f64 until = 0;
            i32 frame = 0;
            bool live = false;
            bool running = false;
            bool stopped = false;
        };

        struct Scheduler
        {
            Array<CoroutineLanguage> languages;
            Array<Record> records;
            Array<u32> free;
            // The handles waiting at each point, in the order they began to wait.
            Array<u64> lists[3];
            // The handles waiting for another coroutine.
            Array<u64> nested;
            u32 count = 0;
        };

        Scheduler& S()
        {
            static Scheduler s;
            return s;
        }

        u64 HandleOf(u32 slot) { return u64(S().records[slot].generation) << 32 | u64(slot + 1); }

        bool Find(u64 handle, u32& slot)
        {
            Scheduler& s = S();
            u32 low = u32(handle);
            if (low == 0 || low > s.records.Size())
                return false;
            slot = low - 1;
            const Record& r = s.records[slot];
            return r.live && r.generation == u32(handle >> 32);
        }

        u32 Allocate(u64 owner, u32 language, u64 state)
        {
            Scheduler& s = S();
            u32 slot;
            if (!s.free.Empty())
            {
                slot = s.free.Back();
                s.free.Pop();
            }
            else
            {
                slot = u32(s.records.Size());
                s.records.Push(Record());
            }
            Record& r = s.records[slot];
            r.owner = owner;
            r.state = state;
            r.language = language;
            r.live = true;
            r.running = r.stopped = false;
            r.wait = CoroutineYield();
            s.count++;
            return slot;
        }

        void Resume(u32 slot);

        // The routine is gone: its state is released, the coroutines waiting for it go on.
        void Finish(u32 slot)
        {
            Scheduler& s = S();
            u64 handle = HandleOf(slot);
            Record& r = s.records[slot];
            u64 state = r.state;
            CoroutineLanguage language = s.languages[r.language];
            r.live = false;
            r.generation++;
            s.free.Push(slot);
            s.count--;
            if (language.release)
                language.release(state);
            Array<u64> waiters;
            for (usize i = 0; i < s.nested.Size();)
            {
                u32 w;
                if (Find(s.nested[i], w) && s.records[w].wait.inner == handle)
                {
                    waiters.Push(s.nested[i]);
                    s.nested.RemoveAt(i);
                }
                else
                    i++;
            }
            for (u64 w : waiters)
            {
                u32 at;
                if (Find(w, at))
                    Resume(at);
            }
        }

        void Wait(u32 slot, const CoroutineYield& y)
        {
            Scheduler& s = S();
            Record& r = s.records[slot];
            u64 handle = HandleOf(slot);
            r.wait = y;
            r.frame = Time::frameCount();
            switch (CoroutineWait(y.wait))
            {
            case CoroutineWait::Seconds:
                r.until = Time::timeAsDouble() + y.seconds;
                s.lists[u32(CoroutinePoint::Update)].Push(handle);
                break;
            case CoroutineWait::RealSeconds:
                r.until = Time::realtimeSinceStartupAsDouble() + y.seconds;
                s.lists[u32(CoroutinePoint::Update)].Push(handle);
                break;
            case CoroutineWait::FixedUpdate: s.lists[u32(CoroutinePoint::FixedUpdate)].Push(handle); break;
            case CoroutineWait::EndOfFrame: s.lists[u32(CoroutinePoint::EndOfFrame)].Push(handle); break;
            case CoroutineWait::Coroutine:
            {
                u32 inner;
                if (Find(y.inner, inner) && y.inner != handle)
                {
                    s.nested.Push(handle);
                    break;
                }
                // A coroutine that already ended: the next frame.
                r.wait.wait = u32(CoroutineWait::NextFrame);
                s.lists[u32(CoroutinePoint::Update)].Push(handle);
                break;
            }
            default:
                // NextFrame, Poll and anything else: after the next frame's Update.
                s.lists[u32(CoroutinePoint::Update)].Push(handle);
                break;
            }
        }

        void Resume(u32 slot)
        {
            Scheduler& s = S();
            u64 handle = HandleOf(slot);
            CoroutineLanguage language = s.languages[s.records[slot].language];
            u64 state = s.records[slot].state;
            s.records[slot].running = true;
            CoroutineYield y;
            language.resume(state, handle, &y);
            // The records may have grown while it ran (coroutines it started).
            Record& r = s.records[slot];
            r.running = false;
            if (r.stopped || CoroutineWait(y.wait) == CoroutineWait::Ended)
                Finish(slot);
            else
                Wait(slot, y);
        }

        bool Ready(const Record& r, i32 frame)
        {
            switch (CoroutineWait(r.wait.wait))
            {
            case CoroutineWait::FixedUpdate:
            case CoroutineWait::EndOfFrame: return true;
            case CoroutineWait::Seconds: return frame > r.frame && Time::timeAsDouble() >= r.until;
            case CoroutineWait::RealSeconds: return frame > r.frame && Time::realtimeSinceStartupAsDouble() >= r.until;
            default: return frame > r.frame;
            }
        }

        // ------------------------------------------------------------------------------------------- the C ABI

        u32 ExportRegisterLanguage(void (*resume)(u64, u64, CoroutineYield*), u8 (*poll)(u64, u64), void (*release)(u64))
        {
            return RegisterCoroutineLanguage(CoroutineLanguage{resume, poll, release});
        }

        u64 ExportStart(u64 owner, u32 language, u64 state)
        {
            auto* b = static_cast<GameBehaviour*>(EntityIdToObject(EntityId::FromULong(owner)));
            if (!b || !b->Is<GameBehaviour>())
            {
                if (language < S().languages.Size() && S().languages[language].release)
                    S().languages[language].release(state);
                return 0;
            }
            return StartCoroutine(b, language, state);
        }

        u64 ExportWait(u32 language, u64 state, u32 wait, f64 seconds, u64 inner)
        {
            CoroutineYield y;
            y.wait = wait;
            y.seconds = seconds;
            y.inner = inner;
            return StartCoroutineWait(language, state, y);
        }

        void ExportStopAll(u64 owner)
        {
            if (auto* b = static_cast<GameBehaviour*>(EntityIdToObject(EntityId::FromULong(owner))))
                StopAllCoroutines(b);
        }

        u8 ExportIsRunning(u64 handle) { return IsCoroutineRunning(handle) ? 1 : 0; }

        Array<void (*)()>& DelayedTaskRunners()
        {
            static Array<void (*)()> runners;
            return runners;
        }
    }

    void AddDelayedTaskRunner(void (*run)())
    {
        for (auto r : DelayedTaskRunners())
            if (r == run)
                return;
        DelayedTaskRunners().Push(run);
    }

    void RunDelayedTasks()
    {
        for (usize i = 0; i < DelayedTaskRunners().Size(); i++)
            DelayedTaskRunners()[i]();
    }

    u32 RegisterCoroutineLanguage(const CoroutineLanguage& language)
    {
        S().languages.Push(language);
        return u32(S().languages.Size() - 1);
    }

    u64 StartCoroutine(GameBehaviour* owner, u32 language, u64 state)
    {
        Scheduler& s = S();
        if (language >= s.languages.Size())
            return 0;
        GameObject* g = owner ? owner->gameObject() : nullptr;
        if (owner && (!g || !g->activeInHierarchy()))
        {
            HX_LOG_ERROR(gCoroutineLog, "Coroutine couldn't be started because the the game object '{}' is inactive!", g ? g->Name() : StringView());
            if (s.languages[language].release)
                s.languages[language].release(state);
            return 0;
        }
        u32 slot = Allocate(owner ? owner->GetEntityId().value : 0, language, state);
        u64 handle = HandleOf(slot);
        Resume(slot);
        return IsCoroutineRunning(handle) ? handle : 0;
    }

    u64 StartCoroutineWait(u32 language, u64 state, const CoroutineYield& wait)
    {
        if (language >= S().languages.Size())
            return 0;
        u32 slot = Allocate(0, language, state);
        u64 handle = HandleOf(slot);
        Wait(slot, wait);
        return handle;
    }

    void StopCoroutine(u64 handle)
    {
        u32 slot;
        if (!Find(handle, slot))
            return;
        Record& r = S().records[slot];
        if (r.running)
            r.stopped = true;
        else
            Finish(slot);
    }

    void StopAllCoroutines(const GameBehaviour* owner)
    {
        if (!owner)
            return;
        Scheduler& s = S();
        u64 id = owner->GetEntityId().value;
        for (u32 i = 0; i < s.records.Size(); i++)
            if (s.records[i].live && s.records[i].owner == id)
                StopCoroutine(HandleOf(i));
    }

    bool IsCoroutineRunning(u64 handle)
    {
        u32 slot;
        return Find(handle, slot) && !S().records[slot].stopped;
    }

    void RunCoroutines(CoroutinePoint point)
    {
        Scheduler& s = S();
        Array<u64>& list = s.lists[u32(point)];
        if (list.Empty())
            return;
        Array<u64> now = Move(list);
        list = Array<u64>();
        Array<u64> keep;
        i32 frame = Time::frameCount();
        for (u64 handle : now)
        {
            u32 slot;
            if (!Find(handle, slot))
                continue;
            Record& r = s.records[slot];
            if (r.stopped)
                continue;
            bool ready = Ready(r, frame);
            if (ready && CoroutineWait(r.wait.wait) == CoroutineWait::Poll)
            {
                const CoroutineLanguage& language = s.languages[r.language];
                ready = !language.poll || language.poll(r.state, handle) == 0;
            }
            if (ready)
                Resume(slot);
            else
                keep.Push(handle);
        }
        // Those still waiting stay ahead of the ones that began to wait during this pass.
        for (u64 h : s.lists[u32(point)])
            keep.Push(h);
        s.lists[u32(point)] = Move(keep);
    }

    u32 CoroutineCount() { return S().count; }

    namespace detail
    {
        void RegisterCoroutineExports()
        {
            scripting::RegisterNativeExport("HxCoroutines_RegisterLanguage", std::bit_cast<const void*>(&ExportRegisterLanguage));
            scripting::RegisterNativeExport("HxCoroutines_Start", std::bit_cast<const void*>(&ExportStart));
            scripting::RegisterNativeExport("HxCoroutines_Wait", std::bit_cast<const void*>(&ExportWait));
            scripting::RegisterNativeExport("HxCoroutines_Stop", std::bit_cast<const void*>(static_cast<void (*)(u64)>(&StopCoroutine)));
            scripting::RegisterNativeExport("HxCoroutines_StopAll", std::bit_cast<const void*>(&ExportStopAll));
            scripting::RegisterNativeExport("HxCoroutines_IsRunning", std::bit_cast<const void*>(&ExportIsRunning));
            scripting::RegisterNativeExport("HxEngine_AddDelayedTaskRunner", std::bit_cast<const void*>(&AddDelayedTaskRunner));
        }
    }
}
