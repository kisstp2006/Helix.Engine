#include <Helix/Engine/Material/Material.h>

#include <Helix/Core/Hashing/Hash.h>
#include <Helix/Core/Logging/Log.h>
#include <Helix/Core/Strings/Format.h>

#include <cstring>

namespace helix::engine
{
    using namespace shadercompiler;

    namespace
    {
        LogChannel MaterialLog{"Engine"};

        char Lower(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }

        bool SameIgnoringCase(StringView a, StringView b)
        {
            if (a.Size() != b.Size())
                return false;
            for (usize i = 0; i < a.Size(); i++)
                if (Lower(a[i]) != Lower(b[i]))
                    return false;
            return true;
        }

        // A default texture of a property by its name ("white", "black", "gray", "bump", ...); gray without one.
        struct DefaultTexture
        {
            StringView name;
            Texture2D* (*make)();
        };
        constexpr DefaultTexture kDefaultTextures[] = {
            {"white", &Texture2D::whiteTexture}, {"black", &Texture2D::blackTexture}, {"gray", &Texture2D::grayTexture},
            {"grey", &Texture2D::grayTexture},   {"red", &Texture2D::redTexture},     {"bump", &Texture2D::normalTexture},
            {"normal", &Texture2D::normalTexture}, {"linearGray", &Texture2D::linearGrayTexture},
        };

        Texture2D* Default(StringView name)
        {
            for (const DefaultTexture& d : kDefaultTextures)
                if (SameIgnoringCase(d.name, name))
                    return d.make();
            return Texture2D::grayTexture();
        }

        // One value copied from a sheet into another, whatever its kind.
        void CopyValue(PropertySheet& to, const PropertySheet& from, const PropertyValue& v)
        {
            Span<const f32> n = from.Numbers(v);
            switch (v.kind)
            {
            case PropertyKind::Float:
                to.SetFloat(v.id, n[0]);
                break;
            case PropertyKind::Int:
                to.SetInt(v.id, from.GetInt(v.id));
                break;
            case PropertyKind::Vector:
                to.SetVector(v.id, Vector4(n[0], n[1], n[2], n[3]));
                break;
            case PropertyKind::Matrix:
                to.SetMatrix(v.id, from.GetMatrix(v.id));
                break;
            case PropertyKind::Texture:
                to.SetTexture(v.id, v.texture);
                to.SetTextureScale(v.id, Vector2(n[0], n[1]));
                to.SetTextureOffset(v.id, Vector2(n[2], n[3]));
                break;
            case PropertyKind::FloatArray:
                to.SetFloatArray(v.id, n);
                break;
            case PropertyKind::VectorArray:
                to.SetVectorArray(v.id, Span<const Vector4>(reinterpret_cast<const Vector4*>(n.Data()), v.count));
                break;
            case PropertyKind::MatrixArray:
                to.SetMatrixArray(v.id, Span<const Matrix4x4>(reinterpret_cast<const Matrix4x4*>(n.Data()), v.count));
                break;
            }
        }

        // `count` numbers of a value as floats (ints by their value).
        void Floats(const PropertySheet& sheet, const PropertyValue& v, u32 first, u32 count, f32* out)
        {
            Span<const f32> n = sheet.Numbers(v);
            for (u32 i = 0; i < count; i++)
            {
                if (first + i >= n.Size())
                {
                    out[i] = 0;
                    continue;
                }
                if (v.kind == PropertyKind::Int)
                {
                    i32 bits;
                    std::memcpy(&bits, &n[first + i], 4);
                    out[i] = f32(bits);
                }
                else
                    out[i] = n[first + i];
            }
        }

        // Writes floats into a block member of any numeric type.
        void WriteMember(const ReflectedMember& m, const f32* values, u32 count, Array<u8>& block)
        {
            u32 components = m.size / 4;
            if (count < components)
                components = count;
            if (m.offset + components * 4 > block.Size())
                return;
            u8* at = block.Data() + m.offset;
            for (u32 i = 0; i < components; i++)
            {
                if (m.type.base == BaseType::Int)
                {
                    i32 v = i32(values[i]);
                    std::memcpy(at + i * 4, &v, 4);
                }
                else if (m.type.base == BaseType::UInt)
                {
                    u32 v = values[i] < 0 ? 0u : u32(values[i]);
                    std::memcpy(at + i * 4, &v, 4);
                }
                else
                    std::memcpy(at + i * 4, &values[i], 4);
            }
        }

    }

