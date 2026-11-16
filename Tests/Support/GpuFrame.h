#pragma once

#include <Helix/Core/Containers/Array.h>
#include <Helix/Core/Strings/String.h>
#include <Helix/RHI/CommandList/CommandList.h>
#include <Helix/RHI/Device/Device.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

// The GPU time of frames on a device: each frame bracketed on the device and timed as a whole ("Frame") around what
// the caller draws, with the scopes inside it (a command buffer's samples, so a render graph's passes) as the device's
// GPU timers report them.
namespace helix::engine::testing
{
    struct GpuFrameTimes
    {
        // Each timed frame's GPU time in milliseconds.
        Array<f64> frames;
        // The scopes of the last frame read (the frame first); their names stay valid (literals and sample markers).
        Array<rhi::GpuTiming> scopes;

        f64 Median() const
        {
            if (frames.Empty())
                return 0;
            Array<f64> sorted = frames;
            std::sort(sorted.Data(), sorted.Data() + sorted.Size());
            return sorted[sorted.Size() / 2];
        }
    };

    // Draws frames with `draw` until `count` were timed (at most twice as many tries); empty without GPU timers.
    template <typename Draw>
    GpuFrameTimes TimeGpuFrames(rhi::Device& device, u32 count, Draw&& draw)
    {
        GpuFrameTimes t;
        if (!device.Caps().gpuTimers)
            return t;
        constexpr u32 kMaxScopes = 256;
        static rhi::GpuTiming timings[kMaxScopes];
        for (u32 tries = 0; tries < count * 2 && t.frames.Size() < count; tries++)
        {
            device.BeginFrame();
            rhi::CommandList begin;
            begin.BeginGpuScope("Frame");
            device.Submit(begin);
            draw();
            rhi::CommandList end;
            end.EndGpuScope();
            device.Submit(end);
            device.EndFrame();
            device.WaitIdle();
            u32 n = device.ReadGpuTimings(timings, kMaxScopes);
            if (n == 0 || StringView(timings[0].name) != "Frame")
                continue;
            t.frames.Push(timings[0].durationMs);
            t.scopes.Clear();
            for (u32 i = 0; i < n; i++)
                t.scopes.Push(timings[i]);
        }
        return t;
    }

    // The frame's median GPU time and the scopes of the last frame down to `depth` levels below the frame.
    inline void PrintGpuFrames(StringView label, const GpuFrameTimes& t, u32 depth = 2)
    {
        std::printf("    %.*s: GPU frame median %.3f ms over %u frames\n", int(label.Size()), label.Data(), t.Median(), u32(t.frames.Size()));
        for (const rhi::GpuTiming& s : t.scopes)
            if (s.depth >= 1 && s.depth <= depth)
                std::printf("      %*s%-32s %.3f ms\n", int(s.depth - 1) * 2, "", s.name, s.durationMs);
    }

    // The budget check of a frame's GPU time is on when HELIX_GPU_BUDGET=1 (the hardware run of an exit check; software
    // devices only print).
    inline bool GpuBudgetChecked()
    {
        const char* v = std::getenv("HELIX_GPU_BUDGET");
        return v && v[0] == '1';
    }
}
