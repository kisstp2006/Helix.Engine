#pragma once

#include <Helix/Core/Basics/Types.h>
#include <Helix/Core/Containers/Array.h>
#include <Helix/Core/Strings/String.h>
#include <Helix/Scripting/Bindings/Tags.h>

// Shader keywords as the reference's value types. A local keyword is an index into the keyword space of a shader (its
// keywords, bool ones by their name and enum ones as name_member, declared local or global); a global keyword is an
// index into the global keyword list (the keywords shaders declare global, the ones Create or EnableKeyword names). The
// structs hold ids, not names, so they cross to C# as plain values.
namespace helix::engine
{
    class Shader;

    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class ShaderKeywordType : i32
    {
        None = 0,
        BuiltinDefault = 2,
        BuiltinExtra = 6,
        BuiltinAutoStripped = 10,
        UserDefined = 16,
        Plugin = 32,
    };

    // One keyword of a shader's space.
    struct ShaderKeywordInfo
    {
        String name;
        // A uniform bool of the variant (Dynamic = true), not a variant of its own.
        bool dynamic = false;
        // Declared global: a global keyword of the same name overrides it.
        bool overridable = false;
    };

    HX_STRUCT(Namespace = "HelixEngine.Rendering")
    struct LocalKeyword
    {
        // The shader whose space it is in (an object id) and its index there (~0 when the name is not in it).
        HX_FIELD(Private, Name = "m_Shader") u64 shader = 0;
        HX_FIELD(Private, Name = "m_Index") u32 index = ~0u;
        HX_FIELD(Private, Name = "m_Padding") u32 padding = 0;

        LocalKeyword() = default;
        // The keyword of that name in the shader's space; an error is logged when there is none (the struct is made,
        // invalid).
        LocalKeyword(const Shader* shader, StringView name);

        String name() const;
        bool isValid() const;
        bool isDynamic() const;
        bool isOverridable() const;
        ShaderKeywordType type() const { return isValid() ? ShaderKeywordType::UserDefined : ShaderKeywordType::None; }
        bool operator==(const LocalKeyword& o) const { return shader == o.shader && index == o.index; }
    };

    HX_STRUCT(Namespace = "HelixEngine.Rendering")
    struct GlobalKeyword
    {
        HX_FIELD(Private, Name = "m_Index") u32 index = ~0u;

        GlobalKeyword() = default;
        // The global keyword of that name (added to the list when it is not there yet).
        explicit GlobalKeyword(StringView name);
        static GlobalKeyword Create(StringView name) { return GlobalKeyword(name); }
        String name() const;
        bool operator==(const GlobalKeyword& o) const { return index == o.index; }
    };

    HX_STRUCT(Namespace = "HelixEngine.Rendering")
    struct LocalKeywordSpace
    {
        HX_FIELD(Private, Name = "m_Shader") u64 shader = 0;

        u32 keywordCount() const;
        Array<String> keywordNames() const;
        Array<LocalKeyword> keywords() const;
        // Invalid (isValid false) when the name is not in the space.
        LocalKeyword FindKeyword(StringView name) const;
        bool operator==(const LocalKeywordSpace& o) const { return shader == o.shader; }
    };

    // The reference's ShaderTagId: a tag name or value by id (none is 0).
    HX_STRUCT(Namespace = "HelixEngine.Rendering")
    struct ShaderTagId
    {
        HX_FIELD(Private, Name = "m_Id") i32 id = 0;

        ShaderTagId() = default;
        explicit ShaderTagId(StringView name);
        String name() const;
        bool operator==(const ShaderTagId& o) const { return id == o.id; }
    };

    // The global keyword list.
    u32 GlobalKeywordIndex(StringView name);
    StringView GlobalKeywordName(u32 index);
    u32 GlobalKeywordCount();

    // What the C# side's hand written members of these structs call (internal in C#).
    HX_CLASS(Namespace = "HelixEngine.Internal", Static, Internal)
    class KeywordNative
    {
    public:
        HX_FUNCTION() static LocalKeyword Find(Shader* shader, StringView name);
        HX_FUNCTION() static String Name(LocalKeyword keyword) { return keyword.name(); }
        HX_FUNCTION() static bool IsValid(LocalKeyword keyword) { return keyword.isValid(); }
        HX_FUNCTION() static bool IsDynamic(LocalKeyword keyword) { return keyword.isDynamic(); }
        HX_FUNCTION() static bool IsOverridable(LocalKeyword keyword) { return keyword.isOverridable(); }
        HX_FUNCTION() static u32 SpaceCount(LocalKeywordSpace space) { return space.keywordCount(); }
        HX_FUNCTION() static LocalKeyword SpaceAt(LocalKeywordSpace space, u32 index);
        HX_FUNCTION() static LocalKeyword SpaceFind(LocalKeywordSpace space, StringView name) { return space.FindKeyword(name); }
        HX_FUNCTION() static GlobalKeyword Global(StringView name) { return GlobalKeyword(name); }
        HX_FUNCTION() static String GlobalName(GlobalKeyword keyword) { return keyword.name(); }
        HX_FUNCTION() static ShaderTagId Tag(StringView name) { return ShaderTagId(name); }
        HX_FUNCTION() static String TagName(ShaderTagId tag) { return tag.name(); }
    };
}
