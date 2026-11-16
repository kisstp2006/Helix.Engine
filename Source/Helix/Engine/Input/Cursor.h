#pragma once

#include <Helix/Core/Basics/Tags.h>
#include <Helix/Core/Containers/Array.h>
#include <Helix/Math/Vector.h>
#include <Helix/Platform/Window/Window.h>

// The pointer's look over the player's window. Scripts set an image (Cursor.SetCursor), the UI also a system shape;
// the host applies the latest at the end of the frame. The image's pixels are taken when it is set (its rows from the
// top), so the texture may change or go afterwards.
namespace helix::engine
{
    class Texture2D;

    HX_ENUM(Namespace = "HelixEngine")
    enum class CursorMode : i32
    {
        // The system draws it.
        Auto = 0,
        // Drawn by the engine (the system's cursor here too: no software cursor is drawn yet).
        ForceSoftware = 1,
    };

    HX_CLASS(Namespace = "HelixEngine")
    class Cursor
    {
    public:
        // The texture as the pointer, the hotspot in its pixels from the top left; null for the default arrow.
        HX_FUNCTION() static void SetCursor(Texture2D* texture, Vector2 hotspot, CursorMode cursorMode);

        // ---------------------------------------------------------------------------------------- for C++
        // A system shape (the UI's cursor keywords); the arrow is the default.
        static void SetSystemCursor(platform::SystemCursor shape);
        // An image (RGBA8, rows from the top) with its hotspot.
        static void SetCursorPixels(Span<const u8> rgba, u32 width, u32 height, Vector2 hotspot);

        struct State
        {
            // Counts the changes (the host applies one it has not yet).
            u64 version = 0;
            bool image = false;
            platform::SystemCursor shape = platform::SystemCursor::Arrow;
            Array<u8> pixels;
            u32 width = 0, height = 0;
            i32 hotspotX = 0, hotspotY = 0;
        };
        static const State& Current();
        // The default arrow, nothing to apply (the host's shutdown, tests).
        static void Reset();
    };
}
