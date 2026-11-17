#pragma once

#include <Helix/Core/Basics/Tags.h>
#include <Helix/Core/Strings/String.h>

// A 128 bit hash (C#: its two halves private, its methods the C# side's). As text: 32 hex digits, the first half's
// bytes from the lowest, then the second's.
namespace helix::engine
{
    HX_STRUCT(Namespace = "HelixEngine")
    struct Hash128
    {
        HX_FIELD(Name = "u64_0", Private) u64 u64_0 = 0;
        HX_FIELD(Name = "u64_1", Private) u64 u64_1 = 0;

        Hash128() = default;
        Hash128(u64 a, u64 b) : u64_0(a), u64_1(b) {}
        bool isValid() const { return u64_0 != 0 || u64_1 != 0; }
        bool operator==(const Hash128& o) const { return u64_0 == o.u64_0 && u64_1 == o.u64_1; }
        String ToString() const;
        // The hash of 32 hex digits (false: other text; out is zero then).
        static bool TryParse(StringView text, Hash128& out);
    };
}
