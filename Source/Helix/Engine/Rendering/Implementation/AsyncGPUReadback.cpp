#include <Helix/Engine/Rendering/AsyncGPUReadback.h>
#include <Helix/Engine/Rendering/Implementation/RenderCore.h>
#include <Helix/Engine/Rendering/Pipeline/CommandBuffer.h>

#include <Helix/Core/Containers/HashMap.h>
#include <Helix/Core/Logging/Log.h>

#include <algorithm>

namespace helix::engine
{
    namespace
    {
        LogChannel ReadbackLog{"Engine"};

        struct Readback
        {
            EntityId texture;
            u32 mip = 0;
            bool afterSubmit = false;
            // A command buffer's request waits until its command ran.
            bool armed = true;
            bool done = false, error = false, delivered = false;
            u64 doneFrame = 0;
            u32 width = 0, height = 0;
            Array<u8> data = Array<u8>();
            AsyncGPUReadback::Callback callback = nullptr;
            void* user = nullptr;
        };

        struct Readbacks
        {
            HashMap<u64, Readback> byId;
            u64 next = 0;
            u64 frame = 0;
        };

        Readbacks& RB()
        {
            static Readbacks r;
            return r;
        }

        void ReadNow(u64 id, Readback& r)
        {
            r.done = true;
            r.doneFrame = RB().frame;
            rhi::Device* device = render::Device();
            RenderTexture* rt = EntityIdToObject<RenderTexture>(r.texture);
            const render::GpuRenderTarget* g = rt && device ? render::RenderTargetOf(*rt) : nullptr;
            if (!g || !g->color.IsValid() || r.mip >= g->mips)
            {
                r.error = true;
                return;
            }
            r.width = Max(1u, g->width >> r.mip);
            r.height = Max(1u, g->height >> r.mip);
            r.data.Resize(usize(r.width) * r.height * rhi::FormatBytesPerPixel(g->colorFormat));
            r.error = !device->ReadTexture(g->color, r.mip, r.data.Data(), r.data.Size());
            (void)id;
        }

        AsyncGPUReadbackRequest Make(Texture* source, i32 mip, AsyncGPUReadback::Callback callback, void* user, bool afterSubmit)
        {
            Readbacks& s = RB();
            u64 id = ++s.next;
            Readback& r = s.byId.Set(id, Readback());
            r.texture = source ? source->GetEntityId() : EntityId();
            r.mip = u32(Max(mip, 0));
            r.afterSubmit = afterSubmit;
            r.armed = !afterSubmit;
            r.callback = callback;
            r.user = user;
            if (!source || !source->Is<RenderTexture>())
            {
                HX_LOG_ERROR(ReadbackLog, "AsyncGPUReadback.Request: {} (render textures are read back).", source ? "not a render texture" : "the source is null");
                r.done = r.error = true;
                r.doneFrame = s.frame;
            }
            return AsyncGPUReadbackRequest(id);
        }

        const Readback* Find(u64 id) { return RB().byId.Find(id); }
    }

    bool AsyncGPUReadbackRequest::done() const { return Find(id_) == nullptr || Find(id_)->done; }
    bool AsyncGPUReadbackRequest::hasError() const { return Find(id_) == nullptr || Find(id_)->error; }
    i32 AsyncGPUReadbackRequest::width() const { return Find(id_) ? i32(Find(id_)->width) : 0; }
    i32 AsyncGPUReadbackRequest::height() const { return Find(id_) ? i32(Find(id_)->height) : 0; }
    i32 AsyncGPUReadbackRequest::layerDataSize() const { return Find(id_) ? i32(Find(id_)->data.Size()) : 0; }

    Span<const u8> AsyncGPUReadbackRequest::GetData(i32 layer) const
    {
        const Readback* r = Find(id_);
        if (!r || !r->done || r->error || layer != 0)
            return {};
        return Span<const u8>(r->data.Data(), r->data.Size());
    }

    void AsyncGPUReadbackRequest::WaitForCompletion()
    {
        Readback* r = RB().byId.Find(id_);
        if (!r || r->done)
            return;
        ReadNow(id_, *r);
    }

    AsyncGPUReadbackRequest AsyncGPUReadback::Request(Texture* source, i32 mipIndex, Callback callback, void* user)
    {
        return Make(source, mipIndex, callback, user, false);
    }

    void AsyncGPUReadback::WaitAllRequests()
    {
        for (auto e : RB().byId)
            if (!e.value.done)
                ReadNow(e.key, e.value);
    }

    namespace render
    {
        AsyncGPUReadbackRequest RequestReadbackAfterSubmit(Texture* source, i32 mipIndex, AsyncGPUReadback::Callback callback, void* user)
        {
            return Make(source, mipIndex, callback, user, true);
        }

        void ReadPendingReadbacks(bool afterSubmit)
        {
            for (auto e : RB().byId)
                if (!e.value.done && e.value.armed && (!afterSubmit || e.value.afterSubmit))
                    ReadNow(e.key, e.value);
        }

        void ArmReadback(u64 id)
        {
            if (Readback* r = RB().byId.Find(id))
                r->armed = true;
        }

        void DeliverReadbacks(u64 frame)
        {
            Readbacks& s = RB();
            s.frame = frame;
            // Callbacks may make requests: walk the ids first.
            Array<u64> ready, old;
            for (auto e : s.byId)
            {
                if (e.value.done && !e.value.delivered && e.value.doneFrame < frame)
                    ready.Push(e.key);
                else if (e.value.delivered && e.value.doneFrame + 2 < frame)
                    old.Push(e.key);
            }
            // In the order they were requested.
            std::sort(ready.Data(), ready.Data() + ready.Size());
            for (u64 id : ready)
                if (Readback* r = s.byId.Find(id))
                {
                    r->delivered = true;
                    if (r->callback)
                        r->callback(r->user, AsyncGPUReadbackRequest(id));
                }
            for (u64 id : old)
                s.byId.Remove(id);
        }
    }

    namespace
    {
        // A C# callback: an unmanaged function of (user, id), called once.
        struct ManagedCallback
        {
            u64 function, user;
        };

        void CallManaged(void* user, const AsyncGPUReadbackRequest& request)
        {
            auto* m = static_cast<ManagedCallback*>(user);
            reinterpret_cast<void (*)(u64, u64)>(m->function)(m->user, request.Id());
            delete m;
        }
    }

    u64 ReadbackNative::Request(Texture* source, i32 mipIndex, u64 callback, u64 user)
    {
        return AsyncGPUReadback::Request(source, mipIndex, callback ? CallManaged : nullptr, callback ? new ManagedCallback{callback, user} : nullptr).Id();
    }

    u64 ReadbackNative::RequestInBuffer(u64 buffer, Texture* source, i32 mipIndex, u64 callback, u64 user)
    {
        CommandBuffer* b = CommandBuffer::Find(buffer);
        if (!b)
            return 0;
        return b->RequestAsyncReadback(source, mipIndex, callback ? CallManaged : nullptr, callback ? new ManagedCallback{callback, user} : nullptr).Id();
    }
}
