#pragma once

#include <Helix/Engine/Material/PropertySheet.h>
#include <Helix/Engine/Object/Object.h>
#include <Helix/Engine/Shader/ShaderKeywords.h>
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
    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class ShaderPropertyType : i32
    {
        Color = 0,
        Vector = 1,
        Float = 2,
        Range = 3,
        Texture = 4,
        Int = 5,
    };

    HX_ENUM(Namespace = "HelixEngine.Rendering", Attributes = "[System.Flags]")
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
        // Unique for the run of the program (GPU caches key on it; addresses may come back).
        u32 serial = 0;
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

    HX_CLASS(Namespace = "HelixEngine", Sealed)
    class Shader : public Object
    {
        HX_ENGINE_CLASS(Shader, Object, ClassId::Shader, ClassNone)

    public:
        Shader() = default;
        Shader(const Shader&) = default;

        // A loaded shader by name; null when none is.
        HX_FUNCTION() static Shader* Find(StringView name);
        HX_FUNCTION() static i32 PropertyToID(StringView name) { return engine::PropertyToID(name); }

        HX_PROPERTY(ReadOnly) bool isSupported() const;
        HX_PROPERTY() i32 maximumLOD() const { return maximumLOD_; }
        void SetMaximumLOD(i32 value) { maximumLOD_ = value; }
        HX_PROPERTY() static i32 globalMaximumLOD();
        static void SetGlobalMaximumLOD(i32 value);
        // From the Queue tag (Background 1000, Geometry 2000, AlphaTest 2450, Transparent 3000, Overlay 4000, "+n"),
        // 2000 without one.
        HX_PROPERTY(ReadOnly) i32 renderQueue() const;
        // Passes of the first SubShader.
        HX_PROPERTY(ReadOnly) i32 passCount() const;
        HX_PROPERTY(ReadOnly) i32 subshaderCount() const;
        HX_FUNCTION() i32 GetPassCountInSubshader(i32 subshaderIndex) const;
        // The pass of the first SubShader at `pass`, as an index into the definition's passes; -1 when there is none.
        i32 PassIndex(i32 pass) const;
        StringView FindPassTagValue(i32 pass, StringView tag) const;
        StringView FindSubshaderTagValue(i32 subshader, StringView tag) const;
        // The same with tag ids: a pass of the first SubShader, a pass of a SubShader; none (0) when it is not there.
        HX_FUNCTION(Name = "FindPassTagValue") ShaderTagId FindPassTagValueId(i32 passIndex, ShaderTagId tagName) const;
        HX_FUNCTION(Name = "FindPassTagValue") ShaderTagId FindPassTagValueId(i32 subshaderIndex, i32 passIndex, ShaderTagId tagName) const;
        HX_FUNCTION(Name = "FindSubshaderTagValue") ShaderTagId FindSubshaderTagValueId(i32 subshaderIndex, ShaderTagId tagName) const;

        HX_FUNCTION() i32 GetPropertyCount() const;
        HX_FUNCTION() i32 FindPropertyIndex(StringView propertyName) const;
        HX_FUNCTION() StringView GetPropertyName(i32 propertyIndex) const;
        HX_FUNCTION() i32 GetPropertyNameId(i32 propertyIndex) const;
        HX_FUNCTION() StringView GetPropertyDescription(i32 propertyIndex) const;
        HX_FUNCTION() ShaderPropertyType GetPropertyType(i32 propertyIndex) const;
        HX_FUNCTION() ShaderPropertyFlags GetPropertyFlags(i32 propertyIndex) const;
        HX_FUNCTION() f32 GetPropertyDefaultFloatValue(i32 propertyIndex) const;
        HX_FUNCTION() i32 GetPropertyDefaultIntValue(i32 propertyIndex) const;
        HX_FUNCTION() Vector4 GetPropertyDefaultVectorValue(i32 propertyIndex) const;
        HX_FUNCTION() Vector2 GetPropertyRangeLimits(i32 propertyIndex) const;
        HX_FUNCTION() TextureDimension GetPropertyTextureDimension(i32 propertyIndex) const;
        HX_FUNCTION() StringView GetPropertyTextureDefaultName(i32 propertyIndex) const;
        // The local keywords (declared local): bool keywords by their name, enum keywords as name_member.
        void GetLocalKeywords(Array<String>& out) const;
        // The keyword space: every keyword of the shader, local and global, in declaration order.
        const Array<ShaderKeywordInfo>& KeywordSpace() const;
        HX_PROPERTY(ReadOnly) LocalKeywordSpace keywordSpace() const { return LocalKeywordSpace{GetEntityId().value}; }

        // ---------------------------------------------------------------------------------------------- globals
        HX_FUNCTION() static void EnableKeyword(StringView keyword);
        HX_FUNCTION() static void DisableKeyword(StringView keyword);
        static void SetKeyword(StringView keyword, bool enabled) { enabled ? EnableKeyword(keyword) : DisableKeyword(keyword); }
        HX_FUNCTION() static bool IsKeywordEnabled(StringView keyword);
        HX_FUNCTION() static void EnableKeyword(const GlobalKeyword& keyword) { EnableKeyword(keyword.name()); }
        HX_FUNCTION() static void DisableKeyword(const GlobalKeyword& keyword) { DisableKeyword(keyword.name()); }
        HX_FUNCTION() static bool IsKeywordEnabled(const GlobalKeyword& keyword) { return IsKeywordEnabled(keyword.name()); }
        HX_FUNCTION() static void SetKeyword(const GlobalKeyword& keyword, bool value) { SetKeyword(keyword.name(), value); }
        static void GetEnabledGlobalKeywords(Array<String>& out);
        HX_PROPERTY(ReadOnly) static Array<GlobalKeyword> enabledGlobalKeywords();
        HX_PROPERTY(ReadOnly) static Array<GlobalKeyword> globalKeywords();
        // Compiles the variants of every loaded shader's passes with no keywords for the current target.
        HX_FUNCTION() static void WarmupAllShaders();

        HX_FUNCTION() static void SetGlobalFloat(StringView name, f32 value) { SetGlobalFloat(PropertyToID(name), value); }
        HX_FUNCTION() static void SetGlobalFloat(i32 nameID, f32 value) { Globals().SetFloat(nameID, value); }
        HX_FUNCTION() static void SetGlobalInt(StringView name, i32 value) { SetGlobalFloat(name, f32(value)); }
        HX_FUNCTION() static void SetGlobalInt(i32 nameID, i32 value) { SetGlobalFloat(nameID, f32(value)); }
        HX_FUNCTION() static void SetGlobalInteger(StringView name, i32 value) { SetGlobalInteger(PropertyToID(name), value); }
        HX_FUNCTION() static void SetGlobalInteger(i32 nameID, i32 value) { Globals().SetInt(nameID, value); }
        HX_FUNCTION() static void SetGlobalVector(StringView name, Vector4 value) { SetGlobalVector(PropertyToID(name), value); }
        HX_FUNCTION() static void SetGlobalVector(i32 nameID, Vector4 value) { Globals().SetVector(nameID, value); }
        HX_FUNCTION() static void SetGlobalColor(StringView name, Color value) { SetGlobalColor(PropertyToID(name), value); }
        HX_FUNCTION() static void SetGlobalColor(i32 nameID, Color value) { Globals().SetColor(nameID, value); }
        HX_FUNCTION() static void SetGlobalMatrix(StringView name, Matrix4x4 value) { SetGlobalMatrix(PropertyToID(name), value); }
        HX_FUNCTION() static void SetGlobalMatrix(i32 nameID, Matrix4x4 value) { Globals().SetMatrix(nameID, value); }
        HX_FUNCTION() static void SetGlobalTexture(StringView name, Texture* value) { SetGlobalTexture(PropertyToID(name), value); }
        HX_FUNCTION() static void SetGlobalTexture(i32 nameID, Texture* value) { Globals().SetTexture(nameID, value ? value->GetEntityId() : EntityId()); }
        HX_FUNCTION() static void SetGlobalFloatArray(StringView name, Span<const f32> values) { SetGlobalFloatArray(PropertyToID(name), values); }
        HX_FUNCTION() static void SetGlobalFloatArray(i32 nameID, Span<const f32> values) { Globals().SetFloatArray(nameID, values); }
        HX_FUNCTION() static void SetGlobalVectorArray(StringView name, Span<const Vector4> values) { SetGlobalVectorArray(PropertyToID(name), values); }
        HX_FUNCTION() static void SetGlobalVectorArray(i32 nameID, Span<const Vector4> values) { Globals().SetVectorArray(nameID, values); }
        HX_FUNCTION() static void SetGlobalMatrixArray(StringView name, Span<const Matrix4x4> values) { SetGlobalMatrixArray(PropertyToID(name), values); }
        HX_FUNCTION() static void SetGlobalMatrixArray(i32 nameID, Span<const Matrix4x4> values) { Globals().SetMatrixArray(nameID, values); }
        HX_FUNCTION() static f32 GetGlobalFloat(StringView name) { return GetGlobalFloat(PropertyToID(name)); }
        HX_FUNCTION() static f32 GetGlobalFloat(i32 nameID) { return Globals().GetFloat(nameID); }
        HX_FUNCTION() static i32 GetGlobalInt(StringView name) { return i32(GetGlobalFloat(name)); }
        HX_FUNCTION() static i32 GetGlobalInt(i32 nameID) { return i32(GetGlobalFloat(nameID)); }
        HX_FUNCTION() static i32 GetGlobalInteger(StringView name) { return GetGlobalInteger(PropertyToID(name)); }
        HX_FUNCTION() static i32 GetGlobalInteger(i32 nameID) { return Globals().GetInt(nameID); }
        HX_FUNCTION() static Vector4 GetGlobalVector(StringView name) { return GetGlobalVector(PropertyToID(name)); }
        HX_FUNCTION() static Vector4 GetGlobalVector(i32 nameID) { return Globals().GetVector(nameID); }
        HX_FUNCTION() static Color GetGlobalColor(StringView name) { return GetGlobalColor(PropertyToID(name)); }
        HX_FUNCTION() static Color GetGlobalColor(i32 nameID) { return Globals().GetColor(nameID); }
        HX_FUNCTION() static Matrix4x4 GetGlobalMatrix(StringView name) { return GetGlobalMatrix(PropertyToID(name)); }
        HX_FUNCTION() static Matrix4x4 GetGlobalMatrix(i32 nameID) { return Globals().GetMatrix(nameID); }
        HX_FUNCTION() static Texture* GetGlobalTexture(StringView name) { return GetGlobalTexture(PropertyToID(name)); }
        HX_FUNCTION() static Texture* GetGlobalTexture(i32 nameID) { return EntityIdToObject<Texture>(Globals().GetTexture(nameID)); }
        HX_FUNCTION() static Array<f32> GetGlobalFloatArray(StringView name) { return GetGlobalFloatArray(PropertyToID(name)); }
        HX_FUNCTION() static Array<f32> GetGlobalFloatArray(i32 nameID)
        {
            Array<f32> out;
            Globals().GetFloatArray(nameID, out);
            return out;
        }
        HX_FUNCTION() static Array<Vector4> GetGlobalVectorArray(StringView name) { return GetGlobalVectorArray(PropertyToID(name)); }
        HX_FUNCTION() static Array<Vector4> GetGlobalVectorArray(i32 nameID)
        {
            Array<Vector4> out;
            Globals().GetVectorArray(nameID, out);
            return out;
        }
        HX_FUNCTION() static Array<Matrix4x4> GetGlobalMatrixArray(StringView name) { return GetGlobalMatrixArray(PropertyToID(name)); }
        HX_FUNCTION() static Array<Matrix4x4> GetGlobalMatrixArray(i32 nameID)
        {
            Array<Matrix4x4> out;
            Globals().GetMatrixArray(nameID, out);
            return out;
        }
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
        // Made on first use from the definition.
        mutable Array<ShaderKeywordInfo> keywordSpace_ = Array<ShaderKeywordInfo>();
        mutable bool keywordSpaceMade_ = false;
    };
}
