#pragma once

#include <Helix/Core/Basics/Span.h>
#include <Helix/Core/Containers/Array.h>
#include <Helix/Core/Containers/HashMap.h>
#include <Helix/Core/Strings/String.h>
#include <Helix/Engine/Object/EntityId.h>
#include <Helix/Math/Color.h>
#include <Helix/Math/Matrix4x4.h>
#include <Helix/Math/Vector.h>

// Property ids and the table of property values that materials, MaterialPropertyBlocks and the global shader
// properties share: one entry per id, numbers in one pool, textures as ids with their tiling.
namespace helix::engine
{
    // A unique id per property name for the run of the program (Shader.PropertyToID).
    i32 PropertyToID(StringView name);
    // The id of a name PropertyToID gave one to, 0 otherwise (no new id).
    i32 FindPropertyID(StringView name);
    // The name of an id; empty for ids PropertyToID never gave.
    StringView PropertyName(i32 id);

    enum class PropertyKind : u8
    {
        Float,
        Int,
        Vector,
        Matrix,
        // A texture (an object id) and its tiling: scale xy, offset zw.
        Texture,
        FloatArray,
        VectorArray,
        MatrixArray,
    };

    struct PropertyValue
    {
        i32 id = 0;
        PropertyKind kind = PropertyKind::Float;
        // Floats in the pool from `offset`: 1 per float or int (an int keeps its bits), 4 per vector or tiling, 16 per
        // matrix, times `count` for arrays.
        u32 offset = 0;
        u32 count = 1;
        EntityId texture;
    };

    class PropertySheet;
    // One value copied from a sheet into another, whatever its kind.
    void CopyPropertyValue(PropertySheet& to, const PropertySheet& from, const PropertyValue& value);

    class PropertySheet
    {
    public:
        PropertySheet() = default;

        void SetFloat(i32 id, f32 value);
        void SetInt(i32 id, i32 value);
        void SetVector(i32 id, const Vector4& value);
        void SetColor(i32 id, const Color& value) { SetVector(id, Vector4(value.r, value.g, value.b, value.a)); }
        void SetMatrix(i32 id, const Matrix4x4& value);
        void SetTexture(i32 id, EntityId texture);
        void SetTextureScale(i32 id, Vector2 scale);
        void SetTextureOffset(i32 id, Vector2 offset);
        // Arrays keep the size of their first set; longer ones are cut (with a warning), shorter ones fill the front.
        void SetFloatArray(i32 id, Span<const f32> values);
        void SetVectorArray(i32 id, Span<const Vector4> values);
        void SetMatrixArray(i32 id, Span<const Matrix4x4> values);

        f32 GetFloat(i32 id, f32 fallback = 0) const;
        i32 GetInt(i32 id, i32 fallback = 0) const;
        Vector4 GetVector(i32 id, const Vector4& fallback = Vector4()) const;
        Color GetColor(i32 id, const Color& fallback = Color()) const;
        Matrix4x4 GetMatrix(i32 id) const;
        EntityId GetTexture(i32 id) const;
        // Scale xy, offset zw; (1, 1, 0, 0) without a value.
        Vector4 GetTextureScaleOffset(i32 id) const;
        void GetFloatArray(i32 id, Array<f32>& out) const;
        void GetVectorArray(i32 id, Array<Vector4>& out) const;
        void GetMatrixArray(i32 id, Array<Matrix4x4>& out) const;

        bool Has(i32 id) const { return index_.Find(id) != nullptr; }
        bool Has(i32 id, PropertyKind kind) const;
        const PropertyValue* Find(i32 id) const;
        Span<const f32> Numbers(const PropertyValue& v) const { return Span<const f32>(numbers_.Data() + v.offset, Width(v.kind) * v.count); }
        Span<const PropertyValue> Values() const { return Span<const PropertyValue>(values_.Data(), values_.Size()); }
        bool Empty() const { return values_.Empty(); }
        void Remove(i32 id);
        void Clear();
        // Bumped by every change.
        u32 Version() const { return version_; }

        // Floats of one element of a kind.
        static u32 Width(PropertyKind kind);

    private:
        PropertyValue& Slot(i32 id, PropertyKind kind, u32 count);

        Array<PropertyValue> values_ = Array<PropertyValue>();
        HashMap<i32, u32> index_;
        Array<f32> numbers_ = Array<f32>();
        u32 version_ = 0;
    };
}
