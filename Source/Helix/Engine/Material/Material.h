#pragma once

#include <Helix/Engine/Shader/Shader.h>

// Material: a shader and the values of its properties (one PropertySheet), keywords, the render queue override, tags
// and passes switched off. For the renderer it keeps, per pass and variant, the bytes of the uniform blocks its
// properties go to and the texture of every slot; they are written again when a value (or a global) changed.
namespace helix::engine
{
    HX_ENUM(Namespace = "HelixEngine", Attributes = "[System.Flags]")
    enum class MaterialGlobalIlluminationFlags : i32
    {
        None = 0,
        RealtimeEmissive = 1,
        BakedEmissive = 2,
        AnyEmissive = 3,
        EmissiveIsBlack = 4,
    };

    HX_ENUM(Namespace = "HelixEngine")
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

    // A property block by its id in the block table (what scripts hold).
    HX_STRUCT(Namespace = "HelixEngine.Internal", Internal)
    struct PropertyBlockId
    {
        HX_FIELD() u64 id = 0;
    };

    // Values on top of a material for one renderer: the same kinds, no render state. Every block has an id in the
    // block table while it lives (so scripts reach it; the table is thread safe).
    class MaterialPropertyBlock
    {
    public:
        MaterialPropertyBlock();
        MaterialPropertyBlock(const MaterialPropertyBlock& other);
        MaterialPropertyBlock& operator=(const MaterialPropertyBlock& other)
        {
            sheet_ = other.sheet_;
            return *this;
        }
        ~MaterialPropertyBlock();

        u64 Id() const { return id_; }
        operator PropertyBlockId() const { return PropertyBlockId{id_}; }
        // The live block of an id, null when there is none.
        static MaterialPropertyBlock* Find(PropertyBlockId block);

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
        // A texture of the C++ script API (a value handle).
        template <class H>
            requires requires(const H& h) { h.HxId(); }
        void SetTexture(StringView name, const H& texture)
        {
            sheet_.SetTexture(PropertyToID(name), EntityId::FromULong(texture.HxId()));
        }
        void SetInt(StringView name, i32 value) { SetFloat(name, f32(value)); }
        void SetInt(i32 id, i32 value) { SetFloat(id, f32(value)); }
        void SetFloatArray(StringView name, Span<const f32> values) { sheet_.SetFloatArray(PropertyToID(name), values); }
        void SetFloatArray(i32 id, Span<const f32> values) { sheet_.SetFloatArray(id, values); }
        void SetVectorArray(StringView name, Span<const Vector4> values) { sheet_.SetVectorArray(PropertyToID(name), values); }
        void SetVectorArray(i32 id, Span<const Vector4> values) { sheet_.SetVectorArray(id, values); }
        void SetMatrixArray(StringView name, Span<const Matrix4x4> values) { sheet_.SetMatrixArray(PropertyToID(name), values); }
        void SetMatrixArray(i32 id, Span<const Matrix4x4> values) { sheet_.SetMatrixArray(id, values); }
        f32 GetFloat(StringView name) const { return sheet_.GetFloat(PropertyToID(name)); }
        f32 GetFloat(i32 id) const { return sheet_.GetFloat(id); }
        i32 GetInt(StringView name) const { return i32(GetFloat(name)); }
        i32 GetInt(i32 id) const { return i32(GetFloat(id)); }
        i32 GetInteger(StringView name) const { return sheet_.GetInt(PropertyToID(name)); }
        i32 GetInteger(i32 id) const { return sheet_.GetInt(id); }
        Color GetColor(StringView name) const { return sheet_.GetColor(PropertyToID(name)); }
        Color GetColor(i32 id) const { return sheet_.GetColor(id); }
        Vector4 GetVector(StringView name) const { return sheet_.GetVector(PropertyToID(name)); }
        Vector4 GetVector(i32 id) const { return sheet_.GetVector(id); }
        Matrix4x4 GetMatrix(StringView name) const { return sheet_.GetMatrix(PropertyToID(name)); }
        Matrix4x4 GetMatrix(i32 id) const { return sheet_.GetMatrix(id); }
        Texture* GetTexture(StringView name) const { return EntityIdToObject<Texture>(sheet_.GetTexture(PropertyToID(name))); }
        Texture* GetTexture(i32 id) const { return EntityIdToObject<Texture>(sheet_.GetTexture(id)); }
        Array<f32> GetFloatArray(i32 id) const
        {
            Array<f32> out;
            sheet_.GetFloatArray(id, out);
            return out;
        }
        Array<Vector4> GetVectorArray(i32 id) const
        {
            Array<Vector4> out;
            sheet_.GetVectorArray(id, out);
            return out;
        }
        Array<Matrix4x4> GetMatrixArray(i32 id) const
        {
            Array<Matrix4x4> out;
            sheet_.GetMatrixArray(id, out);
            return out;
        }
        bool HasProperty(StringView name) const { return sheet_.Has(PropertyToID(name)); }
        bool HasProperty(i32 id) const { return sheet_.Has(id); }
        bool HasFloat(StringView name) const { return HasFloat(PropertyToID(name)); }
        bool HasFloat(i32 id) const { return sheet_.Has(id, PropertyKind::Float); }
        bool HasInt(StringView name) const { return HasFloat(name); }
        bool HasInt(i32 id) const { return HasFloat(id); }
        bool HasInteger(StringView name) const { return HasInteger(PropertyToID(name)); }
        bool HasInteger(i32 id) const { return sheet_.Has(id, PropertyKind::Int); }
        bool HasVector(StringView name) const { return HasVector(PropertyToID(name)); }
        bool HasVector(i32 id) const { return sheet_.Has(id, PropertyKind::Vector); }
        bool HasColor(StringView name) const { return HasVector(name); }
        bool HasColor(i32 id) const { return HasVector(id); }
        bool HasMatrix(StringView name) const { return HasMatrix(PropertyToID(name)); }
        bool HasMatrix(i32 id) const { return sheet_.Has(id, PropertyKind::Matrix); }
        bool HasTexture(StringView name) const { return HasTexture(PropertyToID(name)); }
        bool HasTexture(i32 id) const { return sheet_.Has(id, PropertyKind::Texture); }

