#include <Helix/Engine/Input/SystemClipboard.h>

namespace helix::engine
{
    namespace
    {
        struct Clipboard
        {
            SystemClipboard::Host host;
            String own;
        };

        Clipboard& C()
        {
            static Clipboard* c = new Clipboard();
            return *c;
        }
    }

    String SystemClipboard::Text()
    {
        Clipboard& c = C();
        return c.host.get ? c.host.get(c.host.user) : c.own;
    }

    void SystemClipboard::SetText(StringView value)
    {
        Clipboard& c = C();
        c.own = String(value);
        if (c.host.set)
            c.host.set(c.host.user, value);
    }

    void SystemClipboard::SetHost(const Host& host) { C().host = host; }
}
