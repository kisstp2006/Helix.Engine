#pragma once

#include <Helix/Core/Containers/Array.h>
#include <Helix/Core/Strings/String.h>
#include <Helix/RHI/Descriptors/Descriptors.h>
#include <Helix/Core/Basics/Tags.h>

// GraphicsBuffer: a buffer of the GPU (structured, raw, vertex, index, indirect arguments, constants) that shaders,
// compute shaders and draws read and write. It keeps a copy of what was set; the device's buffer is made on first use
// and takes every SetData; GetData reads the device's buffer back (waiting for the work before it). C# reaches it by
// id (GraphicsBuffer and ComputeBuffer in C#).
namespace helix::rhi
{
    class CommandList;
}

namespace helix::engine
{
    // What a buffer is for (the reference's GraphicsBuffer.Target values).
    HX_ENUM(Namespace = "HelixEngine.Internal", Internal, Attributes = "[System.Flags]")
    enum class GraphicsBufferTarget : i32
    {
        Vertex = 1,
        Index = 2,
        CopySource = 4,
        CopyDestination = 8,
        Structured = 16,
        Raw = 32,
        Append = 64,
        Counter = 128,
        IndirectArguments = 256,
        Constant = 512,
    };

    class GraphicsBuffer
    {
    public:
        // Null (and an error) for a count or stride below 1, or a stride that is no multiple of 4 for structured, raw
        // and constant buffers (the reference's rules).
        static GraphicsBuffer* Create(GraphicsBufferTarget target, i32 usageFlags, i32 count, i32 stride);
        // Releases the device's buffer and the object.
        static void Release(GraphicsBuffer* buffer);
        static GraphicsBuffer* Find(u64 id);
        u64 Id() const { return id_; }

        GraphicsBufferTarget target() const { return target_; }
        i32 usageFlags() const { return usage_; }
        i32 count() const { return count_; }
        i32 stride() const { return stride_; }
        const String& name() const { return name_; }
        void SetName(StringView name) { name_ = String(name); }

        // Bytes at a byte offset; false (and an error) when they do not fit.
        bool SetData(const void* data, u32 bytes, u32 offset);
        // The same into a command list (in its order with the work recorded there); the device's buffer is made first.
        bool RecordData(rhi::CommandList& cl, const void* data, u32 bytes, u32 offset);
        // Bytes at a byte offset from the device's buffer (its copy without a device).
        bool GetData(void* out, u32 bytes, u32 offset);
        // The counter of an append or counter buffer (kept as a value; the devices of this milestone have no counters).
        void SetCounterValue(u32 value) { counter_ = value; }
        u32 counterValue() const { return counter_; }

        // The device's buffer, made from the copy when there is none (invalid without a device).
        rhi::BufferHandle Gpu();
        u32 sizeInBytes() const { return u32(count_) * u32(stride_); }

    private:
        GraphicsBuffer() = default;
        ~GraphicsBuffer();

        u64 id_ = 0;
        GraphicsBufferTarget target_ = GraphicsBufferTarget::Structured;
        i32 usage_ = 0, count_ = 0, stride_ = 0;
        u32 counter_ = 0;
        String name_ = String();
        Array<u8> data_ = Array<u8>();
        rhi::BufferHandle gpu_;
        // The device the buffer was made on (made again on another).
        void* device_ = nullptr;
    };

    // What C#'s GraphicsBuffer and ComputeBuffer call (internal): buffers by id, data through pinned addresses.
    HX_CLASS(Namespace = "HelixEngine.Internal", Static, Internal)
    class GraphicsBufferNative
    {
    public:
        // 0 (and an error) when the buffer cannot be made.
        HX_FUNCTION() static u64 Create(i32 target, i32 usageFlags, i32 count, i32 stride)
        {
            GraphicsBuffer* b = GraphicsBuffer::Create(GraphicsBufferTarget(target), usageFlags, count, stride);
            return b ? b->Id() : 0;
        }
        HX_FUNCTION() static void Release(u64 id) { GraphicsBuffer::Release(GraphicsBuffer::Find(id)); }
        HX_FUNCTION() static bool IsValid(u64 id) { return GraphicsBuffer::Find(id) != nullptr; }
        HX_FUNCTION() static i32 Count(u64 id)
        {
            GraphicsBuffer* b = GraphicsBuffer::Find(id);
            return b ? b->count() : 0;
        }
        HX_FUNCTION() static i32 Stride(u64 id)
        {
            GraphicsBuffer* b = GraphicsBuffer::Find(id);
            return b ? b->stride() : 0;
        }
        HX_FUNCTION() static i32 Target(u64 id)
        {
            GraphicsBuffer* b = GraphicsBuffer::Find(id);
            return b ? i32(b->target()) : 0;
        }
        HX_FUNCTION() static i32 UsageFlags(u64 id)
        {
            GraphicsBuffer* b = GraphicsBuffer::Find(id);
            return b ? b->usageFlags() : 0;
        }
        HX_FUNCTION() static String Name(u64 id)
        {
            GraphicsBuffer* b = GraphicsBuffer::Find(id);
            return b ? b->name() : String();
        }
        HX_FUNCTION() static bool SetData(u64 id, u64 address, u32 bytes, u32 offset)
        {
            GraphicsBuffer* b = GraphicsBuffer::Find(id);
            return b && b->SetData(reinterpret_cast<const void*>(uptr(address)), bytes, offset);
        }
        HX_FUNCTION() static bool GetData(u64 id, u64 address, u32 bytes, u32 offset)
        {
            GraphicsBuffer* b = GraphicsBuffer::Find(id);
            return b && b->GetData(reinterpret_cast<void*>(uptr(address)), bytes, offset);
        }
        HX_FUNCTION() static void SetCounterValue(u64 id, u32 value)
        {
            if (GraphicsBuffer* b = GraphicsBuffer::Find(id))
                b->SetCounterValue(value);
        }
        HX_FUNCTION() static void SetName(u64 id, StringView name)
        {
            if (GraphicsBuffer* b = GraphicsBuffer::Find(id))
                b->SetName(name);
        }
        // The device's buffer as a pointer-sized value (0 without a device).
        HX_FUNCTION() static u64 NativeBufferPtr(u64 id)
        {
            GraphicsBuffer* b = GraphicsBuffer::Find(id);
            return b ? u64(b->Gpu().value) : 0;
        }
    };
}
