#pragma once

#include <Helix/Engine/Material/PropertySheet.h>
#include <Helix/Engine/Object/Object.h>
#include <Helix/Engine/Texture/Texture.h>
#include <Helix/ShaderCompiler/Cache/ShaderCache.h>
#include <Helix/ShaderCompiler/Definition/ShaderDefinition.h>
#include <Helix/ShaderCompiler/Material/MaterialLayout.h>
#include <Helix/ShaderCompiler/Reflection/Reflection.h>

#include <memory>

// Shader: one shader definition of a C# shader assembly (Helix.ShaderCompiler reads it), with its properties, tags
// and passes. Variants compile on demand per pass, keyword values and target through one ShaderCache; a variant that
// does not compile logs its errors (with file:line) once and the error shader's variant takes its place.
namespace helix::engine
{
    enum class ShaderPropertyType : i32
    {
        Color = 0,
        Vector = 1,
        Float = 2,
        Range = 3,
        Texture = 4,
        Int = 5,
    };

    enum class ShaderPropertyFlags : i32
    {
        None = 0,
        HideInInspector = 1,
        PerRendererData = 2,
        NoScaleOffset = 4,
        Normal = 8,
        HDR = 16,
        Gamma = 32,
        NonModifiableTextureData = 64,
        MainTexture = 128,
        MainColor = 256,
    };

    // A C# shader assembly with what it references and its debug information, shared by the Shaders made from it.
    struct ShaderProgram;

    // One compiled variant of a pass for one target.
    struct ShaderVariant
    {
        bool ok = false;
        // What went wrong (file:line first when the assembly has debug information).
        String error;
        u32 pass = 0;
        // The shader the variant belongs to (the error shader's for variants that take its place).
        const shadercompiler::ShaderDescription* description = nullptr;
        shadercompiler::ShaderTarget target = shadercompiler::ShaderTarget::Glsl330;
        shadercompiler::CompiledShader vertex, fragment;
        shadercompiler::PassReflection reflection;
        // Where the material properties go in this variant.
        shadercompiler::MaterialLayout layout;
        const shadercompiler::RenderState* state = nullptr;
        // The keyword values it was compiled with (the keywords the code read).
        Array<shadercompiler::KeywordValue> keywords = Array<shadercompiler::KeywordValue>();
        // Made once for materials: the property id of every layout binding, and the members of material blocks that
        // are no properties (arrays, matrices set by name) with the id and element they take.
        Array<i32> bindingIds = Array<i32>();
        struct NamedMember
        {
            u32 block;
            const shadercompiler::ReflectedMember* member;
            i32 id;
            u32 element;
        };
        Array<NamedMember> namedMembers = Array<NamedMember>();
    };

    class Shader : public Object
    {
        HX_ENGINE_CLASS(Shader, Object, ClassId::Shader, ClassNone)

    public:
        Shader() = default;
        Shader(const Shader&) = default;

        // A loaded shader by name; null when none is.
        static Shader* Find(StringView name);
        static i32 PropertyToID(StringView name) { return engine::PropertyToID(name); }

        bool isSupported() const;
        i32 maximumLOD() const { return maximumLOD_; }
        void SetMaximumLOD(i32 value) { maximumLOD_ = value; }
        static i32 globalMaximumLOD();
        static void SetGlobalMaximumLOD(i32 value);
        // From the Queue tag (Background 1000, Geometry 2000, AlphaTest 2450, Transparent 3000, Overlay 4000, "+n"),
        // 2000 without one.
        i32 renderQueue() const;
        // Passes of the first SubShader.
        i32 passCount() const;
        i32 subshaderCount() const;
        i32 GetPassCountInSubshader(i32 subshader) const;
        // The pass of the first SubShader at `pass`, as an index into the definition's passes; -1 when there is none.
        i32 PassIndex(i32 pass) const;
        StringView FindPassTagValue(i32 pass, StringView tag) const;
        StringView FindSubshaderTagValue(i32 subshader, StringView tag) const;

        i32 GetPropertyCount() const;
        i32 FindPropertyIndex(StringView name) const;
        StringView GetPropertyName(i32 index) const;
        i32 GetPropertyNameId(i32 index) const;
        StringView GetPropertyDescription(i32 index) const;
        ShaderPropertyType GetPropertyType(i32 index) const;
        // ShaderPropertyFlags combined.
        i32 GetPropertyFlags(i32 index) const;
        f32 GetPropertyDefaultFloatValue(i32 index) const;
        i32 GetPropertyDefaultIntValue(i32 index) const;
        Vector4 GetPropertyDefaultVectorValue(i32 index) const;
        Vector2 GetPropertyRangeLimits(i32 index) const;
        TextureDimension GetPropertyTextureDimension(i32 index) const;
        StringView GetPropertyTextureDefaultName(i32 index) const;
        // The local keywords (the keyword space): bool keywords by their name, enum keywords as name_member.
        void GetLocalKeywords(Array<String>& out) const;

        // ---------------------------------------------------------------------------------------------- globals
        static void EnableKeyword(StringView keyword);
        static void DisableKeyword(StringView keyword);
        static void SetKeyword(StringView keyword, bool enabled) { enabled ? EnableKeyword(keyword) : DisableKeyword(keyword); }
        static bool IsKeywordEnabled(StringView keyword);
        static void GetEnabledGlobalKeywords(Array<String>& out);
        // Bumped whenever a global keyword changes.
        static u32 GlobalKeywordsVersion();
        // Global property values (SetGlobalFloat, ...): the renderer reads them for properties no material sets.
        static PropertySheet& Globals();

        // ---------------------------------------------------------------------------------------------- native
        // Null for a shader with no definition (made by code or read from a file without its assembly).
        const shadercompiler::ShaderDescription* Description() const;
        // The variant of a pass (an index into the definition's passes) for keyword values and a target; the error
        // shader's when it does not compile, null only when that does not compile either.
        const ShaderVariant* GetVariant(u32 pass, Span<const shadercompiler::KeywordValue> keywords, shadercompiler::ShaderTarget target);
        // Every material keyword name (bool and enum member) to keyword values, global keywords added.
        void KeywordValues(Span<const String> enabled, Array<shadercompiler::KeywordValue>& out) const;
        bool IsLocalKeyword(StringView keyword) const;

        // The shader of definition `index` of a program (the asset loader).
        static Shader* Create(std::shared_ptr<ShaderProgram> program, u32 index);
        // Forgets the compiled variants (they compile again on use, through the cache).
        void ClearVariants();
        // Hidden/InternalErrorShader, built in.
        static Shader* ErrorShader();
        // Where compiled shaders are kept between runs (an existing directory); empty for memory only.
        static void SetShaderCacheDirectory(StringView directory);
        static shadercompiler::ShaderCache& Cache();

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("m_Name", name_);
        }

    private:
        std::shared_ptr<ShaderProgram> program_;
        u32 index_ = 0;
        i32 maximumLOD_ = -1;
    };
}
