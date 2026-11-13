#include <Helix/Engine/Shader/ShaderKeywords.h>

#include <Helix/Core/Containers/HashMap.h>
#include <Helix/Core/Logging/Log.h>
#include <Helix/Engine/Material/PropertySheet.h>
#include <Helix/Engine/Rendering/Graphics.h>
#include <Helix/Engine/Shader/Shader.h>

namespace helix::engine
{
    namespace
    {
        LogChannel gKeywordLog{"Engine"};

        struct GlobalKeywords
        {
            Array<String> names;
            HashMap<u64, u32> byHash;
        };

        GlobalKeywords& Globals()
        {
            static GlobalKeywords g;
            return g;
        }

        u64 NameHash(StringView name)
        {
            u64 h = 1469598103934665603ull;
            for (char c : name)
                h = (h ^ u8(c)) * 1099511628211ull;
            return h;
        }

        const Shader* ShaderOf(u64 id) { return EntityIdToObject<Shader>(EntityId::FromULong(id)); }

        const ShaderKeywordInfo* InfoOf(u64 shader, u32 index)
        {
            const Shader* s = ShaderOf(shader);
            if (!s)
                return nullptr;
            const Array<ShaderKeywordInfo>& space = s->KeywordSpace();
            return index < space.Size() ? &space[index] : nullptr;
        }
    }

    u32 GlobalKeywordIndex(StringView name)
    {
        GlobalKeywords& g = Globals();
        u64 h = NameHash(name);
        // Collisions are told apart by the name.
        for (u64 key = h;; key++)
        {
            const u32* at = g.byHash.Find(key);
            if (!at)
            {
                g.byHash.Set(key, u32(g.names.Size()));
                g.names.Push(String(name));
                return u32(g.names.Size() - 1);
            }
            if (g.names[*at] == name)
                return *at;
        }
    }

    StringView GlobalKeywordName(u32 index)
    {
        const GlobalKeywords& g = Globals();
        return index < g.names.Size() ? StringView(g.names[index]) : StringView();
    }

    u32 GlobalKeywordCount() { return u32(Globals().names.Size()); }

    LocalKeyword::LocalKeyword(const Shader* s, StringView name)
    {
        shader = s ? s->GetEntityId().value : 0;
        if (s)
        {
            const Array<ShaderKeywordInfo>& space = s->KeywordSpace();
            for (u32 i = 0; i < space.Size(); i++)
                if (space[i].name == name)
                {
                    index = i;
                    return;
                }
        }
        HX_LOG_ERROR(gKeywordLog, "Local keyword {} doesn't exist in the shader {}.", name, s ? s->Name() : StringView("null"));
    }

    String LocalKeyword::name() const
    {
        const ShaderKeywordInfo* k = InfoOf(shader, index);
        return k ? k->name : String();
    }

    bool LocalKeyword::isValid() const { return InfoOf(shader, index) != nullptr; }

    bool LocalKeyword::isDynamic() const
    {
        const ShaderKeywordInfo* k = InfoOf(shader, index);
        return k && k->dynamic;
    }

    bool LocalKeyword::isOverridable() const
    {
        const ShaderKeywordInfo* k = InfoOf(shader, index);
        return k && k->overridable;
    }

    GlobalKeyword::GlobalKeyword(StringView name) : index(GlobalKeywordIndex(name)) {}

    String GlobalKeyword::name() const { return String(GlobalKeywordName(index)); }

    u32 LocalKeywordSpace::keywordCount() const
    {
        const Shader* s = ShaderOf(shader);
        return s ? u32(s->KeywordSpace().Size()) : 0u;
    }

    Array<String> LocalKeywordSpace::keywordNames() const
    {
        Array<String> out;
        if (const Shader* s = ShaderOf(shader))
            for (const ShaderKeywordInfo& k : s->KeywordSpace())
                out.Push(k.name);
        return out;
    }

    Array<LocalKeyword> LocalKeywordSpace::keywords() const
    {
        Array<LocalKeyword> out;
        for (u32 i = 0; i < keywordCount(); i++)
        {
            LocalKeyword k;
            k.shader = shader;
            k.index = i;
            out.Push(k);
        }
        return out;
    }

    LocalKeyword LocalKeywordSpace::FindKeyword(StringView name) const
    {
        LocalKeyword k;
        k.shader = shader;
        if (const Shader* s = ShaderOf(shader))
        {
            const Array<ShaderKeywordInfo>& space = s->KeywordSpace();
            for (u32 i = 0; i < space.Size(); i++)
                if (space[i].name == name)
                    k.index = i;
        }
        return k;
    }

    // Tag names share the property ids (one id per name for the run).
    ShaderTagId::ShaderTagId(StringView name) : id(name.Empty() ? 0 : PropertyToID(name)) {}

    String ShaderTagId::name() const { return String(PropertyName(id)); }

    LocalKeyword KeywordNative::Find(Shader* shader, StringView name) { return LocalKeyword(shader, name); }

    LocalKeyword KeywordNative::SpaceAt(LocalKeywordSpace space, u32 index)
    {
        LocalKeyword k;
        k.shader = space.shader;
        k.index = index < space.keywordCount() ? index : ~0u;
        return k;
    }
}
