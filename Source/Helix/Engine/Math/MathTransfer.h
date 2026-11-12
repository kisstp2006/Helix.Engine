#pragma once

#include <Helix/Math/Color.h>
#include <Helix/Math/Geometry.h>
#include <Helix/Serialization/Transfer/Traits.h>

// Helix.Math's value types in scene, prefab and asset files, in the reference's layout: vectors and quaternions as
// {x, y, z[, w]}, colors as {r, g, b, a}, Color32 as one rgba number, Rect with its version, Bounds as m_Center and
// m_Extent, Matrix4x4 as e<row><column>.
namespace helix::serialization
{
    template <>
    struct TransferTraits<Vector2>
    {
        static constexpr StringView kTypeName = "Vector2f";
        static constexpr bool kFlow = true;
        template <class V>
        static void Transfer(V& v, Vector2& value)
        {
            v.Field("x", value.x);
            v.Field("y", value.y);
        }
    };

    template <>
    struct TransferTraits<Vector3>
    {
        static constexpr StringView kTypeName = "Vector3f";
        static constexpr bool kFlow = true;
        template <class V>
        static void Transfer(V& v, Vector3& value)
        {
            v.Field("x", value.x);
            v.Field("y", value.y);
            v.Field("z", value.z);
        }
    };

    template <>
    struct TransferTraits<Vector4>
    {
        static constexpr StringView kTypeName = "Vector4f";
        static constexpr bool kFlow = true;
        template <class V>
        static void Transfer(V& v, Vector4& value)
        {
            v.Field("x", value.x);
            v.Field("y", value.y);
            v.Field("z", value.z);
            v.Field("w", value.w);
        }
    };

    template <>
    struct TransferTraits<Quaternion>
    {
        static constexpr StringView kTypeName = "Quaternionf";
        static constexpr bool kFlow = true;
        template <class V>
        static void Transfer(V& v, Quaternion& value)
        {
            v.Field("x", value.x);
            v.Field("y", value.y);
            v.Field("z", value.z);
            v.Field("w", value.w);
        }
    };

    template <>
    struct TransferTraits<Vector2Int>
    {
        static constexpr StringView kTypeName = "Vector2Int";
        static constexpr bool kFlow = true;
        template <class V>
        static void Transfer(V& v, Vector2Int& value)
        {
            v.Field("x", value.x);
            v.Field("y", value.y);
        }
    };

    template <>
    struct TransferTraits<Vector3Int>
    {
        static constexpr StringView kTypeName = "Vector3Int";
        static constexpr bool kFlow = true;
        template <class V>
        static void Transfer(V& v, Vector3Int& value)
        {
            v.Field("x", value.x);
            v.Field("y", value.y);
            v.Field("z", value.z);
        }
    };

    template <>
    struct TransferTraits<Color>
    {
        static constexpr StringView kTypeName = "ColorRGBA";
        static constexpr bool kFlow = true;
        template <class V>
        static void Transfer(V& v, Color& value)
        {
            v.Field("r", value.r);
            v.Field("g", value.g);
            v.Field("b", value.b);
            v.Field("a", value.a);
        }
    };

    // One number: r in the low byte, a in the high one.
    template <>
    struct TransferTraits<Color32>
    {
        static constexpr StringView kTypeName = "ColorRGBA";
        static constexpr bool kFlow = true;
        static constexpr i32 kVersion = 2;
        template <class V>
        static void Transfer(V& v, Color32& value)
        {
            u32 rgba = u32(value.r) | (u32(value.g) << 8) | (u32(value.b) << 16) | (u32(value.a) << 24);
            v.Field("rgba", rgba);
            if constexpr (V::kReading)
                value = Color32(u8(rgba), u8(rgba >> 8), u8(rgba >> 16), u8(rgba >> 24));
        }
    };

    template <>
    struct TransferTraits<Rect>
    {
        static constexpr StringView kTypeName = "Rectf";
        static constexpr i32 kVersion = 2;
        template <class V>
        static void Transfer(V& v, Rect& value)
        {
            v.Field("x", value.x);
            v.Field("y", value.y);
            v.Field("width", value.width);
            v.Field("height", value.height);
        }
    };

    template <>
    struct TransferTraits<RectInt>
    {
        static constexpr StringView kTypeName = "RectInt";
        template <class V>
        static void Transfer(V& v, RectInt& value)
        {
            v.Field("x", value.x);
            v.Field("y", value.y);
            v.Field("width", value.width);
            v.Field("height", value.height);
        }
    };

    template <>
    struct TransferTraits<Bounds>
    {
        static constexpr StringView kTypeName = "AABB";
        template <class V>
        static void Transfer(V& v, Bounds& value)
        {
            v.Field("m_Center", value.center);
            v.Field("m_Extent", value.extents);
        }
    };

    template <>
    struct TransferTraits<Matrix4x4>
    {
        static constexpr StringView kTypeName = "Matrix4x4f";
        template <class V>
        static void Transfer(V& v, Matrix4x4& value)
        {
            static constexpr const char* kNames[16] = {"e00", "e10", "e20", "e30", "e01", "e11", "e21", "e31",
                                                       "e02", "e12", "e22", "e32", "e03", "e13", "e23", "e33"};
            // The names by row, the order of the file by row too (e00 e01 e02 e03 e10 ...).
            for (int row = 0; row < 4; row++)
                for (int column = 0; column < 4; column++)
                    v.Field(kNames[row + column * 4], value(row, column));
        }
    };
}
