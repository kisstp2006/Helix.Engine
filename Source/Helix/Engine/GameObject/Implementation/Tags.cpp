#include <Helix/Engine/GameObject/Tags.h>

#include <Helix/Core/Containers/Array.h>

namespace helix::engine
{
    namespace
    {
        struct TagTable
        {
            Array<String> tags;
            String layers[kLayerCount];

            TagTable()
            {
                for (const char* t : {"Untagged", "Respawn", "Finish", "EditorOnly", "MainCamera", "Player", "GameController"})
                    tags.Push(String(t));
                layers[0] = "Default", layers[1] = "TransparentFX", layers[2] = "Ignore Raycast", layers[4] = "Water", layers[5] = "UI";
            }
        };

        TagTable& Table()
        {
            static TagTable t;
            return t;
        }
    }

    bool GetExistingTag(StringView name, TagHandle& out)
    {
        const TagTable& t = Table();
        for (usize i = 0; i < t.tags.Size(); i++)
            if (t.tags[i] == name)
            {
                out.index = u16(i);
                return true;
            }
        out = TagHandle();
        return false;
    }

    bool AddTag(StringView name, TagHandle& out)
    {
        if (name.Empty())
            return false;
        if (GetExistingTag(name, out))
            return true;
        TagTable& t = Table();
        if (t.tags.Size() >= 0xFFFF)
            return false;
        out.index = u16(t.tags.Size());
        t.tags.Push(String(name));
        return true;
    }

    StringView TagName(TagHandle tag)
    {
        const TagTable& t = Table();
        return tag.index < t.tags.Size() ? StringView(t.tags[tag.index]) : StringView("Untagged");
    }

    u32 TagCount() { return u32(Table().tags.Size()); }

    StringView LayerName(u32 layer) { return layer < kLayerCount ? StringView(Table().layers[layer]) : StringView(); }

    void SetLayerName(u32 layer, StringView name)
    {
        if (layer < kLayerCount)
            Table().layers[layer] = name;
    }

    i32 NameToLayer(StringView name)
    {
        const TagTable& t = Table();
        for (u32 i = 0; i < kLayerCount; i++)
            if (!name.Empty() && t.layers[i] == name)
                return i32(i);
        return -1;
    }
}
