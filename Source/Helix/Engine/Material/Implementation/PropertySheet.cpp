#include <Helix/Engine/Material/PropertySheet.h>

#include <Helix/Core/Logging/Log.h>
#include <Helix/Core/Strings/Format.h>

#include <cstring>

namespace helix::engine
{
    namespace
    {
        LogChannel PropertyLog{"Engine"};

        struct Names
        {
            HashMap<String, i32> ids;
            Array<String> names = Array<String>();
        };

        Names& PropertyNames()
        {
            static Names n;
            return n;
        }
    }

    i32 PropertyToID(StringView name)
    {
        Names& n = PropertyNames();
        String key(name);
        if (const i32* id = n.ids.Find(key))
            return *id;
        // Ids start at 1: 0 stays "no property".
        i32 id = i32(n.names.Size()) + 1;
        n.names.Push(key);
        n.ids.Set(Move(key), id);
        return id;
    }

    i32 FindPropertyID(StringView name)
    {
        const i32* id = PropertyNames().ids.Find(String(name));
        return id ? *id : 0;
    }

    StringView PropertyName(i32 id)
    {
        Names& n = PropertyNames();
        return id >= 1 && usize(id) <= n.names.Size() ? StringView(n.names[usize(id) - 1]) : StringView();
    }

    u32 PropertySheet::Width(PropertyKind kind)
    {
        switch (kind)
        {
        case PropertyKind::Vector:
        case PropertyKind::Texture:
        case PropertyKind::VectorArray:
            return 4;
        case PropertyKind::Matrix:
        case PropertyKind::MatrixArray:
            return 16;
        default:
            return 1;
        }
    }

    const PropertyValue* PropertySheet::Find(i32 id) const
    {
        const u32* i = index_.Find(id);
        return i ? &values_[*i] : nullptr;
    }

    bool PropertySheet::Has(i32 id, PropertyKind kind) const
    {
        const PropertyValue* v = Find(id);
        return v && v->kind == kind;
    }

    PropertyValue& PropertySheet::Slot(i32 id, PropertyKind kind, u32 count)
    {
        version_++;
        if (const u32* i = index_.Find(id))
        {
            PropertyValue& v = values_[*i];
            // Same kind: the pool space is reused (arrays keep their first size).
            if (v.kind == kind)
                return v;
            // Another kind: the value starts over at the end of the pool.
            v.kind = kind;
            v.count = count;
            v.offset = u32(numbers_.Size());
            numbers_.Resize(numbers_.Size() + usize(Width(kind)) * count);
            std::memset(numbers_.Data() + v.offset, 0, usize(Width(kind)) * count * sizeof(f32));
            return v;
        }
        PropertyValue v;
        v.id = id;
        v.kind = kind;
        v.count = count;
        v.offset = u32(numbers_.Size());
        numbers_.Resize(numbers_.Size() + usize(Width(kind)) * count);
        std::memset(numbers_.Data() + v.offset, 0, usize(Width(kind)) * count * sizeof(f32));
        if (kind == PropertyKind::Texture)
        {
            numbers_[v.offset] = 1;
            numbers_[v.offset + 1] = 1;
        }
        index_.Set(id, u32(values_.Size()));
        values_.Push(v);
        return values_[values_.Size() - 1];
    }

    void PropertySheet::SetFloat(i32 id, f32 value) { numbers_[Slot(id, PropertyKind::Float, 1).offset] = value; }

    void PropertySheet::SetInt(i32 id, i32 value)
    {
        PropertyValue& v = Slot(id, PropertyKind::Int, 1);
        std::memcpy(&numbers_[v.offset], &value, 4);
    }

    void PropertySheet::SetVector(i32 id, const Vector4& value) { std::memcpy(&numbers_[Slot(id, PropertyKind::Vector, 1).offset], &value.x, 16); }

    void PropertySheet::SetMatrix(i32 id, const Matrix4x4& value) { std::memcpy(&numbers_[Slot(id, PropertyKind::Matrix, 1).offset], &value.m00, 64); }

    void PropertySheet::SetTexture(i32 id, EntityId texture) { Slot(id, PropertyKind::Texture, 1).texture = texture; }

    void PropertySheet::SetTextureScale(i32 id, Vector2 scale)
    {
        PropertyValue& v = Slot(id, PropertyKind::Texture, 1);
        numbers_[v.offset] = scale.x;
        numbers_[v.offset + 1] = scale.y;
    }

    void PropertySheet::SetTextureOffset(i32 id, Vector2 offset)
    {
        PropertyValue& v = Slot(id, PropertyKind::Texture, 1);
        numbers_[v.offset + 2] = offset.x;
        numbers_[v.offset + 3] = offset.y;
    }

    namespace
    {
        void ArrayCut(i32 id, usize given, u32 kept)
        {
            if (given > kept)
                HX_LOG_WARNING(PropertyLog, "Property ({}) is an array of {}; {} values were given, the rest is dropped.", PropertyName(id), kept, given);
        }
    }

