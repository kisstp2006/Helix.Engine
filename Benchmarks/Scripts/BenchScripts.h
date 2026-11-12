#pragma once

#include <Helix/Engine/Script/GameBehaviour.h>

// The benchmarks' C++ script (a game module of its own, EngineBench).
namespace helix::engine::bench
{
    HX_CLASS(Namespace = "EngineBench")
    class Spinner : public GameBehaviour
    {
    public:
        static inline u64 updates = 0;
        void Update() { updates++; }
    };
}
