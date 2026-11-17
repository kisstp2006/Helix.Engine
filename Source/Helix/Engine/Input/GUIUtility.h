#pragma once

#include <Helix/Core/Basics/Tags.h>
#include <Helix/Core/Strings/String.h>

// What the immediate mode GUI shares with the rest: the system clipboard's text. The host gives its window's
// clipboard (the player); without a host the process keeps its own.
namespace helix::engine
{
    HX_CLASS(Namespace = "HelixEngine")
    class GUIUtility
    {
    public:
        HX_PROPERTY() static String systemCopyBuffer();
        static void SetSystemCopyBuffer(StringView value);

        // ---------------------------------------------------------------------------------------- for C++
        struct ClipboardHost
        {
            String (*get)(void* user) = nullptr;
            bool (*set)(void* user, StringView text) = nullptr;
            void* user = nullptr;
        };
        // The host's clipboard (none: the process's own).
        static void SetClipboardHost(const ClipboardHost& host);
    };
}
