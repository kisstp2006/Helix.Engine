#pragma once

#include <Helix/Engine/Object/Object.h>

namespace helix::engine
{
    // What the object model's own code reaches inside Object.
    struct ObjectAccess
    {
        // Out of the table: its slot and its class list.
        static void Remove(Object* object);
        // Into the table, as an object of the class.
        static Object* Add(Object* object, u32 klass);
        static bool Findable(const Object* object, bool includeInactive);

        static bool Destroying(const Object* o) { return o->destroying_; }
        static void SetDestroying(Object* o) { o->destroying_ = true; }
        static void SetPooled(Object* o) { o->pooled_ = true; }
    };
}