        void CopyFrom(const PropertySheet& from, const PropertyValue& value) { CopyPropertyValue(sheet_, from, value); }
        const PropertySheet& Sheet() const { return sheet_; }

    private:
        PropertySheet sheet_;
        u64 id_ = 0;
    };

    // What the C# MaterialPropertyBlock calls (internal in C#): blocks of the table by id.
    HX_CLASS(Namespace = "HelixEngine.Internal", Static, Internal)
    class PropertyBlockNative
    {
    public:
        // A block owned by the caller (Destroy frees it; any thread).
        HX_FUNCTION() static PropertyBlockId Create();
        HX_FUNCTION() static void Destroy(PropertyBlockId block);
        HX_FUNCTION() static bool IsEmpty(PropertyBlockId block) { return With(block, [](MaterialPropertyBlock& b) { return b.isEmpty(); }, true); }
        HX_FUNCTION() static void Clear(PropertyBlockId block) { With(block, [](MaterialPropertyBlock& b) { b.Clear(); return 0; }, 0); }
        HX_FUNCTION() static void SetFloat(PropertyBlockId block, i32 id, f32 v) { With(block, [&](MaterialPropertyBlock& b) { b.SetFloat(id, v); return 0; }, 0); }
        HX_FUNCTION() static void SetInt(PropertyBlockId block, i32 id, i32 v) { With(block, [&](MaterialPropertyBlock& b) { b.SetInt(id, v); return 0; }, 0); }
        HX_FUNCTION() static void SetInteger(PropertyBlockId block, i32 id, i32 v) { With(block, [&](MaterialPropertyBlock& b) { b.SetInteger(id, v); return 0; }, 0); }
        HX_FUNCTION() static void SetColor(PropertyBlockId block, i32 id, Color v) { With(block, [&](MaterialPropertyBlock& b) { b.SetColor(id, v); return 0; }, 0); }
        HX_FUNCTION() static void SetVector(PropertyBlockId block, i32 id, Vector4 v) { With(block, [&](MaterialPropertyBlock& b) { b.SetVector(id, v); return 0; }, 0); }
        HX_FUNCTION() static void SetMatrix(PropertyBlockId block, i32 id, Matrix4x4 v) { With(block, [&](MaterialPropertyBlock& b) { b.SetMatrix(id, v); return 0; }, 0); }
        HX_FUNCTION() static void SetTexture(PropertyBlockId block, i32 id, Texture* v) { With(block, [&](MaterialPropertyBlock& b) { b.SetTexture(id, v); return 0; }, 0); }
        HX_FUNCTION() static void SetFloatArray(PropertyBlockId block, i32 id, Span<const f32> v) { With(block, [&](MaterialPropertyBlock& b) { b.SetFloatArray(id, v); return 0; }, 0); }
        HX_FUNCTION() static void SetVectorArray(PropertyBlockId block, i32 id, Span<const Vector4> v) { With(block, [&](MaterialPropertyBlock& b) { b.SetVectorArray(id, v); return 0; }, 0); }
        HX_FUNCTION() static void SetMatrixArray(PropertyBlockId block, i32 id, Span<const Matrix4x4> v) { With(block, [&](MaterialPropertyBlock& b) { b.SetMatrixArray(id, v); return 0; }, 0); }
        HX_FUNCTION() static f32 GetFloat(PropertyBlockId block, i32 id) { return With(block, [&](MaterialPropertyBlock& b) { return b.GetFloat(id); }, 0.0f); }
        HX_FUNCTION() static i32 GetInt(PropertyBlockId block, i32 id) { return With(block, [&](MaterialPropertyBlock& b) { return b.GetInt(id); }, 0); }
        HX_FUNCTION() static i32 GetInteger(PropertyBlockId block, i32 id) { return With(block, [&](MaterialPropertyBlock& b) { return b.GetInteger(id); }, 0); }
        HX_FUNCTION() static Color GetColor(PropertyBlockId block, i32 id) { return With(block, [&](MaterialPropertyBlock& b) { return b.GetColor(id); }, Color()); }
        HX_FUNCTION() static Vector4 GetVector(PropertyBlockId block, i32 id) { return With(block, [&](MaterialPropertyBlock& b) { return b.GetVector(id); }, Vector4()); }
        HX_FUNCTION() static Matrix4x4 GetMatrix(PropertyBlockId block, i32 id) { return With(block, [&](MaterialPropertyBlock& b) { return b.GetMatrix(id); }, Matrix4x4()); }
        HX_FUNCTION() static Texture* GetTexture(PropertyBlockId block, i32 id) { return With(block, [&](MaterialPropertyBlock& b) { return b.GetTexture(id); }, (Texture*)nullptr); }
        HX_FUNCTION() static Array<f32> GetFloatArray(PropertyBlockId block, i32 id) { return With(block, [&](MaterialPropertyBlock& b) { return b.GetFloatArray(id); }, Array<f32>()); }
        HX_FUNCTION() static Array<Vector4> GetVectorArray(PropertyBlockId block, i32 id) { return With(block, [&](MaterialPropertyBlock& b) { return b.GetVectorArray(id); }, Array<Vector4>()); }
        HX_FUNCTION() static Array<Matrix4x4> GetMatrixArray(PropertyBlockId block, i32 id) { return With(block, [&](MaterialPropertyBlock& b) { return b.GetMatrixArray(id); }, Array<Matrix4x4>()); }
        HX_FUNCTION() static bool HasProperty(PropertyBlockId block, i32 id) { return With(block, [&](MaterialPropertyBlock& b) { return b.HasProperty(id); }, false); }
        HX_FUNCTION() static bool HasFloat(PropertyBlockId block, i32 id) { return With(block, [&](MaterialPropertyBlock& b) { return b.HasFloat(id); }, false); }
        HX_FUNCTION() static bool HasInteger(PropertyBlockId block, i32 id) { return With(block, [&](MaterialPropertyBlock& b) { return b.HasInteger(id); }, false); }
        HX_FUNCTION() static bool HasVector(PropertyBlockId block, i32 id) { return With(block, [&](MaterialPropertyBlock& b) { return b.HasVector(id); }, false); }
        HX_FUNCTION() static bool HasMatrix(PropertyBlockId block, i32 id) { return With(block, [&](MaterialPropertyBlock& b) { return b.HasMatrix(id); }, false); }
        HX_FUNCTION() static bool HasTexture(PropertyBlockId block, i32 id) { return With(block, [&](MaterialPropertyBlock& b) { return b.HasTexture(id); }, false); }