    // ---------------------------------------------------------------------------------------------- lifetime

    Material::Material(const Material& other)
        : Object(other), shader_(other.shader_), values_(other.values_), keywords_(other.keywords_), customQueue_(other.customQueue_),
          tags_(other.tags_), disabledPasses_(other.disabledPasses_), instancing_(other.instancing_), doubleSidedGI_(other.doubleSidedGI_),
          giFlags_(other.giFlags_)
    {
    }

    Material::~Material() { ClearPassData(); }

    void Material::ClearPassData()
    {
        for (MaterialPassData* d : passData_)
            delete d;
        passData_.Clear();
    }

    Material* Material::Create(Shader* shader)
    {
        if (!shader)
        {
            HX_LOG_ERROR(MaterialLog, "new Material: the shader is null.");
            return nullptr;
        }
        Material* m = NewObject<Material>();
        m->SetShader(shader);
        return m;
    }

    Material* Material::Create(const Material& source) { return static_cast<Material*>(CloneObject(source)); }

    void Material::SetShader(Shader* shader)
    {
        shader_ = PPtr<Shader>(shader);
        keywordsVersion_++;
        ClearPassData();
        ApplyDefaults();
        Changed();
    }

    void Material::ApplyDefaults()
    {
        Shader* s = shader_.Get();
        const ShaderDescription* d = s ? s->Description() : nullptr;
        if (!d)
            return;
        for (const ShaderProperty& p : d->properties)
        {
            i32 id = PropertyToID(p.name);
            if (values_.Has(id))
                continue;
            if (p.type.IsTexture())
                values_.SetTexture(id, EntityId());
            else if (p.type.IsMatrix())
                values_.SetMatrix(id, Matrix4x4(Vector4(1, 0, 0, 0), Vector4(0, 1, 0, 0), Vector4(0, 0, 1, 0), Vector4(0, 0, 0, 1)));
            else if (p.type.IsScalar())
            {
                if (p.type.IsInteger())
                    values_.SetInt(id, i32(p.defaultValue[0]));
                else
                    values_.SetFloat(id, p.defaultValue[0]);
            }
            else
                values_.SetVector(id, Vector4(p.defaultValue[0], p.defaultValue[1], p.defaultValue[2], p.defaultValue[3]));
        }
    }

    // ---------------------------------------------------------------------------------------------- values

    void Material::SetColorArray(StringView name, Span<const Color> values)
    {
        Changed();
        values_.SetVectorArray(PropertyToID(name), Span<const Vector4>(reinterpret_cast<const Vector4*>(values.Data()), values.Size()));
    }

    Vector2 Material::GetTextureOffset(StringView name) const
    {
        Vector4 st = values_.GetTextureScaleOffset(PropertyToID(name));
        return Vector2(st.z, st.w);
    }

    Vector2 Material::GetTextureScale(StringView name) const
    {
        Vector4 st = values_.GetTextureScaleOffset(PropertyToID(name));
        return Vector2(st.x, st.y);
    }

    bool Material::HasProperty(StringView name) const
    {
        Shader* s = shader_.Get();
        const ShaderDescription* d = s ? s->Description() : nullptr;
        return d && d->FindProperty(name);
    }

    bool Material::HasFloat(StringView name) const
    {
        i32 id = PropertyToID(name);
        return values_.Has(id, PropertyKind::Float) || values_.Has(id, PropertyKind::FloatArray);
    }

    bool Material::HasVector(StringView name) const
    {
        i32 id = PropertyToID(name);
        return values_.Has(id, PropertyKind::Vector) || values_.Has(id, PropertyKind::VectorArray);
    }

    bool Material::HasMatrix(StringView name) const
    {
        i32 id = PropertyToID(name);
        return values_.Has(id, PropertyKind::Matrix) || values_.Has(id, PropertyKind::MatrixArray);
    }

    void Material::GetTexturePropertyNames(Array<String>& out) const
    {
        out.Clear();
        for (const PropertyValue& v : values_.Values())
            if (v.kind == PropertyKind::Texture)
                out.Push(String(PropertyName(v.id)));
    }

    void Material::GetTexturePropertyNameIDs(Array<i32>& out) const
    {
        out.Clear();
        for (const PropertyValue& v : values_.Values())
            if (v.kind == PropertyKind::Texture)
                out.Push(v.id);
    }

