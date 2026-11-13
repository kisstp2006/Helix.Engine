#pragma once

#include <Helix/Engine/GameObject/GameObject.h>
#include <Helix/Engine/Script/MonoScript.h>
#include <Helix/Engine/Script/ScriptClass.h>
#include <Helix/Serialization/Transfer/Dynamic.h>

// ScriptableObject: a script object of either language that is no component. Its documents are GameBehaviour
// documents of class 114 (GameBehaviour's resolve hooks decide the class of a 114 document: this one when the script
// derives from it); they carry m_GameObject (none), m_Enabled, m_Script, m_Name and the script's fields. Awake and
// OnEnable come when it is made (CreateInstance), read from a file or copied (Instantiate); OnDisable and OnDestroy
// when it is destroyed.
namespace helix::engine
{
    HX_CLASS(ScriptBase)
    class ScriptableObject : public Object
    {
        HX_ENGINE_CLASS_DOCUMENT(ScriptableObject, Object, ClassId::MonoBehaviour, ClassSharedId, "GameBehaviour")

    public:
        ScriptableObject() = default;
        ScriptableObject(const ScriptableObject& other) : Object(other), scriptAsset_(other.scriptAsset_), script_(other.script_) {}
        ~ScriptableObject() override = default;

        // An object of the script class (C# or C++) of that type, or of ScriptableObject itself, awake and enabled;
        // null with an error for a type that is no ScriptableObject.
        HX_FUNCTION(Generic = "T : ScriptableObject") static ScriptableObject* CreateInstance(scripting::TypeId type);
        // The script class of that name (the class name, or namespace and name).
        HX_FUNCTION(Name = "CreateInstance") static ScriptableObject* CreateInstanceByName(StringView className);

        // The script class (null for a ScriptableObject without one).
        const ScriptClass* script() const { return script_; }
        MonoScript* scriptAsset() const { return scriptAsset_.Get(); }

        // Native hooks (the class registry calls them).
        void OnLoadedNative();
        void OnDestroyNative();
        void OnCloneNative(const ScriptableObject& original);
        static u32 ResolveYaml(const serialization::yaml::Tree& tree, u32 root, serialization::ReferenceTable* refs);

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            TransferPrefabLinks(v);
            // The fields of a 114 document a GameObject's script has: none and enabled here.
            PPtr<GameObject> gameObject;
            v.Field("m_GameObject", gameObject);
            u8 enabled = 1;
            v.Field("m_Enabled", enabled);
            v.Field("m_Script", scriptAsset_);
            if constexpr (V::kReading)
                AttachScriptAsset();
            v.Field("m_Name", name_);
            if (script_ && script_->transfer)
            {
                serialization::DynamicVisitor d = serialization::DynamicAdapter<V>::Make(v);
                if ((d.flags & serialization::kDynamicSerializes) && script_->beforeSerialize)
                    script_->beforeSerialize(static_cast<Object*>(this));
                script_->transfer(static_cast<Object*>(this), &d);
                if constexpr (V::kReading)
                    if (script_->afterDeserialize)
                        script_->afterDeserialize(static_cast<Object*>(this));
            }
        }

        // The binary layout: its script class's (with the script's fields), else ScriptableObject's.
        engine::ObjectLayout ObjectLayout();
        void PrepareBinaryRead(const serialization::SerializedFile& file, u32 index, serialization::ReferenceTable* refs);

    private:
        friend struct ScriptableAccess;
        void AttachScriptAsset();

        PPtr<MonoScript> scriptAsset_;
        const ScriptClass* script_ = nullptr;
        bool awoken_ = false;
        bool enabled_ = false;
    };
}