    private:
        template <class F, class R>
        static R With(PropertyBlockId block, F&& f, R fallback)
        {
            MaterialPropertyBlock* b = MaterialPropertyBlock::Find(block);
            return b ? R(f(*b)) : fallback;
        }
    };

    HX_CLASS(Namespace = "HelixEngine")
    class Material : public Object
    {
        HX_ENGINE_CLASS(Material, Object, ClassId::Material, ClassNone)

    public:
        Material() = default;
        // The values, not the renderer's data.
        Material(const Material& other);
        ~Material() override;

        // new Material(shader): the shader's defaults; null with an error without a shader.
        HX_FUNCTION(Constructor) static Material* Create(Shader* shader);
        // new Material(source): a copy.
        HX_FUNCTION(Constructor) static Material* Create(const Material& source);

        HX_PROPERTY() Shader* shader() const { return shader_.Get(); }
        // Values of properties both shaders have stay; the new shader's other properties get their defaults.
        void SetShader(Shader* shader);

        // ---------------------------------------------------------------------------------------------- values
        HX_FUNCTION() void SetFloat(i32 id, f32 value) { Changed(), values_.SetFloat(id, value); }
        HX_FUNCTION() void SetFloat(StringView name, f32 value) { SetFloat(PropertyToID(name), value); }
        HX_FUNCTION() void SetInteger(i32 id, i32 value) { Changed(), values_.SetInt(id, value); }
        HX_FUNCTION() void SetInteger(StringView name, i32 value) { SetInteger(PropertyToID(name), value); }
        HX_FUNCTION() void SetColor(i32 id, Color value) { Changed(), values_.SetColor(id, value); }
        HX_FUNCTION() void SetColor(StringView name, Color value) { SetColor(PropertyToID(name), value); }
        HX_FUNCTION() void SetVector(i32 id, Vector4 value) { Changed(), values_.SetVector(id, value); }
        HX_FUNCTION() void SetVector(StringView name, Vector4 value) { SetVector(PropertyToID(name), value); }
        HX_FUNCTION() void SetMatrix(i32 id, const Matrix4x4& value) { Changed(), values_.SetMatrix(id, value); }
        HX_FUNCTION() void SetMatrix(StringView name, const Matrix4x4& value) { SetMatrix(PropertyToID(name), value); }
        HX_FUNCTION() void SetTexture(i32 id, Texture* value) { Changed(), values_.SetTexture(id, value ? value->GetEntityId() : EntityId()); }
        HX_FUNCTION() void SetTexture(StringView name, Texture* value) { SetTexture(PropertyToID(name), value); }
        HX_FUNCTION() void SetTextureOffset(StringView name, Vector2 value) { Changed(), values_.SetTextureOffset(PropertyToID(name), value); }
        HX_FUNCTION() void SetTextureScale(StringView name, Vector2 value) { Changed(), values_.SetTextureScale(PropertyToID(name), value); }
        // Arrays keep the size of their first set; they reach uniform arrays by name, 16 bytes per element.
        HX_FUNCTION() void SetFloatArray(StringView name, Span<const f32> values) { Changed(), values_.SetFloatArray(PropertyToID(name), values); }
        HX_FUNCTION() void SetVectorArray(StringView name, Span<const Vector4> values) { Changed(), values_.SetVectorArray(PropertyToID(name), values); }
        HX_FUNCTION() void SetColorArray(StringView name, Span<const Color> values);
        HX_FUNCTION() void SetMatrixArray(StringView name, Span<const Matrix4x4> values) { Changed(), values_.SetMatrixArray(PropertyToID(name), values); }

