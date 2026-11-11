#pragma once

#include <Helix/Engine/Shader/Shader.h>

// Material: a shader and the values of its properties (one PropertySheet), keywords, the render queue override, tags
// and passes switched off. For the renderer it keeps, per pass and variant, the bytes of the uniform blocks its
// properties go to and the texture of every slot; they are written again when a value (or a global) changed.
namespace helix::engine
{
    enum class MaterialGlobalIlluminationFlags : i32
    {
        None = 0,
        RealtimeEmissive = 1,
        BakedEmissive = 2,
        AnyEmissive = 3,
        EmissiveIsBlack = 4,
    };

    enum class MaterialPropertyType : i32
    {
        Float = 0,
        Int = 1,
        Vector = 2,
        Matrix = 3,
        Texture = 4,
        ConstantBuffer = 5,
        ComputeBuffer = 6,
    };

    // What a material gives the renderer for one pass and variant.
    struct MaterialPassData
    {
        const ShaderVariant* variant = nullptr;
        // What the variant was picked for: pass, target and the keyword state (shader, material and global keywords).
        u32 pass = 0;
        shadercompiler::ShaderTarget target = shadercompiler::ShaderTarget::Glsl330;
        u64 keywordStamp = 0;
        // One per block of the variant's MaterialLayout, at that block's slot.
        Array<Array<u8>> blocks = Array<Array<u8>>();
        struct TextureSlot
        {
            u32 slot;
            EntityId texture;
        };
        Array<TextureSlot> textures = Array<TextureSlot>();
        // The material and global versions the data was written for.
        u32 version = ~0u, globalsVersion = ~0u;
    };

    // Values on top of a material for one renderer: the same kinds, no render state.
    class MaterialPropertyBlock
    {
    public:
        bool isEmpty() const { return sheet_.Empty(); }
        void Clear() { sheet_.Clear(); }
        void SetFloat(StringView name, f32 value) { sheet_.SetFloat(PropertyToID(name), value); }
        void SetFloat(i32 id, f32 value) { sheet_.SetFloat(id, value); }
        void SetInteger(StringView name, i32 value) { sheet_.SetInt(PropertyToID(name), value); }
        void SetInteger(i32 id, i32 value) { sheet_.SetInt(id, value); }
        void SetColor(StringView name, Color value) { sheet_.SetColor(PropertyToID(name), value); }
        void SetColor(i32 id, Color value) { sheet_.SetColor(id, value); }
        void SetVector(StringView name, Vector4 value) { sheet_.SetVector(PropertyToID(name), value); }
        void SetVector(i32 id, Vector4 value) { sheet_.SetVector(id, value); }
        void SetMatrix(StringView name, const Matrix4x4& value) { sheet_.SetMatrix(PropertyToID(name), value); }
        void SetMatrix(i32 id, const Matrix4x4& value) { sheet_.SetMatrix(id, value); }
        void SetTexture(StringView name, Texture* value) { sheet_.SetTexture(PropertyToID(name), value ? value->GetEntityId() : EntityId()); }
        void SetTexture(i32 id, Texture* value) { sheet_.SetTexture(id, value ? value->GetEntityId() : EntityId()); }
        void SetFloatArray(StringView name, Span<const f32> values) { sheet_.SetFloatArray(PropertyToID(name), values); }
        void SetVectorArray(StringView name, Span<const Vector4> values) { sheet_.SetVectorArray(PropertyToID(name), values); }
        void SetMatrixArray(StringView name, Span<const Matrix4x4> values) { sheet_.SetMatrixArray(PropertyToID(name), values); }
        f32 GetFloat(StringView name) const { return sheet_.GetFloat(PropertyToID(name)); }
        i32 GetInteger(StringView name) const { return sheet_.GetInt(PropertyToID(name)); }
        Color GetColor(StringView name) const { return sheet_.GetColor(PropertyToID(name)); }
        Vector4 GetVector(StringView name) const { return sheet_.GetVector(PropertyToID(name)); }
        Matrix4x4 GetMatrix(StringView name) const { return sheet_.GetMatrix(PropertyToID(name)); }
        Texture* GetTexture(StringView name) const { return EntityIdToObject<Texture>(sheet_.GetTexture(PropertyToID(name))); }
        bool HasProperty(StringView name) const { return sheet_.Has(PropertyToID(name)); }
        bool HasFloat(StringView name) const { return sheet_.Has(PropertyToID(name), PropertyKind::Float); }
        bool HasInteger(StringView name) const { return sheet_.Has(PropertyToID(name), PropertyKind::Int); }
        bool HasVector(StringView name) const { return sheet_.Has(PropertyToID(name), PropertyKind::Vector); }
        bool HasColor(StringView name) const { return HasVector(name); }
        bool HasMatrix(StringView name) const { return sheet_.Has(PropertyToID(name), PropertyKind::Matrix); }
        bool HasTexture(StringView name) const { return sheet_.Has(PropertyToID(name), PropertyKind::Texture); }

        const PropertySheet& Sheet() const { return sheet_; }

