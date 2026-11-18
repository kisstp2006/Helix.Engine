#pragma once

#include <Helix/Engine/Math/MathTransfer.h>
#include <Helix/Engine/Object/Object.h>
#include <Helix/Engine/Texture/Texture.h>
#include <Helix/Math/Geometry.h>
#include <Helix/Math/Vector.h>

// Sprite: a rectangle of a texture with a pivot, a size in world units (pixels per unit) and a border for 9-slicing,
// and the mesh it is drawn with (its rectangle, or the part of it that is not transparent). Textures imported as
// sprites carry them (the importer's single or multiple mode); scripts make them with Sprite.Create.
namespace helix::engine
{
    HX_ENUM(Namespace = "HelixEngine")
    enum class SpriteMeshType : i32
    {
        FullRect = 0,
        Tight = 1,
    };

    HX_ENUM(Namespace = "HelixEngine")
    enum class SpritePackingMode : i32
    {
        Tight = 0,
        Rectangle = 1,
    };

    HX_ENUM(Namespace = "HelixEngine")
    enum class SpritePackingRotation : i32
    {
        None = 0,
        FlipHorizontal = 1,
        FlipVertical = 2,
        Rotate180 = 3,
        Any = 15,
    };

    HX_CLASS(Sealed)
    class Sprite final : public Object
    {
        HX_ENGINE_CLASS(Sprite, Object, ClassId::Sprite, ClassNone)

    public:
        Sprite() = default;
        Sprite(const Sprite&) = default;

        // A sprite of a texture's rectangle (pixels), pivot (normalized in the rectangle), pixels per unit, the
        // pixels around it left in its mesh (extrude), its mesh type and border (left, bottom, right, top).
        HX_FUNCTION() static Sprite* Create(Texture2D* texture, Rect rect, Vector2 pivot, f32 pixelsPerUnit = 100, u32 extrude = 0,
                                            SpriteMeshType meshType = SpriteMeshType::Tight);
        HX_FUNCTION() static Sprite* Create(Texture2D* texture, Rect rect, Vector2 pivot, f32 pixelsPerUnit, u32 extrude, SpriteMeshType meshType,
                                            Vector4 border, bool generateFallbackPhysicsShape = false);

        HX_PROPERTY(ReadOnly) Rect rect() const { return rect_; }
        HX_PROPERTY(ReadOnly) Rect textureRect() const { return rect_; }
        HX_PROPERTY(ReadOnly) Vector2 textureRectOffset() const { return Vector2(0, 0); }
        HX_PROPERTY(ReadOnly) Vector4 border() const { return border_; }
        HX_PROPERTY(ReadOnly) f32 pixelsPerUnit() const { return pixelsPerUnit_; }
        // In pixels from the rectangle's bottom left.
        HX_PROPERTY(ReadOnly) Vector2 pivot() const { return pivot_; }
        HX_PROPERTY(ReadOnly) Bounds bounds() const;
        HX_PROPERTY(ReadOnly) bool packed() const { return false; }
        HX_PROPERTY(ReadOnly) SpritePackingMode packingMode() const { return SpritePackingMode::Rectangle; }
        HX_PROPERTY(ReadOnly) SpritePackingRotation packingRotation() const { return SpritePackingRotation::None; }
        // The mesh: positions in world units around the pivot, uvs in the texture, triangles.
        HX_PROPERTY(ReadOnly) Array<Vector2> uv() const { return uv_; }
        HX_PROPERTY(ReadOnly) Array<Vector2> vertices() const { return vertices_; }
        HX_PROPERTY(ReadOnly) Array<u16> triangles() const { return triangles_; }
        HX_PROPERTY(ReadOnly) Texture2D* texture() const { return texture_.Get(); }
        HX_PROPERTY(ReadOnly) Texture2D* associatedAlphaSplitTexture() const { return nullptr; }
        SpriteMeshType meshType() const { return meshType_; }

        // The sprite set up (Create and the importer).
        void Set(Texture2D* texture, Rect rect, Vector2 pivotNormalized, f32 pixelsPerUnit, u32 extrude, SpriteMeshType meshType, Vector4 border);

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("m_Rect", rect_);
            v.Field("m_Pivot", pivot_);
            v.Field("m_Border", border_);
            v.Field("m_PixelsToUnits", pixelsPerUnit_);
            v.Field("m_Texture", texture_);
            if constexpr (V::kReading)
                BuildMesh();
        }

    private:
        void BuildMesh();

        PPtr<Texture2D> texture_;
        Rect rect_ = Rect(0, 0, 0, 0);
        Vector2 pivot_ = Vector2(0, 0);
        Vector4 border_ = Vector4(0, 0, 0, 0);
        f32 pixelsPerUnit_ = 100;
        u32 extrude_ = 0;
        SpriteMeshType meshType_ = SpriteMeshType::FullRect;
        Array<Vector2> uv_, vertices_;
        Array<u16> triangles_;
    };

    // The UVs and sizes the canvas UI draws a sprite with (namespace HelixEngine.Sprites).
    HX_CLASS(Namespace = "HelixEngine.Sprites", Static)
    class DataUtility
    {
    public:
        // The UVs inside the border, and of the whole sprite (xMin, yMin, xMax, yMax).
        HX_FUNCTION() static Vector4 GetInnerUV(const Sprite* sprite);
        HX_FUNCTION() static Vector4 GetOuterUV(const Sprite* sprite);
        // The transparent pixels packing took away (left, bottom, right, top): none.
        HX_FUNCTION() static Vector4 GetPadding(const Sprite* sprite);
        HX_FUNCTION() static Vector2 GetMinSize(const Sprite* sprite);
    };
}