        HX_FUNCTION() void SetInt(i32 id, i32 value) { SetFloat(id, f32(value)); }
        HX_FUNCTION() void SetInt(StringView name, i32 value) { SetFloat(name, f32(value)); }
        HX_FUNCTION() void SetTextureOffset(i32 id, Vector2 value) { Changed(), values_.SetTextureOffset(id, value); }
        HX_FUNCTION() void SetTextureScale(i32 id, Vector2 value) { Changed(), values_.SetTextureScale(id, value); }
        HX_FUNCTION() void SetFloatArray(i32 id, Span<const f32> values) { Changed(), values_.SetFloatArray(id, values); }
        HX_FUNCTION() void SetVectorArray(i32 id, Span<const Vector4> values) { Changed(), values_.SetVectorArray(id, values); }
        HX_FUNCTION() void SetColorArray(i32 id, Span<const Color> values) { SetColorArray(PropertyName(id), values); }
        HX_FUNCTION() void SetMatrixArray(i32 id, Span<const Matrix4x4> values) { Changed(), values_.SetMatrixArray(id, values); }
        HX_FUNCTION() f32 GetFloat(i32 id) const { return values_.GetFloat(id); }
        HX_FUNCTION() f32 GetFloat(StringView name) const { return GetFloat(PropertyToID(name)); }
        HX_FUNCTION() i32 GetInteger(i32 id) const { return values_.GetInt(id); }
        HX_FUNCTION() i32 GetInteger(StringView name) const { return GetInteger(PropertyToID(name)); }
        HX_FUNCTION() Color GetColor(i32 id) const { return values_.GetColor(id); }
        HX_FUNCTION() Color GetColor(StringView name) const { return GetColor(PropertyToID(name)); }
        HX_FUNCTION() Vector4 GetVector(i32 id) const { return values_.GetVector(id); }
        HX_FUNCTION() Vector4 GetVector(StringView name) const { return GetVector(PropertyToID(name)); }
        HX_FUNCTION() Matrix4x4 GetMatrix(StringView name) const { return values_.GetMatrix(PropertyToID(name)); }
        HX_FUNCTION() Matrix4x4 GetMatrix(i32 id) const { return values_.GetMatrix(id); }
        HX_FUNCTION() i32 GetInt(i32 id) const { return i32(GetFloat(id)); }
        HX_FUNCTION() i32 GetInt(StringView name) const { return i32(GetFloat(name)); }
        HX_FUNCTION() Vector2 GetTextureOffset(i32 id) const { return GetTextureOffset(PropertyName(id)); }
        HX_FUNCTION() Vector2 GetTextureScale(i32 id) const { return GetTextureScale(PropertyName(id)); }
        HX_FUNCTION() Texture* GetTexture(i32 id) const { return EntityIdToObject<Texture>(values_.GetTexture(id)); }
        HX_FUNCTION() Texture* GetTexture(StringView name) const { return GetTexture(PropertyToID(name)); }
        HX_FUNCTION() Vector2 GetTextureOffset(StringView name) const;
        HX_FUNCTION() Vector2 GetTextureScale(StringView name) const;
        void GetFloatArray(StringView name, Array<f32>& out) const { values_.GetFloatArray(PropertyToID(name), out); }
        void GetVectorArray(StringView name, Array<Vector4>& out) const { values_.GetVectorArray(PropertyToID(name), out); }
        void GetMatrixArray(StringView name, Array<Matrix4x4>& out) const { values_.GetMatrixArray(PropertyToID(name), out); }
        HX_FUNCTION() Array<f32> GetFloatArray(i32 id) const
        {
            Array<f32> out;
            values_.GetFloatArray(id, out);
            return out;
        }
        HX_FUNCTION() Array<Vector4> GetVectorArray(i32 id) const
        {
            Array<Vector4> out;
            values_.GetVectorArray(id, out);
            return out;
        }
        HX_FUNCTION() Array<Color> GetColorArray(i32 id) const;
        HX_FUNCTION() Array<Color> GetColorArray(StringView name) const { return GetColorArray(PropertyToID(name)); }
        HX_FUNCTION() Array<f32> GetFloatArray(StringView name) const { return GetFloatArray(PropertyToID(name)); }
        HX_FUNCTION() Array<Vector4> GetVectorArray(StringView name) const { return GetVectorArray(PropertyToID(name)); }
        HX_FUNCTION() Array<Matrix4x4> GetMatrixArray(StringView name) const { return GetMatrixArray(PropertyToID(name)); }
        HX_FUNCTION() Array<Matrix4x4> GetMatrixArray(i32 id) const
        {
            Array<Matrix4x4> out;
            values_.GetMatrixArray(id, out);
            return out;
        }

