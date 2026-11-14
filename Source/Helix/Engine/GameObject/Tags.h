#pragma once

#include <Helix/Core/Basics/Tags.h>
#include <Helix/Core/Basics/Span.h>
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

    // A set of layers as a bit mask (bit n: layer n), as cameras, lights and volumes take them.
    class LayerMask
    {
    public:
        LayerMask() = default;
        LayerMask(i32 mask) : mask_(mask) {}
        operator i32() const { return mask_; }
        i32 value() const { return mask_; }
        void SetValue(i32 value) { mask_ = value; }
        // The mask of the named layers (names no layer has are left out).
        static i32 GetMask(Span<const StringView> layerNames);
        static StringView LayerToName(i32 layer) { return layer >= 0 && u32(layer) < kLayerCount ? LayerName(u32(layer)) : StringView(); }
        static i32 NameToLayer(StringView layerName) { return engine::NameToLayer(layerName); }

    private:
        i32 mask_ = 0;
    };

    // What C#'s LayerMask calls (internal).
    HX_CLASS(Namespace = "HelixEngine.Internal", Static, Internal)
    class LayersNative
    {
    public:
        HX_FUNCTION() static String LayerToName(i32 layer) { return String(LayerMask::LayerToName(layer)); }
        HX_FUNCTION() static i32 NameToLayer(StringView layerName) { return LayerMask::NameToLayer(layerName); }
    };
}
