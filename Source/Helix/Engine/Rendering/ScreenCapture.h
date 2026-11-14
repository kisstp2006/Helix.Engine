#pragma once

#include <Helix/Core/Strings/StringView.h>
#include <Helix/Core/Basics/Tags.h>
#include <Helix/Scripting/Bindings/Tags.h>

// ScreenCapture: a PNG of what the cameras draw. The frame is drawn again into a texture superSize times the screen's
// size at the end of the frame (the swap chain cannot be read back on every backend); the file is written then.
namespace helix::engine
{
    class RenderTexture;

    HX_CLASS(Namespace = "HelixEngine", Static)
    class ScreenCapture
    {
    public:
        // A relative path is relative to the working directory (to persistentDataPath on Android and the Web).
        HX_FUNCTION() static void CaptureScreenshot(StringView filename) { CaptureScreenshot(filename, 1); }
        HX_FUNCTION() static void CaptureScreenshot(StringView filename, i32 superSize);
        // What the cameras drew this frame, into a render texture at the end of the frame.
        HX_FUNCTION() static void CaptureScreenshotIntoRenderTexture(RenderTexture* renderTexture);
        // Captures asked for and not written yet.
        static u32 PendingCaptures();
    };
}
