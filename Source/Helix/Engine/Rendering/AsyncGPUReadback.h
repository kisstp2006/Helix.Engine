#pragma once

#include <Helix/Engine/Texture/RenderTexture.h>

// AsyncGPUReadback: a render texture's texels read back from the GPU without waiting for it. A request made during a
// frame is read once the frame's commands are submitted (its end, or right after a command buffer's submit for
// CommandBuffer::RequestAsyncReadback) and its callback runs at the start of the next frame (callbacks in request order); the data stays readable
// until the frame after that. Requests of other textures fail (hasError).
namespace helix::engine
{
    class AsyncGPUReadbackRequest
    {
    public:
        AsyncGPUReadbackRequest() = default;
        explicit AsyncGPUReadbackRequest(u64 id) : id_(id) {}

        bool done() const;
        bool hasError() const;
        i32 width() const;
        i32 height() const;
        i32 depth() const { return 1; }
        i32 layerCount() const { return 1; }
        i32 layerDataSize() const;
        // The texels of a layer, rows top first (empty until done, or on error).
        Span<const u8> GetData(i32 layer = 0) const;
        // Reads now when it is still waiting (and runs its callback).
        void WaitForCompletion();
        void Update() {}
        bool forcePlayerLoopUpdate = false;
        u64 Id() const { return id_; }

    private:
        u64 id_ = 0;
    };

    class AsyncGPUReadback
    {
    public:
        using Callback = void (*)(void* user, const AsyncGPUReadbackRequest& request);
        static AsyncGPUReadbackRequest Request(Texture* source, i32 mipIndex = 0, Callback callback = nullptr, void* user = nullptr);
        static void WaitAllRequests();
    };

    namespace render
    {
        // A request read after the next submit of a command list that asked for it.
        AsyncGPUReadbackRequest RequestReadbackAfterSubmit(Texture* source, i32 mipIndex, AsyncGPUReadback::Callback callback, void* user);
        // The command that asked for a request ran: it is read after this submit.
        void ArmReadback(u64 id);
        // Reads the requests that wait for a submit (`afterSubmit`) or for the frame's end.
        void ReadPendingReadbacks(bool afterSubmit);
        // The frame's start: the callbacks of the requests read in the frame before; old data freed.
        void DeliverReadbacks(u64 frame);
    }

    // What C#'s AsyncGPUReadback calls (internal): requests by id, callbacks to an unmanaged function (user, id).
    HX_CLASS(Namespace = "HelixEngine.Internal", Static, Internal)
    class ReadbackNative
    {
    public:
        HX_FUNCTION() static u64 Request(Texture* source, i32 mipIndex, u64 callback, u64 user);
        HX_FUNCTION() static u64 RequestInBuffer(u64 buffer, Texture* source, i32 mipIndex, u64 callback, u64 user);
        HX_FUNCTION() static bool Done(u64 id) { return AsyncGPUReadbackRequest(id).done(); }
        HX_FUNCTION() static bool HasError(u64 id) { return AsyncGPUReadbackRequest(id).hasError(); }
        HX_FUNCTION() static i32 Width(u64 id) { return AsyncGPUReadbackRequest(id).width(); }
        HX_FUNCTION() static i32 Height(u64 id) { return AsyncGPUReadbackRequest(id).height(); }
        HX_FUNCTION() static i32 DataSize(u64 id) { return AsyncGPUReadbackRequest(id).layerDataSize(); }
        // The data's address (0 until done); valid until the frame after the callback's.
        HX_FUNCTION() static u64 Data(u64 id)
        {
            Span<const u8> d = AsyncGPUReadbackRequest(id).GetData();
            return d.Empty() ? 0 : u64(reinterpret_cast<uintptr_t>(d.Data()));
        }
        HX_FUNCTION() static void WaitForCompletion(u64 id) { AsyncGPUReadbackRequest(id).WaitForCompletion(); }
        HX_FUNCTION() static void WaitAllRequests() { AsyncGPUReadback::WaitAllRequests(); }
    };
}
