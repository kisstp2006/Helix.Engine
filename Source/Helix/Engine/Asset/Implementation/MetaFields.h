#pragma once

#include <Helix/Core/Basics/Span.h>
#include <Helix/Core/Strings/String.h>
#include <Helix/Serialization/Yaml/Tree.h>

namespace helix::engine
{
    // A number of the importer settings in a .meta file: the first "key: value" line after the line that starts the
    // importer's section ("TextureImporter:"); false when the file has none.
    inline bool MetaNumber(Span<const u8> meta, StringView section, StringView key, double& out)
    {
        StringView text(reinterpret_cast<const char*>(meta.Data()), meta.Size());
        usize at = text.Find(section);
        if (at == StringView::npos)
            return false;
        while (at < text.Size())
        {
            usize end = text.Find('\n', at);
            if (end == StringView::npos)
                end = text.Size();
            StringView line = text.Substr(at, end - at).Trimmed();
            at = end + 1;
            if (line.Size() > key.Size() && line.StartsWith(key) && line[key.Size()] == ':')
                return serialization::yaml::ParseFloat(line.Substr(key.Size() + 1).Trimmed(), out);
        }
        return false;
    }

    // The settings a loader reads from a .meta: each a key and where its value goes.
    struct MetaField
    {
        StringView key;
        void (*apply)(void* settings, double value);
    };

    inline void ReadMetaFields(Span<const u8> meta, StringView section, Span<const MetaField> fields, void* settings)
    {
        if (meta.Empty())
            return;
        for (const MetaField& f : fields)
        {
            double v;
            if (MetaNumber(meta, section, f.key, v))
                f.apply(settings, v);
        }
    }
}
