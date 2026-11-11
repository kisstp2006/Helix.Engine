#pragma once

#include <Helix/Core/Strings/Format.h>
#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Engine/Scene/SceneManager.h>

#include <cstdio>

// Classes for the tests: behaviours that log their hooks, components with references, and a small class tree.
namespace helix::engine::testing
{
    // "<name>:<event>" per hook, in order.
    inline Array<String>& Events()
    {
        static Array<String> e;
        return e;
    }

    inline bool HasEvents(std::initializer_list<const char*> expected)
    {
        bool same = Events().Size() == expected.size();
        usize i = 0;
        for (const char* e : expected)
            same = same && i < Events().Size() && Events()[i++] == StringView(e);
        if (!same)
        {
            std::printf("    events:");
            for (const String& e : Events())
                std::printf(" %s", e.CStr());
            std::printf("\n");
        }
        Events().Clear();
        return same;
    }

    class Logger : public Behaviour
    {
        HX_ENGINE_CLASS(Logger, Behaviour, 2000001, ClassNone)

    public:
        void OnEnableNative() { Events().Push(Format("{}:enable", Name())); }
        void OnDisableNative() { Events().Push(Format("{}:disable", Name())); }
        void OnDestroyNative() { Events().Push(Format("{}:destroy", Name())); }
    };

    // Holds references, to see what Instantiate does with them.
    class Linker final : public Component
    {
        HX_ENGINE_CLASS(Linker, Component, 2000002, ClassNone)

    public:
        PPtr<GameObject> target;
        Array<PPtr<Object>> list = Array<PPtr<Object>>();
        i32 value = 0;

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("target", target);
            v.Field("list", list);
            v.Field("value", value);
        }
    };

    class Single final : public Component
    {
        HX_ENGINE_CLASS(Single, Component, 2000003, ClassDisallowMultiple)
    };

    // Needs a Linker and a Single.
    class Needy final : public Component
    {
        HX_ENGINE_CLASS(Needy, Component, 2000004, ClassNone)

    public:
        static void Requires(Array<u32>& out)
        {
            out.Push(ClassOf<Linker>());
            out.Push(ClassOf<Single>());
        }
    };

    class SubLogger final : public Logger
    {
        HX_ENGINE_CLASS(SubLogger, Logger, 2000005, ClassNone)
    };

    // An object that is not a component, as assets are.
    class Data final : public Object
    {
        HX_ENGINE_CLASS(Data, Object, 2000006, ClassNone)

    public:
        PPtr<Object> other;
        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("other", other);
        }
    };

    // Removes every object a test made.
    inline void Cleanup()
    {
        // By id: destroying a root frees its children and components, so pointers taken up front can dangle.
        Array<Object*> all;
        Array<EntityId> ids;
        FindObjectsOfTypeAll(ClassOf<GameObject>(), all);
        for (Object* o : all)
            ids.Push(o->GetEntityId());
        for (EntityId id : ids)
            if (GameObject* g = EntityIdToObject<GameObject>(id))
                if (!g->transform() || !g->transform()->parent())
                    DestroyImmediate(g, true);
        FindObjectsOfTypeAll(ClassOf<Object>(), all);
        ids.Clear();
        for (Object* o : all)
            ids.Push(o->GetEntityId());
        for (EntityId id : ids)
            if (Object* o = EntityIdToObject(id))
                DestroyImmediate(o, true);
        while (PendingDestroyCount())
            ProcessDestroyQueue(), SetLifecycleTime(LifecycleTime() + 1e9);
        SetLifecycleTime(0);
        Events().Clear();
    }
}
