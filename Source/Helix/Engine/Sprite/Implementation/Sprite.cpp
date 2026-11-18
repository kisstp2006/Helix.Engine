#include <Helix/Engine/Sprite/Sprite.h>

namespace helix::engine
{
    Sprite* Sprite::Create(Texture2D* texture, Rect rect, Vector2 pivot, f32 pixelsPerUnit, u32 extrude, SpriteMeshType meshType)
    {
        return Create(texture, rect, pivot, pixelsPerUnit, extrude, meshType, Vector4(0, 0, 0, 0), false);
    }

    Sprite* Sprite::Create(Texture2D* texture, Rect rect, Vector2 pivot, f32 pixelsPerUnit, u32 extrude, SpriteMeshType meshType, Vector4 border,
                           bool generateFallbackPhysicsShape)
    {
        (void)generateFallbackPhysicsShape;
        if (!texture)
            return nullptr;
        auto* s = NewObject<Sprite>();
        s->Set(texture, rect, pivot, pixelsPerUnit, extrude, meshType, border);
        return s;
    }

    void Sprite::Set(Texture2D* texture, Rect rect, Vector2 pivotNormalized, f32 pixelsPerUnit, u32 extrude, SpriteMeshType meshType, Vector4 border)
    {
        texture_ = PPtr<Texture2D>(texture);
        rect_ = rect;
        pivot_ = Vector2(pivotNormalized.x * rect.width, pivotNormalized.y * rect.height);
        pixelsPerUnit_ = pixelsPerUnit > 0 ? pixelsPerUnit : 100;
        extrude_ = extrude;
        meshType_ = meshType;
        border_ = border;
        BuildMesh();
    }

    void Sprite::BuildMesh()
    {
        uv_.Clear();
        vertices_.Clear();
        triangles_.Clear();
        Texture2D* t = texture_.Get();
        f32 tw = t ? f32(t->width()) : 1, th = t ? f32(t->height()) : 1;
        // The part drawn: the whole rectangle, or (tight) the box of its pixels that are not transparent, with the
        // extruded pixels around it.
        Rect r = rect_;
        if (meshType_ == SpriteMeshType::Tight && t && t->isReadable() && r.width > 0 && r.height > 0)
        {
            i32 x0 = i32(r.x), y0 = i32(r.y), x1 = i32(r.x + r.width), y1 = i32(r.y + r.height);
            i32 minX = x1, minY = y1, maxX = x0 - 1, maxY = y0 - 1;
            for (i32 y = y0; y < y1; y++)
                for (i32 x = x0; x < x1; x++)
                    if (t->GetPixel(x, y).a > 0)
                    {
                        minX = x < minX ? x : minX, minY = y < minY ? y : minY;
                        maxX = x > maxX ? x : maxX, maxY = y > maxY ? y : maxY;
                    }
            if (maxX >= minX)
            {
                i32 e = i32(extrude_);
                minX = minX - e > x0 ? minX - e : x0, minY = minY - e > y0 ? minY - e : y0;
                maxX = maxX + e < x1 - 1 ? maxX + e : x1 - 1, maxY = maxY + e < y1 - 1 ? maxY + e : y1 - 1;
                r = Rect(f32(minX), f32(minY), f32(maxX - minX + 1), f32(maxY - minY + 1));
            }
        }
        const Vector2 corners[4] = {Vector2(r.x, r.y), Vector2(r.x, r.y + r.height), Vector2(r.x + r.width, r.y + r.height), Vector2(r.x + r.width, r.y)};
        for (const Vector2& c : corners)
        {
            uv_.Push(Vector2(c.x / tw, c.y / th));
            vertices_.Push(Vector2((c.x - rect_.x - pivot_.x) / pixelsPerUnit_, (c.y - rect_.y - pivot_.y) / pixelsPerUnit_));
        }
        for (u16 i : {u16(0), u16(1), u16(2), u16(2), u16(3), u16(0)})
            triangles_.Push(i);
    }

    Bounds Sprite::bounds() const
    {
        // The rectangle in world units around the pivot.
        f32 w = rect_.width / pixelsPerUnit_, h = rect_.height / pixelsPerUnit_;
        f32 x = -pivot_.x / pixelsPerUnit_, y = -pivot_.y / pixelsPerUnit_;
        return Bounds(Vector3(x + w * 0.5f, y + h * 0.5f, 0), Vector3(w, h, 0));
    }

    // ------------------------------------------------------------------------------------------------ DataUtility

    namespace
    {
        Vector2 TextureSize(const Sprite* s)
        {
            Texture2D* t = s ? s->texture() : nullptr;
            return t ? Vector2(f32(t->width()), f32(t->height())) : Vector2(1, 1);
        }
    }

    Vector4 DataUtility::GetOuterUV(const Sprite* sprite)
    {
        if (!sprite)
            return Vector4(0, 0, 0, 0);
        Vector2 size = TextureSize(sprite);
        Rect r = sprite->rect();
        return Vector4(r.x / size.x, r.y / size.y, (r.x + r.width) / size.x, (r.y + r.height) / size.y);
    }

    Vector4 DataUtility::GetInnerUV(const Sprite* sprite)
    {
        if (!sprite)
            return Vector4(0, 0, 0, 0);
        Vector2 size = TextureSize(sprite);
        Rect r = sprite->rect();
        Vector4 b = sprite->border();
        return Vector4((r.x + b.x) / size.x, (r.y + b.y) / size.y, (r.x + r.width - b.z) / size.x, (r.y + r.height - b.w) / size.y);
    }

    Vector4 DataUtility::GetPadding(const Sprite* sprite)
    {
        (void)sprite;
        return Vector4(0, 0, 0, 0);
    }

    Vector2 DataUtility::GetMinSize(const Sprite* sprite)
    {
        if (!sprite)
            return Vector2(0, 0);
        Vector4 b = sprite->border();
        return Vector2(b.x + b.z, b.y + b.w);
    }
}
