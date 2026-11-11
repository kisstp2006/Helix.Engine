#include <Helix/Engine/Asset/Asset.h>
#include <Helix/Engine/GameObject/Implementation/CloneAccess.h>
#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Engine/Prefab/Prefab.h>
#include <Helix/Engine/Scene/Implementation/SceneAccess.h>

namespace helix::engine
{
    namespace
    {
        struct Registry
        {
            HashMap<Guid, EntityId> roots;
            PrefabSource* source = nullptr;
        };

        Registry& Prefabs()
        {
            static Registry r;
            return r;
        }
    }

    GameObject* LoadPrefab(Span<const u8> bytes, Array<String>& errors, const Guid& guid)
    {
        const SceneFormat* format = FindSceneFormat(bytes);
        if (!format)
        {
            errors.Push("no scene format reads the bytes");
            return nullptr;
        }
        String error;
        ParsedScene* parsed = format->Parse(bytes, error);
        if (!parsed)
        {
            errors.Push(error);
            return nullptr;
        }
        SceneBuildOptions options;
        options.persistent = true;
        options.enable = false;
        Array<EntityId> roots;
        SceneAccess::Build(*format, *parsed, kAssetSlot, errors, options, &roots);
        delete parsed;
        if (roots.Empty())
        {
            errors.Push("a prefab file has a root GameObject");
            return nullptr;
        }
        if (roots.Size() > 1)
        {
            errors.Push(Format("a prefab file has one root GameObject, this one has {}; the others are removed", roots.Size()));
            for (usize i = 1; i < roots.Size(); i++)
                if (GameObject* g = EntityIdToObject<GameObject>(roots[i]))
                    DestroyImmediate(g, true);
        }
        GameObject* root = EntityIdToObject<GameObject>(roots[0]);
        if (root && guid.IsValid())
            RegisterPrefab(guid, root);
        return root;
    }

    void UnloadPrefab(GameObject* root)
    {
        if (!root)
            return;
        Registry& r = Prefabs();
        Array<Guid> gone;
        for (auto e : r.roots)
            if (e.value == root->GetEntityId())
                gone.Push(e.key);
        for (const Guid& g : gone)
            r.roots.Remove(g);
        CloneAccess::ForgetTemplate(root->GetEntityId());
        DestroyImmediate(root, true);
    }

    GameObject* FindPrefab(const Guid& guid)
    {
        Registry& r = Prefabs();
        if (const EntityId* id = r.roots.Find(guid))
            if (GameObject* g = EntityIdToObject<GameObject>(*id))
                return g;
        if (r.source)
            return r.source->Find(guid);
        // The asset runtime: prefab and model files of the catalog.
        Object* o = LoadAsset(guid);
        return o && o->Is<GameObject>() ? static_cast<GameObject*>(o) : nullptr;
    }

    void RegisterPrefab(const Guid& guid, GameObject* root)
    {
        if (root && guid.IsValid())
            Prefabs().roots.Set(guid, root->GetEntityId());
    }

    void SetPrefabSource(PrefabSource* source) { Prefabs().source = source; }

    void InvalidatePrefabTemplate(GameObject* root)
    {
        if (root)
            CloneAccess::ForgetTemplate(root->GetEntityId());
    }
}