        // Whether the shader has the property.
        HX_FUNCTION() bool HasProperty(StringView name) const;
        HX_FUNCTION() bool HasProperty(i32 id) const { return HasProperty(PropertyName(id)); }
        // Whether the material holds a value of the kind (arrays count for their element kind).
        HX_FUNCTION() bool HasFloat(StringView name) const;
        HX_FUNCTION() bool HasInteger(StringView name) const { return values_.Has(PropertyToID(name), PropertyKind::Int); }
        HX_FUNCTION() bool HasVector(StringView name) const;
        HX_FUNCTION() bool HasColor(StringView name) const { return HasVector(name); }
        HX_FUNCTION() bool HasMatrix(StringView name) const;
        HX_FUNCTION() bool HasTexture(StringView name) const { return values_.Has(PropertyToID(name), PropertyKind::Texture); }
        HX_FUNCTION() bool HasFloat(i32 id) const { return HasFloat(PropertyName(id)); }
        HX_FUNCTION() bool HasInt(StringView name) const { return HasFloat(name); }
        HX_FUNCTION() bool HasInt(i32 id) const { return HasFloat(id); }
        HX_FUNCTION() bool HasInteger(i32 id) const { return values_.Has(id, PropertyKind::Int); }
        HX_FUNCTION() bool HasVector(i32 id) const { return HasVector(PropertyName(id)); }
        HX_FUNCTION() bool HasColor(i32 id) const { return HasVector(id); }
        HX_FUNCTION() bool HasMatrix(i32 id) const { return HasMatrix(PropertyName(id)); }
        HX_FUNCTION() bool HasTexture(i32 id) const { return values_.Has(id, PropertyKind::Texture); }
        void GetTexturePropertyNames(Array<String>& out) const;
        void GetTexturePropertyNameIDs(Array<i32>& out) const;
        void GetPropertyNames(MaterialPropertyType type, Array<String>& out) const;
        // The same as results (the reference's array returning overloads).
        HX_FUNCTION() Array<String> GetTexturePropertyNames() const
        {
            Array<String> out;
          GetTexturePropertyNames(out);
            return out;
        }
        HX_FUNCTION() Array<i32> GetTexturePropertyNameIDs() const
        {
            Array<i32> out;
        GetTexturePropertyNameIDs(out);
            return out;
        }
        HX_FUNCTION() Array<String> GetPropertyNames(MaterialPropertyType type) const
        {
            Array<String> out;
          GetPropertyNames(type, out);
            return out;
        }

