#pragma once

// The .mat form of a Material: the reference's fields, saved properties as {first, second} pairs per kind (textures
// with their tiling, ints, floats, colors); arrays and matrices are not saved.
namespace helix::engine
{
    namespace detail
    {
        struct Vector2Field
        {
            static constexpr StringView kTypeName = "Vector2f";
            static constexpr bool kFlow = true;
            f32* value;
            template <class V>
            void Transfer(V& v)
            {
                v.Field("x", value[0]);
                v.Field("y", value[1]);
            }
        };

        struct ColorField
        {
            static constexpr StringView kTypeName = "ColorRGBA";
            static constexpr bool kFlow = true;
            f32 value[4] = {};
            template <class V>
            void Transfer(V& v)
            {
                v.Field("r", value[0]);
                v.Field("g", value[1]);
                v.Field("b", value[2]);
                v.Field("a", value[3]);
            }
        };

        struct TexEnv
        {
            static constexpr StringView kTypeName = "HelixTexEnv";
            PPtr<Texture> m_Texture;
            f32 scale[2] = {1, 1}, offset[2] = {0, 0};
            template <class V>
            void Transfer(V& v)
            {
                v.Field("m_Texture", m_Texture);
                Vector2Field s{scale}, o{offset};
                v.Field("m_Scale", s);
                v.Field("m_Offset", o);
            }
        };

        template <class T, StringView const& Name>
        struct Saved
        {
            static constexpr StringView kTypeName = Name;
            String first;
            T second;
            template <class V>
            void Transfer(V& v)
            {
                v.Field("first", first);
                v.Field("second", second);
            }
        };

        inline constexpr StringView kTexEnvPair = "pair<TexEnv>";
        inline constexpr StringView kIntPair = "pair<int>";
        inline constexpr StringView kFloatPair = "pair<float>";
        inline constexpr StringView kColorPair = "pair<ColorRGBA>";

        struct SavedProperties
        {
            static constexpr StringView kTypeName = "HelixPropertySheet";
            Array<Saved<TexEnv, kTexEnvPair>> m_TexEnvs = Array<Saved<TexEnv, kTexEnvPair>>();
            Array<Saved<i32, kIntPair>> m_Ints = Array<Saved<i32, kIntPair>>();
            Array<Saved<f32, kFloatPair>> m_Floats = Array<Saved<f32, kFloatPair>>();
            Array<Saved<ColorField, kColorPair>> m_Colors = Array<Saved<ColorField, kColorPair>>();
            template <class V>
            void Transfer(V& v)
            {
                v.Field("m_TexEnvs", m_TexEnvs);
                v.Field("m_Ints", m_Ints);
                v.Field("m_Floats", m_Floats);
                v.Field("m_Colors", m_Colors);
            }
        };

        struct TagPair
        {
            static constexpr StringView kTypeName = "pair<string>";
            String first, second;
            template <class V>
            void Transfer(V& v)
            {
                v.Field("first", first);
                v.Field("second", second);
            }
        };
    }

    template <class V>
    void Material::Transfer(V& v)
    {
        Base::Transfer(v);
        v.Field("m_Name", name_);
        v.Field("m_Shader", shader_);
        Array<String> valid, invalid;
        detail::SavedProperties saved;
        Array<detail::TagPair> tags;
        i32 gi = i32(giFlags_);
        if constexpr (!V::kReading)
        {
            for (const String& k : keywords_)
                (KnowsKeyword(k) ? valid : invalid).Push(k);
            for (const PropertyValue& p : values_.Values())
            {
                Span<const f32> n = values_.Numbers(p);
                String name(PropertyName(p.id));
                switch (p.kind)
                {
                case PropertyKind::Texture:
                {
                    detail::Saved<detail::TexEnv, detail::kTexEnvPair> e;
                    e.first = name;
                    e.second.m_Texture = PPtr<Texture>(p.texture);
                    e.second.scale[0] = n[0], e.second.scale[1] = n[1], e.second.offset[0] = n[2], e.second.offset[1] = n[3];
                    saved.m_TexEnvs.Push(Move(e));
                    break;
                }
                case PropertyKind::Int:
                    saved.m_Ints.Push({name, values_.GetInt(p.id)});
                    break;
                case PropertyKind::Float:
                    saved.m_Floats.Push({name, n[0]});
                    break;
                case PropertyKind::Vector:
                {
                    detail::Saved<detail::ColorField, detail::kColorPair> c;
                    c.first = name;
                    for (u32 i = 0; i < 4; i++)
                        c.second.value[i] = n[i];
                    saved.m_Colors.Push(Move(c));
                    break;
                }
                default:
                    break;
                }
            }
            for (const Tag& t : tags_)
                tags.Push({t.name, t.value});
        }
        v.Field("m_ValidKeywords", valid);
        v.Field("m_InvalidKeywords", invalid);
        v.Field("m_LightmapFlags", gi);
        v.Field("m_EnableInstancingVariants", instancing_);
        v.Field("m_DoubleSidedGI", doubleSidedGI_);
        v.Field("m_CustomRenderQueue", customQueue_);
        v.Field("stringTagMap", tags);
        v.Field("disabledShaderPasses", disabledPasses_);
        v.Field("m_SavedProperties", saved);
        if constexpr (V::kReading)
        {
            giFlags_ = MaterialGlobalIlluminationFlags(gi);
            keywords_ = Move(valid);
            for (String& k : invalid)
                keywords_.Push(Move(k));
            tags_.Clear();
            for (detail::TagPair& t : tags)
                tags_.Push({Move(t.first), Move(t.second)});
            values_.Clear();
            for (auto& e : saved.m_TexEnvs)
            {
                i32 id = PropertyToID(e.first);
                values_.SetTexture(id, e.second.m_Texture.Id());
                values_.SetTextureScale(id, Vector2(e.second.scale[0], e.second.scale[1]));
                values_.SetTextureOffset(id, Vector2(e.second.offset[0], e.second.offset[1]));
            }
            for (auto& e : saved.m_Ints)
                values_.SetInt(PropertyToID(e.first), e.second);
            for (auto& e : saved.m_Floats)
                values_.SetFloat(PropertyToID(e.first), e.second);
            for (auto& e : saved.m_Colors)
                values_.SetVector(PropertyToID(e.first), Vector4(e.second.value[0], e.second.value[1], e.second.value[2], e.second.value[3]));
            Changed();
        }
        else
        {
            // Visitors that change references (Instantiate's remapping) reach the textures through the saved list.
            for (auto& e : saved.m_TexEnvs)
                if (const PropertyValue* p = values_.Find(PropertyToID(e.first)); p && p->texture != e.second.m_Texture.Id())
                    values_.SetTexture(p->id, e.second.m_Texture.Id());
        }
    }
}
