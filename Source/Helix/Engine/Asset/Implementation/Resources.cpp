#include <Helix/Engine/Asset/Resources.h>

namespace helix::engine
{
    Object* Resources::Load(StringView path) { return Load(path, kNoClass); }

    Object* Resources::Load(StringView path, u32 klass)
    {
        const AssetEntry* e = FindResource(path);
        if (!e)
            return nullptr;
        return klass == kNoClass ? LoadAsset(e->guid) : LoadAssetOfClass(e->guid, klass);
    }

    void Resources::LoadAll(StringView path, Array<Object*>& out) { LoadAll(path, kNoClass, out); }

    void Resources::LoadAll(StringView path, u32 klass, Array<Object*>& out)
    {
        out.Clear();
        Array<const AssetEntry*> entries;
        if (const AssetEntry* e = FindResource(path))
            entries.Push(e);
        else
            FindResources(path, entries);
        // Guids first: loading may list the catalog again.
        Array<Guid> guids;
        for (const AssetEntry* e : entries)
            guids.Push(e->guid);
        Array<Object*> objects;
        for (const Guid& g : guids)
        {
            LoadAllAssets(g, objects);
            for (Object* o : objects)
                if (klass == kNoClass || IsDerivedFrom(o->ClassIndex(), klass))
                    out.Push(o);
        }
    }

    ResourceRequest Resources::LoadAsync(StringView path, u32 klass)
    {
        const AssetEntry* e = FindResource(path);
        return LoadAssetAsync(e ? e->guid : Guid(), klass);
    }
}