        // The shader's main color and texture (MainColor / MainTexture, else _Color and _MainTex).
        HX_PROPERTY() Color color() const { return GetColor(MainColorName()); }
        void SetColor(Color value) { SetColor(MainColorName(), value); }
        HX_PROPERTY() Texture* mainTexture() const { return GetTexture(MainTextureName()); }
        void SetMainTexture(Texture* value) { SetTexture(MainTextureName(), value); }
        HX_PROPERTY() Vector2 mainTextureOffset() const { return GetTextureOffset(MainTextureName()); }
        void SetMainTextureOffset(Vector2 value) { SetTextureOffset(MainTextureName(), value); }
        HX_PROPERTY() Vector2 mainTextureScale() const { return GetTextureScale(MainTextureName()); }
        void SetMainTextureScale(Vector2 value) { SetTextureScale(MainTextureName(), value); }

        // ---------------------------------------------------------------------------------------------- keywords
        // Bool keywords by name, enum keywords as name_member (one member of a keyword at a time).
        HX_FUNCTION() void EnableKeyword(StringView keyword);
        HX_FUNCTION() void DisableKeyword(StringView keyword);
        HX_FUNCTION() void SetKeyword(StringView keyword, bool enabled) { enabled ? EnableKeyword(keyword) : DisableKeyword(keyword); }
        HX_FUNCTION() bool IsKeywordEnabled(StringView keyword) const;
        // By the shader's keyword space.
        HX_FUNCTION() void EnableKeyword(const LocalKeyword& keyword) { EnableKeyword(keyword.name()); }
        HX_FUNCTION() void DisableKeyword(const LocalKeyword& keyword) { DisableKeyword(keyword.name()); }
        HX_FUNCTION() void SetKeyword(const LocalKeyword& keyword, bool value) { SetKeyword(keyword.name(), value); }
        HX_FUNCTION() bool IsKeywordEnabled(const LocalKeyword& keyword) const { return IsKeywordEnabled(keyword.name()); }
        // Every enabled keyword name, the shader's or not.
        HX_PROPERTY() Span<const String> shaderKeywords() const { return Span<const String>(keywords_.Data(), keywords_.Size()); }
        void SetShaderKeywords(Span<const String> keywords);
        // The enabled keywords the shader has.
        void GetEnabledKeywords(Array<String>& out) const;
        // The same in the shader's keyword space; setting it leaves only those enabled.
        HX_PROPERTY() Array<LocalKeyword> enabledKeywords() const;
        void SetEnabledKeywords(Span<const LocalKeyword> keywords);

