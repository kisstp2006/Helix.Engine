#pragma once

#include <Helix/Core/Basics/Types.h>

#include <compare>

namespace helix::engine
{
    // The identity of an object: 64 bits, of which the object table uses the low half as a slot and the high half as
    // the slot's generation. Nothing else may be read into the value: not an order of creation, not whether the
    // object is an asset, not a sign. None is 0.
    struct EntityId
    {
        u64 value = 0;

        static constexpr EntityId None() { return EntityId(); }
        static constexpr EntityId FromULong(u64 raw) { return EntityId{raw}; }
        static constexpr u64 ToULong(EntityId id) { return id.value; }

        constexpr bool IsValid() const { return value != 0; }
        constexpr bool operator==(const EntityId&) const = default;
        constexpr auto operator<=>(const EntityId&) const = default;

        // The object table's view of the value.
        constexpr u32 Slot() const { return u32(value); }
        constexpr u32 Generation() const { return u32(value >> 32); }
        static constexpr EntityId Make(u32 slot, u32 generation) { return EntityId{u64(generation) << 32 | slot}; }
    };
}

namespace helix
{
    template <typename T>
    struct Hasher;

    template <>
    struct Hasher<engine::EntityId>
    {
        u64 operator()(engine::EntityId id) const
        {
            u64 x = id.value;
            x ^= x >> 33;
            x *= 0xff51afd7ed558ccdull;
            x ^= x >> 33;
            return x;
        }
    };
}
