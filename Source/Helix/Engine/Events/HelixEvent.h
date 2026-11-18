#pragma once

#include <Helix/Engine/Event/Event.h>
#include <Helix/Engine/Object/Object.h>
#include <Helix/Scripting/Types/Variant.h>

// HelixEvent: an event of a component that scene files wire up (persistent calls: a target object, a method or
// property setter by name, an argument of the call or the event's) and scripts listen to at run time. Native
// components (the canvas UI's controls) keep theirs here; C#'s HelixEvent classes reach them through
// HelixEventNative (the class and the event's index, registered by the component).
namespace helix::engine
{
    HX_ENUM(Namespace = "HelixEngine.Events")
    enum class PersistentListenerMode : i32
    {
        EventDefined = 0,
        Void = 1,
        Object = 2,
        Int = 3,
        Float = 4,
        String = 5,
        Bool = 6,
    };

    HX_ENUM(Namespace = "HelixEngine.Events")
    enum class HelixEventCallState : i32
    {
        Off = 0,
        EditorAndRuntime = 1,
        RuntimeOnly = 2,
    };

    // A persistent call's own argument (by its mode).
    struct ArgumentCache
    {
        static constexpr StringView kTypeName = "ArgumentCache";
        PPtr<Object> objectArgument;
        String objectArgumentAssemblyTypeName;
        i32 intArgument = 0;
        f32 floatArgument = 0;
        String stringArgument;
        bool boolArgument = false;

        template <class V>
        void Transfer(V& v)
        {
            v.Field("m_ObjectArgument", objectArgument);
            v.Field("m_ObjectArgumentAssemblyTypeName", objectArgumentAssemblyTypeName);
            v.Field("m_IntArgument", intArgument);
            v.Field("m_FloatArgument", floatArgument);
            v.Field("m_StringArgument", stringArgument);
            v.Field("m_BoolArgument", boolArgument);
        }
    };

    struct PersistentCall
    {
        static constexpr StringView kTypeName = "PersistentCall";
        PPtr<Object> target;
        String targetAssemblyTypeName;
        String methodName;
        i32 mode = i32(PersistentListenerMode::Void);
        ArgumentCache arguments;
        i32 callState = i32(HelixEventCallState::RuntimeOnly);

        template <class V>
        void Transfer(V& v)
        {
            v.Field("m_Target", target);
            v.Field("m_TargetAssemblyTypeName", targetAssemblyTypeName);
            v.Field("m_MethodName", methodName);
            v.Field("m_Mode", mode);
            v.Field("m_Arguments", arguments);
            v.Field("m_CallState", callState);
        }
    };

    struct PersistentCallGroup
    {
        static constexpr StringView kTypeName = "PersistentCallGroup";
        Array<PersistentCall> calls;

        template <class V>
        void Transfer(V& v)
        {
            v.Field("m_Calls", calls);
        }
    };

    // Calls a method (or a property setter, "set_name") of any object by name: a script's (C# or C++), or a bound
    // class's; false when it has none that takes the arguments.
    bool InvokeObjectMethod(Object* target, StringView method, Span<const scripting::Variant> arguments);

    // The persistent calls of an event (what every HelixEvent shares, whatever its arguments).
    class HelixEventStorage
    {
    public:
        static constexpr StringView kTypeName = "HelixEventBase";
        PersistentCallGroup& persistentCalls() { return persistent_; }
        const PersistentCallGroup& persistentCalls() const { return persistent_; }
        // The persistent calls that run, with the event's arguments for those of mode EventDefined.
        void InvokePersistent(Span<const scripting::Variant> eventArguments) const;

        template <class V>
        void Transfer(V& v)
        {
            v.Field("m_PersistentCalls", persistent_);
        }

    private:
        PersistentCallGroup persistent_;
    };

    // A component's event with typed arguments: its persistent calls, then its listeners (C++ code, and C#'s through
    // the component's bindings).
    template <class... Args>
    class HelixEvent : public HelixEventStorage
    {
    public:
        Event<Args...>& listeners() { return listeners_; }
        void Invoke(Args... args)
        {
            const scripting::Variant values[sizeof...(Args) + 1] = {scripting::Variant::From(args)..., scripting::Variant()};
            InvokePersistent(Span<const scripting::Variant>(values, sizeof...(Args)));
            listeners_.Invoke(args...);
        }

    private:
        Event<Args...> listeners_;
    };

    // The events of native component classes by index: their storage and a raise with Variant arguments.
    struct NativeEventEntry
    {
        HelixEventStorage* (*storage)(Object* owner) = nullptr;
        void (*raise)(Object* owner, Span<const scripting::Variant> arguments) = nullptr;
    };
    void RegisterNativeEvent(u32 klass, i32 index, const NativeEventEntry& entry);
    // The events of a script class of C++ (the canvas UI's controls): by its type, found for C# classes below it too.
    void RegisterNativeEvent(scripting::TypeId scriptType, i32 index, const NativeEventEntry& entry);
    const NativeEventEntry* FindNativeEvent(const Object* owner, i32 index);

    // C#'s HelixEvent of a native component: its persistent calls and its raise (HelixEngine.Events, internal).
    HX_CLASS(Namespace = "HelixEngine.Events", Static, Internal)
    class HelixEventNative
    {
    public:
        HX_FUNCTION() static i32 PersistentCount(Object* owner, i32 index);
        HX_FUNCTION() static Object* PersistentTarget(Object* owner, i32 index, i32 call);
        HX_FUNCTION() static String PersistentMethodName(Object* owner, i32 index, i32 call);
        HX_FUNCTION() static HelixEventCallState GetPersistentState(Object* owner, i32 index, i32 call);
        HX_FUNCTION() static void SetPersistentState(Object* owner, i32 index, i32 call, HelixEventCallState state);
        HX_FUNCTION() static void Raise(Object* owner, i32 index);
        HX_FUNCTION() static void RaiseBool(Object* owner, i32 index, bool value);
        HX_FUNCTION() static void RaiseInt(Object* owner, i32 index, i32 value);
        HX_FUNCTION() static void RaiseFloat(Object* owner, i32 index, f32 value);
        HX_FUNCTION() static void RaiseString(Object* owner, i32 index, StringView value);
        HX_FUNCTION() static void RaiseVector2(Object* owner, i32 index, Vector2 value);
        HX_FUNCTION() static void RaiseObject(Object* owner, i32 index, Object* value);
    };
}
