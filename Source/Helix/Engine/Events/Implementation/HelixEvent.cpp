#include <Helix/Engine/Script/ScriptClass.h>
#include <Helix/Engine/Script/ScriptRuntime.h>
#include <Helix/Engine/Events/HelixEvent.h>

#include <Helix/Core/Containers/HashMap.h>
#include <Helix/Core/Logging/Log.h>

namespace helix::engine
{
    namespace
    {
        LogChannel EventLog{"Engine"};

        struct Registry
        {
            // By class and index: (class << 16) | index.
            HashMap<u64, NativeEventEntry> entries;
            // By script type and index: (type << 32) | index.
            HashMap<u64, NativeEventEntry> scriptEntries;
        };

        Registry& R()
        {
            static Registry* r = new Registry();
            return *r;
        }

        u64 Key(u32 klass, i32 index) { return (u64(klass) << 16) | u64(u16(index)); }
    }

    void HelixEventStorage::InvokePersistent(Span<const scripting::Variant> eventArguments) const
    {
        using scripting::Variant;
        for (const PersistentCall& c : persistent_.calls)
        {
            if (c.callState == i32(HelixEventCallState::Off) || c.methodName.Empty())
                continue;
            Object* target = c.target.Get();
            if (!target)
                continue;
            Variant own;
            Span<const Variant> args;
            switch (PersistentListenerMode(c.mode))
            {
            case PersistentListenerMode::EventDefined:
                args = eventArguments;
                break;
            case PersistentListenerMode::Void:
                break;
            case PersistentListenerMode::Object:
                own = Variant::From(c.arguments.objectArgument.Get());
                args = Span<const Variant>(&own, 1);
                break;
            case PersistentListenerMode::Int:
                own = Variant::From(c.arguments.intArgument);
                args = Span<const Variant>(&own, 1);
                break;
            case PersistentListenerMode::Float:
                own = Variant::From(c.arguments.floatArgument);
                args = Span<const Variant>(&own, 1);
                break;
            case PersistentListenerMode::String:
                own = Variant::Text(c.arguments.stringArgument.View());
                args = Span<const Variant>(&own, 1);
                break;
            case PersistentListenerMode::Bool:
                own = Variant::Bool(c.arguments.boolArgument);
                args = Span<const Variant>(&own, 1);
                break;
            }
            if (!InvokeObjectMethod(target, c.methodName.View(), args))
                HX_LOG_WARNING(EventLog, "HelixEvent: {} has no method {} that takes the call's arguments.", target->Name(), c.methodName.View());
        }
    }

    void RegisterNativeEvent(u32 klass, i32 index, const NativeEventEntry& entry) { R().entries.Set(Key(klass, index), entry); }

    void RegisterNativeEvent(scripting::TypeId scriptType, i32 index, const NativeEventEntry& entry)
    {
        R().scriptEntries.Set((u64(scriptType.value) << 32) | u64(u32(index)), entry);
    }

    const NativeEventEntry* FindNativeEvent(const Object* owner, i32 index)
    {
        if (!owner)
            return nullptr;
        // A script class's events (a C++ script class, or a C# class below one): its type or the nearest base type.
        if (const ScriptClass* s = ScriptClassOfObject(owner))
            for (scripting::TypeId t = s->type; t.value != 0;)
            {
                if (const NativeEventEntry* e = R().scriptEntries.Find((u64(t.value) << 32) | u64(u32(index))))
                    return e;
                const scripting::TypeInfo* info = scripting::GetType(t);
                t = info ? info->base : scripting::TypeId();
            }
        // The class or the nearest base that registered the event.
        for (u32 k = owner->ClassIndex(); k != kNoClass; k = GetClass(k).base)
            if (const NativeEventEntry* e = R().entries.Find(Key(k, index)))
                return e;
        return nullptr;
    }

    // ------------------------------------------------------------------------------------------------ for C#

    namespace
    {
        const PersistentCall* CallOf(Object* owner, i32 index, i32 call)
        {
            const NativeEventEntry* e = FindNativeEvent(owner, index);
            HelixEventStorage* s = e && e->storage ? e->storage(owner) : nullptr;
            if (!s || call < 0 || usize(call) >= s->persistentCalls().calls.Size())
                return nullptr;
            return &s->persistentCalls().calls[usize(call)];
        }

        void RaiseWith(Object* owner, i32 index, Span<const scripting::Variant> args)
        {
            if (const NativeEventEntry* e = FindNativeEvent(owner, index); e && e->raise)
                e->raise(owner, args);
        }
    }

    i32 HelixEventNative::PersistentCount(Object* owner, i32 index)
    {
        const NativeEventEntry* e = FindNativeEvent(owner, index);
        HelixEventStorage* s = e && e->storage ? e->storage(owner) : nullptr;
        return s ? i32(s->persistentCalls().calls.Size()) : 0;
    }

    Object* HelixEventNative::PersistentTarget(Object* owner, i32 index, i32 call)
    {
        const PersistentCall* c = CallOf(owner, index, call);
        return c ? c->target.Get() : nullptr;
    }

    String HelixEventNative::PersistentMethodName(Object* owner, i32 index, i32 call)
    {
        const PersistentCall* c = CallOf(owner, index, call);
        return c ? c->methodName : String();
    }

    HelixEventCallState HelixEventNative::GetPersistentState(Object* owner, i32 index, i32 call)
    {
        const PersistentCall* c = CallOf(owner, index, call);
        return c ? HelixEventCallState(c->callState) : HelixEventCallState::Off;
    }

    void HelixEventNative::SetPersistentState(Object* owner, i32 index, i32 call, HelixEventCallState state)
    {
        if (auto* c = const_cast<PersistentCall*>(CallOf(owner, index, call)))
            c->callState = i32(state);
    }

    void HelixEventNative::Raise(Object* owner, i32 index) { RaiseWith(owner, index, Span<const scripting::Variant>()); }

    void HelixEventNative::RaiseBool(Object* owner, i32 index, bool value)
    {
        scripting::Variant v = scripting::Variant::Bool(value);
        RaiseWith(owner, index, Span<const scripting::Variant>(&v, 1));
    }

    void HelixEventNative::RaiseInt(Object* owner, i32 index, i32 value)
    {
        scripting::Variant v = scripting::Variant::From(value);
        RaiseWith(owner, index, Span<const scripting::Variant>(&v, 1));
    }

    void HelixEventNative::RaiseFloat(Object* owner, i32 index, f32 value)
    {
        scripting::Variant v = scripting::Variant::From(value);
        RaiseWith(owner, index, Span<const scripting::Variant>(&v, 1));
    }

    void HelixEventNative::RaiseString(Object* owner, i32 index, StringView value)
    {
        scripting::Variant v = scripting::Variant::Text(value);
        RaiseWith(owner, index, Span<const scripting::Variant>(&v, 1));
    }

    void HelixEventNative::RaiseVector2(Object* owner, i32 index, Vector2 value)
    {
        scripting::Variant v = scripting::Variant::From(value);
        RaiseWith(owner, index, Span<const scripting::Variant>(&v, 1));
    }

    void HelixEventNative::RaiseObject(Object* owner, i32 index, Object* value)
    {
        scripting::Variant v = scripting::Variant::From(value);
        RaiseWith(owner, index, Span<const scripting::Variant>(&v, 1));
    }
}
