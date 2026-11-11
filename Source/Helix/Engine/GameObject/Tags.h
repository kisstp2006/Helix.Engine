#pragma once

#include <Helix/Core/Strings/String.h>

// Tags and layers as the tag manager keeps them: the built in tags first, tag 0 Untagged;
// 32 layers, the built in ones named.
namespace helix::engine
{
    struct TagHandle
    {
        u16 index = 0;
        bool operator==(const TagHandle&) const = default;
    };

    inline constexpr u32 kLayerCount = 32;

    // The handle of a defined tag; false (and the handle of Untagged) when the tag does not exist.
    bool GetExistingTag(StringView name, TagHandle& out);
    // Adds a tag (or finds it); false when the name is empty or the table is full.
    bool AddTag(StringView name, TagHandle& out);
    StringView TagName(TagHandle tag);
    u32 TagCount();

    StringView LayerName(u32 layer);
    void SetLayerName(u32 layer, StringView name);
    // -1 when no layer has the name.
    i32 NameToLayer(StringView name);
}
