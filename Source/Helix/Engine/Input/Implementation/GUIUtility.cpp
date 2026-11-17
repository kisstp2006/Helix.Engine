#include <Helix/Engine/Input/GUIUtility.h>

namespace helix::engine
{
    namespace
    {
        struct Clipboard
        {
            GUIUtility::ClipboardHost host;
            String own;
        };

        Clipboard& C()
        {
            static Clipboard* c = new Clipboard();
            return *c;
        }
    }

    String GUIUtility::systemCopyBuffer()
    {
        Clipboard& c = C();
        return c.host.get ? c.host.get(c.host.user) : c.own;
    }

    void GUIUtility::SetSystemCopyBuffer(StringView value)
    {
        Clipboard& c = C();
        c.own = String(value);
        if (c.host.set)
            c.host.set(c.host.user, value);
    }

    void GUIUtility::SetClipboardHost(const ClipboardHost& host) { C().host = host; }
}
