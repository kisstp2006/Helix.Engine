#pragma once

#include <Helix/Core/Types/Guid.h>
#include <Helix/Engine/GameObject/GameObject.h>

// Prefab assets: a hierarchy of GameObjects outside every scene, loaded from a prefab file (YAML or binary, through the
// scene formats). Their objects are assets (persistent, never enabled); Instantiate copies them into a scene. Scene and
// prefab files refer to prefabs by guid (PrefabInstance documents), resolved at load through a PrefabSource.
namespace helix::engine
{
    // The fileID of the prefab asset itself in references to it (m_SourcePrefab).
    inline constexpr i64 kPrefabAssetFileId = 100100000;

    // Where prefabs come from by guid when they are not registered (extensible). Without one, the asset runtime loads
    // the prefab or model file of the guid.
    class PrefabSource
    {
    public:
        virtual ~PrefabSource() = default;
        // The root of the loaded prefab, null when there is none.
        virtual GameObject* Find(const Guid& guid) = 0;
    };

    // Loads a prefab file; null with `errors` when it holds no root GameObject. With a guid it is registered for the
    // instances in files loaded later (nested prefabs and variants load their source first).
    GameObject* LoadPrefab(Span<const u8> bytes, Array<String>& errors, const Guid& guid = Guid());
    inline GameObject* LoadPrefab(StringView text, Array<String>& errors, const Guid& guid = Guid())
    {
        return LoadPrefab(Span<const u8>(reinterpret_cast<const u8*>(text.Data()), text.Size()), errors, guid);
    }
    // Destroys a loaded prefab and forgets its guid.
    void UnloadPrefab(GameObject* root);
    // The prefab of a guid: the registered ones first, then the source.
    GameObject* FindPrefab(const Guid& guid);
    void RegisterPrefab(const Guid& guid, GameObject* root);
    // Null for the asset runtime.
    void SetPrefabSource(PrefabSource* source);
    // For tools that change a prefab asset: the copies made after this see the change.
    void InvalidatePrefabTemplate(GameObject* root);
}
