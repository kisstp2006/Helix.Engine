#include <Helix/Engine/Math/Hash128.h>

namespace helix::engine
{
    String Hash128::ToString() const
    {
        static const char kDigits[] = "0123456789abcdef";
        char text[32];
        u64 halves[2] = {u64_0, u64_1};
        for (u32 i = 0; i < 16; i++)
        {
            u8 b = u8(halves[i / 8] >> ((i % 8) * 8));
            text[i * 2] = kDigits[b >> 4];
            text[i * 2 + 1] = kDigits[b & 15];
        }
        return String(StringView(text, 32));
    }

    bool Hash128::TryParse(StringView text, Hash128& out)
    {
        out = Hash128();
        StringView t = text.Trimmed();
        if (t.Size() != 32)
            return false;
        u64 halves[2] = {0, 0};
        for (u32 i = 0; i < 32; i++)
        {
            char c = t[i];
            u32 v = c >= '0' && c <= '9' ? u32(c - '0') : c >= 'a' && c <= 'f' ? u32(c - 'a' + 10) : c >= 'A' && c <= 'F' ? u32(c - 'A' + 10) : 16u;
            if (v > 15)
                return false;
            u32 byte = i / 2;
            u32 shift = (byte % 8) * 8 + (i % 2 == 0 ? 4 : 0);
            halves[byte / 8] |= u64(v) << shift;
        }
        out = Hash128(halves[0], halves[1]);
        return true;
    }
}
