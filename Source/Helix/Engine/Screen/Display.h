#pragma once

#include <Helix/Core/Basics/Tags.h>
#include <Helix/Core/Containers/Array.h>
#include <Helix/Engine/Event/Event.h>
#include <Helix/Math/Vector.h>
#include <Helix/RHI/Descriptors/Descriptors.h>
#include <Helix/Scripting/Bindings/Interop.h>

// The displays a frame draws to (the reference's Display): display 0 is the host's main window (the screen), the
// others windows the host opens when a display is activated, or windows of a tool (an editor's floating windows)
// the host adds. Each has a swap chain of the device (none: the device window's), its window's size and its client
// area's place on the desktop. Cameras, canvases and UI panels draw to their targetDisplay; input events tell their
// display.
namespace helix::engine
{
    struct DisplayState
    {
        bool active = false;
        rhi::SwapChainHandle swapChain{};
        // The window's client area in pixels, and the size drawn at (the same unless SetRenderingResolution).
        i32 systemWidth = 0, systemHeight = 0;
        i32 renderingWidth = 0, renderingHeight = 0;
        // The client area's top left on the desktop.
        i32 x = 0, y = 0;
    };

    class Displays
    {
    public:
        // The host opens a display's window, or moves and sizes an open one (the size: 0 for the display's own; the
        // place: kNoPlace for the display's); true when the display is active after.
        static constexpr i32 kNoPlace = -2147483647;
        using ActivateFunction = bool (*)(void* user, i32 index, i32 width, i32 height, i32 x, i32 y);
        static void SetHost(ActivateFunction activate, void* user);

        // The displays known (the host reports the monitors and adds its tools' windows); display 0 always is.
        static i32 Count();
        static const DisplayState& Get(i32 index);
        // The host keeps them up to date (a window opened, resized, moved, closed).
        static void SetCount(i32 count);
        static void Set(i32 index, const DisplayState& info);
        // A display of a tool's window, its index (past the monitors).
        static i32 Add(const DisplayState& info);
        static void Remove(i32 index);

        static bool Activate(i32 index, i32 width = 0, i32 height = 0, i32 x = kNoPlace, i32 y = kNoPlace);
        // An active display's window moved and sized by the host (SetParams).
        static bool Place(i32 index, i32 width, i32 height, i32 x, i32 y);

        // The display the renderer draws now (overlay listeners draw its panels); 0 outside the frame's drawing.
        static i32 Current();
        static void SetCurrent(i32 index);
        // The display whose client area holds a desktop point, and the point in it (pixels from its top left); -1
        // for none.
        static i32 At(Vector2 desktop, Vector2& local);

        // Called when displays are added, removed or changed size (Display.onDisplaysUpdated).
        using UpdatedFunction = void (*)();
        static void SetUpdated(UpdatedFunction function);
        static void Changed();
    };

    // The script API's Display (C#'s Display is written over these).
    HX_CLASS(Namespace = "HelixEngine.Internal", Static, Internal)
    class DisplayNative
    {
    public:
        HX_FUNCTION() static i32 Count();
        HX_FUNCTION() static bool Active(i32 index);
        HX_FUNCTION() static i32 SystemWidth(i32 index);
        HX_FUNCTION() static i32 SystemHeight(i32 index);
        HX_FUNCTION() static i32 RenderingWidth(i32 index);
        HX_FUNCTION() static i32 RenderingHeight(i32 index);
        HX_FUNCTION() static bool Activate(i32 index, i32 width, i32 height);
        HX_FUNCTION() static void SetParams(i32 index, i32 width, i32 height, i32 x, i32 y);
        HX_FUNCTION() static void SetRenderingResolution(i32 index, i32 width, i32 height);
        // The display under a desktop point and the point in it (z: the display's index; -1 for none).
        HX_FUNCTION() static Vector3 RelativeMouseAt(Vector3 desktop);
        // Raised when displays are added, removed or change (C#'s Display.onDisplaysUpdated listens).
        HX_EVENT() static Event<>& displaysUpdated();
    };
}
