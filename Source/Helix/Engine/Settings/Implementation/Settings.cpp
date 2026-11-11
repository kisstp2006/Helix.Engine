#include <Helix/Engine/Application/Application.h>
#include <Helix/Engine/GameObject/Tags.h>
#include <Helix/Engine/Settings/Settings.h>
#include <Helix/Engine/Time/Time.h>

#include <Helix/Core/Logging/Log.h>
#include <Helix/Serialization/Yaml/Parser.h>

namespace helix::engine
{
    namespace
    {
        LogChannel SettingsLog{"Engine"};

        struct Entry
        {
            u32 klass;
            void (*apply)(Object&);
            EntityId object;
        };

        void ApplyTime(Object& o)
        {
            TimeManager& t = static_cast<TimeManager&>(o);
            Time::SetFixedDeltaTime(t.fixedTimestep);
            Time::SetMaximumDeltaTime(t.maximumAllowedTimestep);
            Time::SetTimeScale(t.timeScale);
            Time::SetMaximumParticleDeltaTime(t.maximumParticleTimestep);
        }

        void ApplyTags(Object& o)
        {
            TagManager& t = static_cast<TagManager&>(o);
            for (const String& tag : t.tags)
            {
                TagHandle h;
                if (!tag.Empty())
                    AddTag(tag, h);
            }
            for (u32 i = 0; i < t.layers.Size() && i < kLayerCount; i++)
                if (!t.layers[i].Empty())
                    SetLayerName(i, t.layers[i]);
        }

        void ApplyPlayer(Object& o)
        {
            PlayerSettings& p = static_cast<PlayerSettings&>(o);
            Application::SetProductInfo(p.companyName, p.productName, p.bundleVersion, p.applicationIdentifier);
        }

        Array<Entry>& Entries()
        {
            static Array<Entry> e = [] {
                Array<Entry> built;
                built.Push({ClassOf<TimeManager>(), &ApplyTime, EntityId()});
                built.Push({ClassOf<TagManager>(), &ApplyTags, EntityId()});
                built.Push({ClassOf<PlayerSettings>(), &ApplyPlayer, EntityId()});
                return built;
            }();
            return e;
        }

        Entry* Find(u32 klass)
        {
            for (Entry& e : Entries())
                if (e.klass == klass)
                    return &e;
            return nullptr;
        }
    }

    void RegisterSettings(u32 klass, void (*apply)(Object&))
    {
        if (Entry* e = Find(klass))
            e->apply = apply;
        else
            Entries().Push({klass, apply, EntityId()});
    }

    Object* GetSettings(u32 klass)
    {
        Entry* e = Find(klass);
        if (!e)
            return nullptr;
        if (Object* o = EntityIdToObject(e->object))
            return o;
        Object* o = NewObjectOfClass(klass);
        if (!o)
            return nullptr;
        o->SetPersistent(true);
        e->object = o->GetEntityId();
        return o;
    }

    void ApplySettings(u32 klass)
    {
        Entry* e = Find(klass);
        Object* o = e ? GetSettings(klass) : nullptr;
        if (o && e->apply)
            e->apply(*o);
    }

    bool LoadSettingsFile(Span<const u8> bytes, Array<String>& errors)
    {
        using namespace serialization;
        // The engine's kinds register their classes on first use.
        Entries();
        yaml::Tree tree;
        String error;
        if (!yaml::Parse(StringView(reinterpret_cast<const char*>(bytes.Data()), bytes.Size()), tree, error))
        {
            errors.Push(error);
            return false;
        }
        usize before = errors.Size();
        for (const yaml::Document& d : tree.documents)
        {
            u32 klass = d.classID >= 0 && d.classID <= 0x7fffffff ? FindClassById(i32(d.classID)) : kNoClass;
            Object* o = klass != kNoClass && Find(klass) ? GetSettings(klass) : nullptr;
            if (!o)
            {
                HX_LOG_WARNING(SettingsLog, "Settings file: line {}: class ID {} is not a kind of settings; skipped.", d.line, d.classID);
                continue;
            }
            o->Class().readYaml(*o, tree, d.root, errors, nullptr);
            ApplySettings(klass);
        }
        return errors.Size() == before;
    }
}