    void Material::GetPropertyNames(MaterialPropertyType type, Array<String>& out) const
    {
        out.Clear();
        struct KindOf
        {
            MaterialPropertyType type;
            PropertyKind kind;
        };
        static constexpr KindOf kKinds[] = {
            {MaterialPropertyType::Float, PropertyKind::Float},   {MaterialPropertyType::Int, PropertyKind::Int},
            {MaterialPropertyType::Vector, PropertyKind::Vector}, {MaterialPropertyType::Matrix, PropertyKind::Matrix},
            {MaterialPropertyType::Texture, PropertyKind::Texture},
        };
        for (const KindOf& k : kKinds)
            if (k.type == type)
                for (const PropertyValue& v : values_.Values())
                    if (v.kind == k.kind)
                        out.Push(String(PropertyName(v.id)));
    }

    String Material::MainColorName() const
    {
        Shader* s = shader_.Get();
        if (const ShaderDescription* d = s ? s->Description() : nullptr)
            for (const ShaderProperty& p : d->properties)
                if (p.Has(PropertyFlags::MainColor))
                    return p.name;
        return String("_Color");
    }

    String Material::MainTextureName() const
    {
        Shader* s = shader_.Get();
        if (const ShaderDescription* d = s ? s->Description() : nullptr)
            for (const ShaderProperty& p : d->properties)
                if (p.Has(PropertyFlags::MainTexture))
                    return p.name;
        return String("_MainTex");
    }

    // ---------------------------------------------------------------------------------------------- keywords

    bool Material::KnowsKeyword(StringView keyword) const
    {
        Shader* s = shader_.Get();
        const ShaderDescription* d = s ? s->Description() : nullptr;
        if (!d)
            return false;
        for (const ShaderKeyword& k : d->keywords)
        {
            if (k.valueNames.Empty() && k.name == keyword)
                return true;
            for (const String& m : k.valueNames)
                if (keyword.Size() == k.name.Size() + 1 + m.Size() && keyword.StartsWith(k.name) && keyword[k.name.Size()] == '_' && keyword.EndsWith(m))
                    return true;
        }
        return false;
    }

    void Material::EnableKeyword(StringView keyword)
    {
        if (IsKeywordEnabled(keyword))
            return;
        // One member of an enum keyword at a time.
        Shader* s = shader_.Get();
        if (const ShaderDescription* d = s ? s->Description() : nullptr)
            for (const ShaderKeyword& k : d->keywords)
                for (const String& m : k.valueNames)
                    if (keyword == StringView(Format("{}_{}", k.name, m)))
                        for (const String& other : k.valueNames)
                            DisableKeyword(Format("{}_{}", k.name, other));
        keywords_.Push(String(keyword));
        keywordsVersion_++;
        Changed();
    }

    void Material::DisableKeyword(StringView keyword)
    {
        for (usize i = 0; i < keywords_.Size(); i++)
            if (keywords_[i] == keyword)
            {
                keywords_.RemoveAt(i);
                keywordsVersion_++;
                Changed();
                return;
            }
    }

    bool Material::IsKeywordEnabled(StringView keyword) const
    {
        for (const String& k : keywords_)
            if (k == keyword)
                return true;
        return false;
    }

    void Material::SetShaderKeywords(Span<const String> keywords)
    {
        keywords_.Clear();
        for (const String& k : keywords)
            keywords_.Push(k);
        keywordsVersion_++;
        Changed();
    }

    void Material::GetEnabledKeywords(Array<String>& out) const
    {
        out.Clear();
        for (const String& k : keywords_)
            if (KnowsKeyword(k))
                out.Push(k);
    }

    // ---------------------------------------------------------------------------------------------- passes and tags

    i32 Material::renderQueue() const
    {
        if (customQueue_ >= 0)
            return customQueue_;
        Shader* s = shader_.Get();
        return s ? s->renderQueue() : 2000;
    }

    i32 Material::passCount() const
    {
        Shader* s = shader_.Get();
        return s ? s->passCount() : 0;
    }

    StringView Material::GetPassName(i32 pass) const
    {
        Shader* s = shader_.Get();
        i32 i = s ? s->PassIndex(pass) : -1;
        return i >= 0 ? StringView(s->Description()->passes[u32(i)].name) : StringView();
    }