    private:
        PropertySheet sheet_;
    };

    class Material : public Object
    {
        HX_ENGINE_CLASS(Material, Object, ClassId::Material, ClassNone)

    public:
        Material() = default;
        // The values, not the renderer's data.
        Material(const Material& other);
        ~Material() override;

        // new Material(shader): the shader's defaults; null with an error without a shader.
        static Material* Create(Shader* shader);
        // new Material(source): a copy.
        static Material* Create(const Material& source);

        Shader* shader() const { return shader_.Get(); }
        // Values of properties both shaders have stay; the new shader's other properties get their defaults.
        void SetShader(Shader* shader);

        // ---------------------------------------------------------------------------------------------- values
        void SetFloat(i32 id, f32 value) { Changed(), values_.SetFloat(id, value); }
        void SetFloat(StringView name, f32 value) { SetFloat(PropertyToID(name), value); }
        void SetInteger(i32 id, i32 value) { Changed(), values_.SetInt(id, value); }
        void SetInteger(StringView name, i32 value) { SetInteger(PropertyToID(name), value); }
        void SetColor(i32 id, Color value) { Changed(), values_.SetColor(id, value); }
        void SetColor(StringView name, Color value) { SetColor(PropertyToID(name), value); }
        void SetVector(i32 id, Vector4 value) { Changed(), values_.SetVector(id, value); }
        void SetVector(StringView name, Vector4 value) { SetVector(PropertyToID(name), value); }
        void SetMatrix(i32 id, const Matrix4x4& value) { Changed(), values_.SetMatrix(id, value); }
        void SetMatrix(StringView name, const Matrix4x4& value) { SetMatrix(PropertyToID(name), value); }
        void SetTexture(i32 id, Texture* value) { Changed(), values_.SetTexture(id, value ? value->GetEntityId() : EntityId()); }
        void SetTexture(StringView name, Texture* value) { SetTexture(PropertyToID(name), value); }
        void SetTextureOffset(StringView name, Vector2 value) { Changed(), values_.SetTextureOffset(PropertyToID(name), value); }
        void SetTextureScale(StringView name, Vector2 value) { Changed(), values_.SetTextureScale(PropertyToID(name), value); }
        // Arrays keep the size of their first set; they reach uniform arrays by name, 16 bytes per element.
        void SetFloatArray(StringView name, Span<const f32> values) { Changed(), values_.SetFloatArray(PropertyToID(name), values); }
        void SetVectorArray(StringView name, Span<const Vector4> values) { Changed(), values_.SetVectorArray(PropertyToID(name), values); }
        void SetColorArray(StringView name, Span<const Color> values);
        void SetMatrixArray(StringView name, Span<const Matrix4x4> values) { Changed(), values_.SetMatrixArray(PropertyToID(name), values); }

        f32 GetFloat(i32 id) const { return values_.GetFloat(id); }
        f32 GetFloat(StringView name) const { return GetFloat(PropertyToID(name)); }
        i32 GetInteger(i32 id) const { return values_.GetInt(id); }
        i32 GetInteger(StringView name) const { return GetInteger(PropertyToID(name)); }
        Color GetColor(i32 id) const { return values_.GetColor(id); }
        Color GetColor(StringView name) const { return GetColor(PropertyToID(name)); }
        Vector4 GetVector(i32 id) const { return values_.GetVector(id); }
        Vector4 GetVector(StringView name) const { return GetVector(PropertyToID(name)); }
        Matrix4x4 GetMatrix(StringView name) const { return values_.GetMatrix(PropertyToID(name)); }
        Texture* GetTexture(i32 id) const { return EntityIdToObject<Texture>(values_.GetTexture(id)); }
        Texture* GetTexture(StringView name) const { return GetTexture(PropertyToID(name)); }
        Vector2 GetTextureOffset(StringView name) const;
        Vector2 GetTextureScale(StringView name) const;
        void GetFloatArray(StringView name, Array<f32>& out) const { values_.GetFloatArray(PropertyToID(name), out); }
        void GetVectorArray(StringView name, Array<Vector4>& out) const { values_.GetVectorArray(PropertyToID(name), out); }
        void GetMatrixArray(StringView name, Array<Matrix4x4>& out) const { values_.GetMatrixArray(PropertyToID(name), out); }

        // Whether the shader has the property.
        bool HasProperty(StringView name) const;
        bool HasProperty(i32 id) const { return HasProperty(PropertyName(id)); }
        // Whether the material holds a value of the kind (arrays count for their element kind).
        bool HasFloat(StringView name) const;
        bool HasInteger(StringView name) const { return values_.Has(PropertyToID(name), PropertyKind::Int); }
        bool HasVector(StringView name) const;
        bool HasColor(StringView name) const { return HasVector(name); }
        bool HasMatrix(StringView name) const;
        bool HasTexture(StringView name) const { return values_.Has(PropertyToID(name), PropertyKind::Texture); }
        void GetTexturePropertyNames(Array<String>& out) const;
        void GetTexturePropertyNameIDs(Array<i32>& out) const;
        void GetPropertyNames(MaterialPropertyType type, Array<String>& out) const;

