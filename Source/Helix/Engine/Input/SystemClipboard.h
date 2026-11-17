#pragma once

#include <Helix/Core/Strings/String.h>

// The system clipboard's text, for the engine's modules (the UI's GuiUtility.systemCopyBuffer and text fields). The host
// gives its window's clipboard (the player); without a host the process keeps its own.
namespace helix::engine
{
    class SystemClipboard
    {
    public:
        static String Text();
        static void SetText(StringView value);

        struct Host
        {
            String (*get)(void* user) = nullptr;
            bool (*set)(void* user, StringView text) = nullptr;
            void* user = nullptr;
        };
        // The host's clipboard (none: the process's own).
        static void SetHost(const Host& host);
    };
}