    i32 Material::FindPass(StringView name) const
    {
        for (i32 p = 0; p < passCount(); p++)
            if (SameIgnoringCase(GetPassName(p), name))
                return p;
        return -1;
    }

    void Material::SetShaderPassEnabled(StringView passName, bool enabled)
    {
        for (usize i = 0; i < disabledPasses_.Size(); i++)
            if (SameIgnoringCase(disabledPasses_[i], passName))
            {
                if (enabled)
                    disabledPasses_.RemoveAt(i), Changed();
                return;
            }
        if (!enabled)
        {
            disabledPasses_.Push(String(passName));
            Changed();
        }
    }

    bool Material::GetShaderPassEnabled(StringView passName) const
    {
        for (const String& p : disabledPasses_)
            if (SameIgnoringCase(p, passName))
                return false;
        return true;
    }

    StringView Material::GetTag(StringView tag, bool searchFallbacks, StringView fallback) const
    {
        for (const Tag& t : tags_)
            if (t.name == tag)
                return t.value;
        Shader* s = shader_.Get();
        // The shader, then (when asked) its fallbacks, a few levels deep.
        for (u32 depth = 0; s && depth < 8; depth++)
        {
            StringView v = s->FindSubshaderTagValue(0, tag);
            if (!v.Empty())
                return v;
            const ShaderDescription* d = s->Description();
            if (!searchFallbacks || !d || d->fallback.Empty())
                break;
            s = Shader::Find(d->fallback);
        }
        return fallback;
    }

    void Material::SetOverrideTag(StringView tag, StringView value)
    {
        Changed();
        for (usize i = 0; i < tags_.Size(); i++)
            if (tags_[i].name == tag)
            {
                if (value.Empty())
                    tags_.RemoveAt(i);
                else
                    tags_[i].value = String(value);
                return;
            }
        if (!value.Empty())
            tags_.Push({String(tag), String(value)});
    }

    // ---------------------------------------------------------------------------------------------- copies

    void Material::CopyPropertiesFromMaterial(const Material& other)
    {
        values_ = other.values_;
        keywords_ = other.keywords_;
        keywordsVersion_++;
        customQueue_ = other.customQueue_;
        tags_ = other.tags_;
        disabledPasses_ = other.disabledPasses_;
        instancing_ = other.instancing_;
        doubleSidedGI_ = other.doubleSidedGI_;
        giFlags_ = other.giFlags_;
        Changed();
    }

    void Material::CopyMatchingPropertiesFromMaterial(const Material& other)
    {
        Shader* s = shader_.Get();
        const ShaderDescription* d = s ? s->Description() : nullptr;
        if (!d)
            return;
        for (const ShaderProperty& p : d->properties)
            if (const PropertyValue* v = other.values_.Find(PropertyToID(p.name)))
                CopyValue(values_, other.values_, *v);
        for (const String& k : other.keywords_)
            if (KnowsKeyword(k))
                EnableKeyword(k);
        Changed();
    }

    void Material::Lerp(const Material& start, const Material& end, f32 t)
    {
        for (const PropertyValue& a : start.values_.Values())
        {
            const PropertyValue* b = end.values_.Find(a.id);
            if (!b || b->kind != a.kind)
                continue;
            Span<const f32> x = start.values_.Numbers(a), y = end.values_.Numbers(*b);
            if (a.kind == PropertyKind::Float)
                values_.SetFloat(a.id, x[0] + (y[0] - x[0]) * t);
            else if (a.kind == PropertyKind::Vector)
                values_.SetVector(a.id, Vector4(x[0] + (y[0] - x[0]) * t, x[1] + (y[1] - x[1]) * t, x[2] + (y[2] - x[2]) * t, x[3] + (y[3] - x[3]) * t));
        }
        Changed();
    }

    i32 Material::ComputeCRC() const
    {
        u64 h = MixHash(EntityId::ToULong(shader_.Id()));
        for (const PropertyValue& v : values_.Values())
        {
            Span<const f32> n = values_.Numbers(v);
            h = CombineHash(h, CombineHash(MixHash(u64(u32(v.id))), MixHash(u64(v.kind))));
            h = CombineHash(h, Hash64(n.Data(), n.Size() * sizeof(f32)));
            h = CombineHash(h, MixHash(EntityId::ToULong(v.texture)));
        }
        for (const String& k : keywords_)
            h = CombineHash(h, Hash64(k.Data(), k.Size()));
        return i32(u32(h ^ (h >> 32)));
    }