    void PropertySheet::SetFloatArray(i32 id, Span<const f32> values)
    {
        if (values.Empty())
            return;
        PropertyValue& v = Slot(id, PropertyKind::FloatArray, u32(values.Size()));
        usize n = values.Size() < v.count ? values.Size() : v.count;
        std::memcpy(&numbers_[v.offset], values.Data(), n * 4);
        ArrayCut(id, values.Size(), v.count);
    }

    void PropertySheet::SetVectorArray(i32 id, Span<const Vector4> values)
    {
        if (values.Empty())
            return;
        PropertyValue& v = Slot(id, PropertyKind::VectorArray, u32(values.Size()));
        usize n = values.Size() < v.count ? values.Size() : v.count;
        std::memcpy(&numbers_[v.offset], values.Data(), n * 16);
        ArrayCut(id, values.Size(), v.count);
    }

    void PropertySheet::SetMatrixArray(i32 id, Span<const Matrix4x4> values)
    {
        if (values.Empty())
            return;
        PropertyValue& v = Slot(id, PropertyKind::MatrixArray, u32(values.Size()));
        usize n = values.Size() < v.count ? values.Size() : v.count;
        std::memcpy(&numbers_[v.offset], values.Data(), n * 64);
        ArrayCut(id, values.Size(), v.count);
    }

    f32 PropertySheet::GetFloat(i32 id, f32 fallback) const
    {
        const PropertyValue* v = Find(id);
        if (!v)
            return fallback;
        if (v->kind == PropertyKind::Int)
        {
            i32 i;
            std::memcpy(&i, &numbers_[v->offset], 4);
            return f32(i);
        }
        return v->kind == PropertyKind::Texture ? fallback : numbers_[v->offset];
    }

    i32 PropertySheet::GetInt(i32 id, i32 fallback) const
    {
        const PropertyValue* v = Find(id);
        if (!v)
            return fallback;
        if (v->kind == PropertyKind::Int)
        {
            i32 i;
            std::memcpy(&i, &numbers_[v->offset], 4);
            return i;
        }
        return v->kind == PropertyKind::Texture ? fallback : i32(numbers_[v->offset]);
    }

    Vector4 PropertySheet::GetVector(i32 id, const Vector4& fallback) const
    {
        const PropertyValue* v = Find(id);
        if (!v || (v->kind != PropertyKind::Vector && v->kind != PropertyKind::VectorArray))
            return fallback;
        Vector4 out;
        std::memcpy(&out.x, &numbers_[v->offset], 16);
        return out;
    }

    Color PropertySheet::GetColor(i32 id, const Color& fallback) const
    {
        Vector4 v = GetVector(id, Vector4(fallback.r, fallback.g, fallback.b, fallback.a));
        return Color(v.x, v.y, v.z, v.w);
    }

    Matrix4x4 PropertySheet::GetMatrix(i32 id) const
    {
        const PropertyValue* v = Find(id);
        Matrix4x4 out;
        if (v && (v->kind == PropertyKind::Matrix || v->kind == PropertyKind::MatrixArray))
            std::memcpy(&out.m00, &numbers_[v->offset], 64);
        return out;
    }

    EntityId PropertySheet::GetTexture(i32 id) const
    {
        const PropertyValue* v = Find(id);
        return v && v->kind == PropertyKind::Texture ? v->texture : EntityId();
    }

    Vector4 PropertySheet::GetTextureScaleOffset(i32 id) const
    {
        const PropertyValue* v = Find(id);
        if (!v || v->kind != PropertyKind::Texture)
            return Vector4(1, 1, 0, 0);
        Vector4 out;
        std::memcpy(&out.x, &numbers_[v->offset], 16);
        return out;
    }

    void PropertySheet::GetFloatArray(i32 id, Array<f32>& out) const
    {
        out.Clear();
        if (const PropertyValue* v = Find(id); v && v->kind == PropertyKind::FloatArray)
            for (u32 i = 0; i < v->count; i++)
                out.Push(numbers_[v->offset + i]);
    }

    void PropertySheet::GetVectorArray(i32 id, Array<Vector4>& out) const
    {
        out.Clear();
        if (const PropertyValue* v = Find(id); v && v->kind == PropertyKind::VectorArray)
        {
            out.Resize(v->count);
            std::memcpy(&out.Data()->x, &numbers_[v->offset], usize(v->count) * 16);
        }
    }

    void PropertySheet::GetMatrixArray(i32 id, Array<Matrix4x4>& out) const
    {
        out.Clear();
        if (const PropertyValue* v = Find(id); v && v->kind == PropertyKind::MatrixArray)
        {
            out.Resize(v->count);
            std::memcpy(&out.Data()->m00, &numbers_[v->offset], usize(v->count) * 64);
        }
    }

    void PropertySheet::Remove(i32 id)
    {
        const u32* i = index_.Find(id);
        if (!i)
            return;
        u32 at = *i;
        index_.Remove(id);
        values_.RemoveAt(at);
        for (u32 k = at; k < values_.Size(); k++)
            index_.Set(values_[k].id, k);
        version_++;
    }

    void PropertySheet::Clear()
    {
        values_.Clear();
        index_ = HashMap<i32, u32>();
        numbers_.Clear();
        version_++;
    }

    void CopyPropertyValue(PropertySheet& to, const PropertySheet& from, const PropertyValue& v)
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
}
