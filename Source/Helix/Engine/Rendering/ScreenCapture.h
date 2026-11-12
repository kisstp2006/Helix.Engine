#pragma once

#include <Helix/Core/Strings/StringView.h>

// ScreenCapture: a PNG of what the cameras draw. The frame is drawn again into a texture superSize times the screen's
// size at the end of the frame (the swap chain cannot be read back on every backend); the file is written then.
namespace helix::engine
{
    class ScreenCapture
    {
    public:
        // A relative path is relative to the working directory (to persistentDataPath on Android and the Web).
        static void CaptureScreenshot(StringView filename, i32 superSize = 1);
        // Captures asked for and not written yet.
        static u32 PendingCaptures();
    };
}