        // ---------------------------------------------------------------------------------------------- passes
        // The override, else the shader's queue.
        HX_PROPERTY() i32 renderQueue() const;
        // -1 takes the shader's again.
        void SetRenderQueue(i32 queue) { Changed(), customQueue_ = queue < -1 ? -1 : queue; }
        HX_PROPERTY(ReadOnly) i32 rawRenderQueue() const { return customQueue_; }
        HX_PROPERTY(ReadOnly) i32 passCount() const;
        HX_FUNCTION() StringView GetPassName(i32 pass) const;
        // The pass of that name (case does not matter), -1 when there is none.
        HX_FUNCTION() i32 FindPass(StringView name) const;
        // Passes by their LightMode.
        HX_FUNCTION() void SetShaderPassEnabled(StringView passName, bool enabled);
        HX_FUNCTION() bool GetShaderPassEnabled(StringView passName) const;
        // The override tag, else the shader's (and its fallbacks' with searchFallbacks), else `fallback`.
        HX_FUNCTION() StringView GetTag(StringView tag, bool searchFallbacks, StringView defaultValue) const;
        HX_FUNCTION() StringView GetTag(StringView tag, bool searchFallbacks) const { return GetTag(tag, searchFallbacks, StringView()); }
        HX_FUNCTION() void SetOverrideTag(StringView tag, StringView value);

        HX_PROPERTY() bool enableInstancing() const { return instancing_; }
        void SetEnableInstancing(bool value) { Changed(), instancing_ = value; }
        HX_PROPERTY() bool doubleSidedGI() const { return doubleSidedGI_; }
        void SetDoubleSidedGI(bool value) { Changed(), doubleSidedGI_ = value; }
        HX_PROPERTY() MaterialGlobalIlluminationFlags globalIlluminationFlags() const { return giFlags_; }
        void SetGlobalIlluminationFlags(MaterialGlobalIlluminationFlags value) { Changed(), giFlags_ = value; }

        // Values, keywords and settings of another material.
        HX_FUNCTION() void CopyPropertiesFromMaterial(const Material& other);
        // Values of the properties this material's shader has, from another material that holds them.
        HX_FUNCTION() void CopyMatchingPropertiesFromMaterial(const Material& other);
        // Floats and colors of both materials interpolated.
        HX_FUNCTION() void Lerp(const Material& start, const Material& end, f32 t);
        HX_FUNCTION() i32 ComputeCRC() const;

        // ---------------------------------------------------------------------------------------------- native
        const PropertySheet& Properties() const { return values_; }
        // The blocks and textures of one pass (an index into the shader definition's passes) for a target, written
        // again when values or globals changed; null without a shader that has the pass.
        const MaterialPassData* GetPassData(u32 pass, shadercompiler::ShaderTarget target);
        // Bumped by every change.
        u32 Version() const { return version_; }
        // Writes values into a variant's blocks and texture slots. Without `overlay` the blocks start from their defaults,
        // every texture slot is filled (defaults by name) and members no value sets take the globals; with it only the
        // values present change what is there (property blocks over a material).
        static void WriteValues(const ShaderVariant& variant, const PropertySheet& values, const PropertySheet* globals, bool overlay,
                                Array<Array<u8>>& blocks, Array<MaterialPassData::TextureSlot>& textures);

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
