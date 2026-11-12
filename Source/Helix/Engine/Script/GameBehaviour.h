#pragma once

#include <Helix/Engine/GameObject/GameObject.h>
#include <Helix/Engine/Script/MonoScript.h>
#include <Helix/Engine/Script/ScriptClass.h>

// GameBehaviour (class 114): the base of scripts in both languages. A C++ script is a C++ class deriving from it (its
// objects are made as that class); a C# script is a GameBehaviour whose C# object (of the C# class) is its wrapper,
// held strongly while the native object lives. The engine calls the messages the script class has, in the reference's
// order (Script/ScriptRuntime.h).
namespace helix::engine
{
    HX_CLASS(ScriptBase)
    class GameBehaviour : public Behaviour
    {
        HX_ENGINE_CLASS(GameBehaviour, Behaviour, ClassId::MonoBehaviour, ClassBehaviour)

    public:
        GameBehaviour();
        GameBehaviour(const GameBehaviour& other);
        ~GameBehaviour() override;

        // The script class (null for a GameBehaviour without one).
        const ScriptClass* script() const { return script_; }
        MonoScript* scriptAsset() const { return scriptAsset_.Get(); }

        // Native hooks (the class registry calls them).
        void OnActivateNative();
        void OnEnableNative();
        void OnDisableNative();
        void OnDestroyNative();
        // The class of the object a scene document makes: the C++ class of its script.
        static u32 ResolveYaml(const serialization::yaml::Tree& tree, u32 root, serialization::ReferenceTable* refs);

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("m_Script", scriptAsset_);
            if constexpr (V::kReading)
                AttachScriptAsset();
        }

    private:
        friend struct ScriptAccess;
        // The script of the asset m_Script names (a C# script gets its C# object).
        void AttachScriptAsset();

        PPtr<MonoScript> scriptAsset_;
        const ScriptClass* script_ = nullptr;
        // The order of creation (the last key of the order scripts run in).
        u64 sequence_ = 0;
        i32 order_ = 0;
        bool awoken_ = false;
        bool started_ = false;
        bool pendingStart_ = false;
        // In the lists of the phases (enabled on an active GameObject).
        bool listed_ = false;
    };
}