    // ---------------------------------------------------------------------------------------------- renderer data

    void Material::Write(MaterialPassData& data) const
    {
        const ShaderVariant& v = *data.variant;
        const ShaderDescription* d = v.description;
        const MaterialLayout& layout = v.layout;
        const PropertySheet& globals = Shader::Globals();
        data.blocks.Resize(layout.blocks.Size());
        for (u32 i = 0; i < layout.blocks.Size(); i++)
            data.blocks[i] = layout.blocks[i].defaults;
        data.textures.Clear();
        f32 numbers[16];
        // Properties where the layout puts them.
        for (u32 k = 0; k < layout.bindings.Size(); k++)
        {
            const PropertyBinding& binding = layout.bindings[k];
            const ShaderProperty& p = d->properties[binding.property];
            i32 id = v.bindingIds[k];
            const PropertyValue* value = values_.Find(id);
            switch (binding.target)
            {
            case PropertyTarget::Member:
                if (value && value->kind != PropertyKind::Texture)
                {
                    u32 count = PropertySheet::Width(value->kind);
                    Floats(values_, *value, 0, count, numbers);
                    WritePropertyValue(p, binding, Span<const f32>(numbers, count), data.blocks[binding.block]);
                }
                break;
            case PropertyTarget::ScaleOffset:
            {
                Vector4 st = values_.GetTextureScaleOffset(id);
                const f32 s[4] = {st.x, st.y, st.z, st.w};
                WritePropertyValue(p, binding, Span<const f32>(s, 4), data.blocks[binding.block]);
                break;
            }
            case PropertyTarget::Texture:
            {
                EntityId texture = value ? value->texture : EntityId();
                if (!EntityIdToObject(texture))
                    texture = Default(p.defaultTexture)->GetEntityId();
                data.textures.Push({binding.slot, texture});
                break;
            }
            }
        }
        // Members of the same blocks that are no properties take values by name: the material's, else the globals.
        for (const ShaderVariant::NamedMember& m : v.namedMembers)
        {
            const PropertyValue* value = values_.Find(m.id);
            const PropertySheet* sheet = &values_;
            if (!value)
                value = globals.Find(m.id), sheet = &globals;
            if (!value || value->kind == PropertyKind::Texture || m.element >= value->count)
                continue;
            u32 width = PropertySheet::Width(value->kind);
            Floats(*sheet, *value, m.element * width, width, numbers);
            WriteMember(*m.member, numbers, width, data.blocks[m.block]);
        }
    }

    u64 Material::KeywordStamp() const
    {
        return CombineHash(CombineHash(MixHash(EntityId::ToULong(shader_.Id())), MixHash(keywordsVersion_)), MixHash(Shader::GlobalKeywordsVersion()));
    }

    const MaterialPassData* Material::GetPassData(u32 pass, ShaderTarget target)
    {
        // The usual case: the same pass and target as before, keywords unchanged.
        u64 stamp = KeywordStamp();
        MaterialPassData* data = nullptr;
        for (MaterialPassData* d : passData_)
            if (d->pass == pass && d->target == target && d->keywordStamp == stamp)
            {
                data = d;
                break;
            }
        if (!data)
        {
            Shader* s = shader_.Get();
            if (!s)
                s = Shader::ErrorShader();
            if (!s)
                return nullptr;
            Array<KeywordValue> keywords;
            s->KeywordValues(Span<const String>(keywords_.Data(), keywords_.Size()), keywords);
            const ShaderVariant* v = s->GetVariant(pass, Span<const KeywordValue>(keywords.Data(), keywords.Size()), target);
            if (!v)
                return nullptr;
            for (MaterialPassData* d : passData_)
                if (d->variant == v && d->pass == pass && d->target == target)
                    data = d;
            if (!data)
            {
                data = new MaterialPassData();
                data->variant = v;
                data->pass = pass;
                data->target = target;
                passData_.Push(data);
            }
            data->keywordStamp = stamp;
        }
        u32 globals = Shader::Globals().Version();
        if (data->version != version_ || data->globalsVersion != globals)
        {
            Write(*data);
            data->version = version_;
            data->globalsVersion = globals;
        }
        return data;
    }
}
