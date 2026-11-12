#pragma once

// The engine's own classes register when first used; files name classes by ID and name before any code touched
// them, so a lookup registers every built in class first (once).
namespace helix::engine
{
    void RegisterBuiltinClasses();
}
