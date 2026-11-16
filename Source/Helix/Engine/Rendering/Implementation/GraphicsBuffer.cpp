#include <Helix/Engine/Rendering/GraphicsBuffer.h>

#include <Helix/Core/Containers/HashMap.h>
#include <Helix/Core/Logging/Log.h>
#include <Helix/Engine/Rendering/Implementation/RenderCore.h>
#include <Helix/RHI/CommandList/CommandList.h>

#include <cstring>
#include <mutex>

namespace helix::engine
{
    namespace
    {
        LogChannel BufferLog{"Engine"};

        struct Registry
        {
            std::mutex mutex;
            HashMap<u64, GraphicsBuffer*> buffers;
            u64 next = 0;
        };

        Registry& Reg()
        {
            static Registry r;
            return r;
        }

        bool Has(GraphicsBufferTarget target, GraphicsBufferTarget flag) { return (i32(target) & i32(flag)) != 0; }

        // The device's uses of a target.
        u32 UsageOf(GraphicsBufferTarget target)
        {
            u32 usage = 0;
            if (Has(target, GraphicsBufferTarget::Vertex))
                usage |= rhi::BufferVertex;
            if (Has(target, GraphicsBufferTarget::Index))
                usage |= rhi::BufferIndex;
            if (Has(target, GraphicsBufferTarget::Constant))
                usage |= rhi::BufferUniform;
            if (Has(target, GraphicsBufferTarget::Structured) || Has(target, GraphicsBufferTarget::Raw) || Has(target, GraphicsBufferTarget::Append) ||
                Has(target, GraphicsBufferTarget::Counter))
                usage |= rhi::BufferStorage;
            if (Has(target, GraphicsBufferTarget::IndirectArguments))
                usage |= rhi::BufferIndirect | rhi::BufferStorage;
            return usage ? usage : rhi::BufferStorage;
        }

        // The most bytes one update command carries.
        constexpr u32 kUpdateChunk = 32768;
    }

    GraphicsBuffer* GraphicsBuffer::Create(GraphicsBufferTarget target, i32 usageFlags, i32 count, i32 stride)
    {
        if (count <= 0 || stride <= 0)
        {
            HX_LOG_ERROR(BufferLog, "GraphicsBuffer: count ({}) and stride ({}) must be greater than 0.", count, stride);
            return nullptr;
        }
        bool fourAligned = Has(target, GraphicsBufferTarget::Structured) || Has(target, GraphicsBufferTarget::Raw) || Has(target, GraphicsBufferTarget::Constant) ||
                           Has(target, GraphicsBufferTarget::IndirectArguments);
        if (fourAligned && stride % 4 != 0)
        {
            HX_LOG_ERROR(BufferLog, "GraphicsBuffer: the stride ({}) of a structured, raw, constant or indirect arguments buffer must be a multiple of 4.", stride);
            return nullptr;
        }
        auto* b = new GraphicsBuffer();
        b->target_ = target;
        b->usage_ = usageFlags;
        b->count_ = count;
        b->stride_ = stride;
        b->data_.Resize(usize(count) * usize(stride));
        std::memset(b->data_.Data(), 0, b->data_.Size());
        Registry& r = Reg();
        std::lock_guard<std::mutex> lock(r.mutex);
        b->id_ = ++r.next;
        r.buffers.Set(b->id_, b);
        return b;
    }

    void GraphicsBuffer::Release(GraphicsBuffer* buffer)
    {
        if (!buffer)
            return;
        {
            Registry& r = Reg();
            std::lock_guard<std::mutex> lock(r.mutex);
            r.buffers.Remove(buffer->id_);
        }
        delete buffer;
    }

    GraphicsBuffer* GraphicsBuffer::Find(u64 id)
    {
        Registry& r = Reg();
        std::lock_guard<std::mutex> lock(r.mutex);
        GraphicsBuffer** b = r.buffers.Find(id);
        return b ? *b : nullptr;
    }

    GraphicsBuffer::~GraphicsBuffer()
    {
        rhi::Device* d = render::Device();
        if (gpu_.IsValid() && d && d == device_)
            d->Destroy(gpu_);
    }

    rhi::BufferHandle GraphicsBuffer::Gpu()
    {
        rhi::Device* d = render::Device();
        if (!d)
            return rhi::BufferHandle();
        if (gpu_.IsValid() && d == device_)
            return gpu_;
        rhi::BufferDesc desc;
        desc.size = data_.Size();
        desc.usage = UsageOf(target_);
        desc.initialData = data_.Data();
        desc.debugName = name_.View();
        gpu_ = d->CreateBuffer(desc);
        device_ = d;
        return gpu_;
    }

    bool GraphicsBuffer::SetData(const void* data, u32 bytes, u32 offset)
    {
        if (u64(offset) + bytes > data_.Size())
        {
            HX_LOG_ERROR(BufferLog, "GraphicsBuffer.SetData: {} bytes at {} do not fit the buffer's {}.", bytes, offset, data_.Size());
            return false;
        }
        if (bytes == 0)
            return true;
        std::memcpy(data_.Data() + offset, data, bytes);
        rhi::Device* d = render::Device();
        if (!d || !gpu_.IsValid() || d != device_)
            return true;
        // The device's buffer takes the bytes in order with the work before (an update per chunk).
        rhi::CommandList cl;
        for (u32 at = 0; at < bytes; at += kUpdateChunk)
        {
            u32 size = bytes - at < kUpdateChunk ? bytes - at : kUpdateChunk;
            cl.UpdateBuffer(gpu_, offset + at, data_.Data() + offset + at, size);
        }
        d->Submit(cl);
        return true;
    }

    bool GraphicsBuffer::RecordData(rhi::CommandList& cl, const void* data, u32 bytes, u32 offset)
    {
        if (u64(offset) + bytes > data_.Size())
        {
            HX_LOG_ERROR(BufferLog, "CommandBuffer.SetBufferData: {} bytes at {} do not fit the buffer's {}.", bytes, offset, data_.Size());
            return false;
        }
        if (bytes == 0)
            return true;
        std::memcpy(data_.Data() + offset, data, bytes);
        rhi::BufferHandle gpu = Gpu();
        if (!gpu.IsValid())
            return false;
        for (u32 at = 0; at < bytes; at += kUpdateChunk)
        {
            u32 size = bytes - at < kUpdateChunk ? bytes - at : kUpdateChunk;
            cl.UpdateBuffer(gpu, offset + at, data_.Data() + offset + at, size);
        }
        return true;
    }

    bool GraphicsBuffer::GetData(void* out, u32 bytes, u32 offset)
    {
        if (u64(offset) + bytes > data_.Size())
        {
            HX_LOG_ERROR(BufferLog, "GraphicsBuffer.GetData: {} bytes at {} do not fit the buffer's {}.", bytes, offset, data_.Size());
            return false;
        }
        rhi::Device* d = render::Device();
        if (d && gpu_.IsValid() && d == device_)
        {
            if (!d->ReadBuffer(gpu_, offset, out, bytes))
                return false;
            std::memcpy(data_.Data() + offset, out, bytes);
            return true;
        }
        std::memcpy(out, data_.Data() + offset, bytes);
        return true;
    }
}