        // The shader's main color and texture (MainColor / MainTexture, else _Color and _MainTex).
        Color color() const { return GetColor(MainColorName()); }
        void SetColor(Color value) { SetColor(MainColorName(), value); }
        Texture* mainTexture() const { return GetTexture(MainTextureName()); }
        void SetMainTexture(Texture* value) { SetTexture(MainTextureName(), value); }
        Vector2 mainTextureOffset() const { return GetTextureOffset(MainTextureName()); }
        void SetMainTextureOffset(Vector2 value) { SetTextureOffset(MainTextureName(), value); }
        Vector2 mainTextureScale() const { return GetTextureScale(MainTextureName()); }
        void SetMainTextureScale(Vector2 value) { SetTextureScale(MainTextureName(), value); }

        // ---------------------------------------------------------------------------------------------- keywords
        // Bool keywords by name, enum keywords as name_member (one member of a keyword at a time).
        void EnableKeyword(StringView keyword);
        void DisableKeyword(StringView keyword);
        void SetKeyword(StringView keyword, bool enabled) { enabled ? EnableKeyword(keyword) : DisableKeyword(keyword); }
        bool IsKeywordEnabled(StringView keyword) const;
        // Every enabled keyword name, the shader's or not.
        Span<const String> shaderKeywords() const { return Span<const String>(keywords_.Data(), keywords_.Size()); }
        void SetShaderKeywords(Span<const String> keywords);
        // The enabled keywords the shader has.
        void GetEnabledKeywords(Array<String>& out) const;

        // ---------------------------------------------------------------------------------------------- passes
        // The override, else the shader's queue.
        i32 renderQueue() const;
        // -1 takes the shader's again.
        void SetRenderQueue(i32 queue) { Changed(), customQueue_ = queue < -1 ? -1 : queue; }
        i32 rawRenderQueue() const { return customQueue_; }
        i32 passCount() const;
        StringView GetPassName(i32 pass) const;
        // The pass of that name (case does not matter), -1 when there is none.
        i32 FindPass(StringView name) const;
        // Passes by their LightMode.
        void SetShaderPassEnabled(StringView passName, bool enabled);
        bool GetShaderPassEnabled(StringView passName) const;
        // The override tag, else the shader's (and its fallbacks' with searchFallbacks), else `fallback`.
        StringView GetTag(StringView tag, bool searchFallbacks, StringView fallback = {}) const;
        void SetOverrideTag(StringView tag, StringView value);

        bool enableInstancing() const { return instancing_; }
        void SetEnableInstancing(bool value) { Changed(), instancing_ = value; }
        bool doubleSidedGI() const { return doubleSidedGI_; }
        void SetDoubleSidedGI(bool value) { Changed(), doubleSidedGI_ = value; }
        MaterialGlobalIlluminationFlags globalIlluminationFlags() const { return giFlags_; }
        void SetGlobalIlluminationFlags(MaterialGlobalIlluminationFlags value) { Changed(), giFlags_ = value; }

        // Values, keywords and settings of another material.
        void CopyPropertiesFromMaterial(const Material& other);
        // Values of the properties this material's shader has, from another material that holds them.
        void CopyMatchingPropertiesFromMaterial(const Material& other);
        // Floats and colors of both materials interpolated.
        void Lerp(const Material& start, const Material& end, f32 t);
        i32 ComputeCRC() const;

        // ---------------------------------------------------------------------------------------------- native
        const PropertySheet& Properties() const { return values_; }
        // The blocks and textures of one pass (an index into the shader definition's passes) for a target, written
        // again when values or globals changed; null without a shader that has the pass.
        const MaterialPassData* GetPassData(u32 pass, shadercompiler::ShaderTarget target);
        // Bumped by every change.
        u32 Version() const { return version_; }

        template <class V>
        void Transfer(V& v);

    private:
        void Changed() { version_++; }
        u64 KeywordStamp() const;
        // The shader declares the keyword (local or global, enum members as name_member).
        bool KnowsKeyword(StringView keyword) const;
        void ApplyDefaults();
        String MainColorName() const;
        String MainTextureName() const;
        void Write(MaterialPassData& data) const;
        void ClearPassData();

        PPtr<Shader> shader_;
        PropertySheet values_;
        Array<String> keywords_ = Array<String>();
        i32 customQueue_ = -1;
        struct Tag
        {
            String name, value;
        };
        Array<Tag> tags_ = Array<Tag>();
        Array<String> disabledPasses_ = Array<String>();
        bool instancing_ = false;
        bool doubleSidedGI_ = false;
        MaterialGlobalIlluminationFlags giFlags_ = MaterialGlobalIlluminationFlags::EmissiveIsBlack;
        u32 version_ = 0;
        // Bumped by keyword and shader changes (the variant may be another).
        u32 keywordsVersion_ = 0;
        Array<MaterialPassData*> passData_ = Array<MaterialPassData*>();
    };
}

#include <Helix/Engine/Material/MaterialTransfer.h>
