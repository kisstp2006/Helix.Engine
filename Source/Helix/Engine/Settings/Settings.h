#pragma once

#include <Helix/Engine/Object/Object.h>

// Settings assets: one object per kind for the whole project (time, tags and layers, the player's product), read from
// the project's settings files (one YAML document per object, as scene files). Kinds register by class ID with what
// applying them does; more kinds (quality, graphics, physics) register the same way.
namespace helix::engine
{
    namespace SettingsClassId
    {
        inline constexpr i32 TimeManager = 5, TagManager = 78, PlayerSettings = 129, BuildSettings = 141;
    }

    class TimeManager final : public Object
    {
        HX_ENGINE_CLASS(TimeManager, Object, SettingsClassId::TimeManager, ClassNone)

    public:
        f32 fixedTimestep = 0.02f;
        f32 maximumAllowedTimestep = 0.3333333f;
        f32 timeScale = 1;
        f32 maximumParticleTimestep = 0.03f;

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("Fixed Timestep", fixedTimestep);
            v.Field("Maximum Allowed Timestep", maximumAllowedTimestep);
            v.Field("m_TimeScale", timeScale);
            v.Field("Maximum Particle Timestep", maximumParticleTimestep);
        }
    };

    class TagManager final : public Object
    {
        HX_ENGINE_CLASS(TagManager, Object, SettingsClassId::TagManager, ClassNone)

    public:
        // The tags after the built in ones; the 32 layer names (the built in ones keep theirs).
        Array<String> tags = Array<String>();
        Array<String> layers = Array<String>();

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("tags", tags);
            v.Field("layers", layers);
        }
    };

    class PlayerSettings final : public Object
    {
        HX_ENGINE_CLASS(PlayerSettings, Object, SettingsClassId::PlayerSettings, ClassNone)

    public:
        String companyName = String("DefaultCompany");
        String productName = String("Product");
        String bundleVersion = String("0.1");
        String applicationIdentifier;

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("companyName", companyName);
            v.Field("productName", productName);
            v.Field("bundleVersion", bundleVersion);
            v.Field("applicationIdentifier", applicationIdentifier);
        }
    };

    // The build: its scenes in order (paths of scene assets), the first one starts the player.
    class BuildSettings final : public Object
    {
        HX_ENGINE_CLASS(BuildSettings, Object, SettingsClassId::BuildSettings, ClassNone)

    public:
        Array<String> scenes = Array<String>();

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("scenes", scenes);
        }
    };

    // A kind of settings: its class (a registered engine class) and what applying it does.
    void RegisterSettings(u32 klass, void (*apply)(Object& settings));
    // The settings object of a kind (made with its defaults on first use); null for kinds not registered.
    Object* GetSettings(u32 klass);
    template <class T>
    T* GetSettings()
    {
        return static_cast<T*>(GetSettings(ClassOf<T>()));
    }
    // Reads a settings file (YAML or binary, any number of settings documents) into the settings objects and applies
    // them; documents of kinds not registered are warned about.
    bool LoadSettingsFile(Span<const u8> bytes, Array<String>& errors);
    inline bool LoadSettingsFile(StringView text, Array<String>& errors)
    {
        return LoadSettingsFile(Span<const u8>(reinterpret_cast<const u8*>(text.Data()), text.Size()), errors);
    }
    // Applies a settings object again after its fields changed.
    void ApplySettings(u32 klass);
}
