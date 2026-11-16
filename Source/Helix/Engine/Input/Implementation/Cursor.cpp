#include <Helix/Engine/Input/Cursor.h>

#include <Helix/Engine/Texture/Texture.h>

namespace helix::engine
{
    namespace
    {
        Cursor::State& Mutable()
        {
            static Cursor::State* state = new Cursor::State();
            return *state;
        }

        u8 Byte(f32 v) { return u8(v <= 0 ? 0 : v >= 1 ? 255 : v * 255.0f + 0.5f); }
    }

    void Cursor::SetCursor(Texture2D* texture, Vector2 hotspot, CursorMode)
    {
        if (!texture || texture->width() <= 0 || texture->height() <= 0)
        {
            SetSystemCursor(platform::SystemCursor::Arrow);
            return;
        }
        // Its pixels from the top row down (a texture's rows go up).
        u32 w = u32(texture->width()), h = u32(texture->height());
        Array<u8> rgba;
        rgba.Reserve(usize(w) * h * 4);
        for (u32 y = h; y-- > 0;)
            for (u32 x = 0; x < w; x++)
            {
                Color c = texture->GetPixel(i32(x), i32(y));
                rgba.Push(Byte(c.r)), rgba.Push(Byte(c.g)), rgba.Push(Byte(c.b)), rgba.Push(Byte(c.a));
            }
        SetCursorPixels(Span<const u8>(rgba.Data(), rgba.Size()), w, h, hotspot);
    }

    void Cursor::SetSystemCursor(platform::SystemCursor shape)
    {
        State& s = Mutable();
        if (!s.image && s.shape == shape)
            return;
        s.image = false;
        s.shape = shape;
        s.pixels.Clear();
        s.version++;
    }

    void Cursor::SetCursorPixels(Span<const u8> rgba, u32 width, u32 height, Vector2 hotspot)
    {
        State& s = Mutable();
        i32 hx = i32(hotspot.x), hy = i32(hotspot.y);
        if (s.image && s.width == width && s.height == height && s.hotspotX == hx && s.hotspotY == hy && s.pixels.Size() == rgba.Size())
        {
            bool same = true;
            for (usize i = 0; i < rgba.Size() && same; i++)
                same = s.pixels[i] == rgba[i];
            if (same)
                return;
        }
        s.image = true;
        s.pixels.Clear();
        for (u8 b : rgba)
            s.pixels.Push(b);
        s.width = width, s.height = height;
        s.hotspotX = hx, s.hotspotY = hy;
        s.version++;
    }

    const Cursor::State& Cursor::Current() { return Mutable(); }

    void Cursor::Reset()
    {
        State& s = Mutable();
        u64 version = s.version;
        s = State();
        s.version = version;
    }
}
