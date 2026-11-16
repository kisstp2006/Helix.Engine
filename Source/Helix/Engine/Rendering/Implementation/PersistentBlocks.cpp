#include <Helix/Engine/Rendering/Implementation/RenderCore.h>
#include <Helix/Engine/Rendering/Implementation/RendererManager.h>
#include <Helix/Engine/Rendering/Pipeline/RenderPipeline.h>

#include <Helix/Core/Containers/HashMap.h>

#include <cstring>

// The SRP-batcher style path: material blocks and per object blocks live in parts of large uniform buffers. A part is
// written once and, when its contents change, written into a fresh part (the GPU may still read the old one); a freed
// part is reused after the frames in flight have passed.
namespace helix::engine::render
{
    namespace
    {
        constexpr u32 kArenaSize = 4u << 20;
        constexpr u32 kAlignment = 256;
        // Frames before a freed part is reused, and before an unused material's parts are freed.
        constexpr u64 kFramesInFlight = 3;
        constexpr u64 kMaterialIdle = 8;

        struct PendingFree
        {
            UniformPart part;
            u64 frame;
        };

        struct MaterialParts
        {
            u32 writes = ~0u;
            u64 lastFrame = 0;
            Array<UniformPart> parts = Array<UniformPart>();
        };

        struct ObjectSlot
        {
            u32 version = 0;
            UniformPart part;
            ObjectLights lights;
        };

        struct Blocks
        {
            rhi::Device* device = nullptr;
            Array<rhi::BufferHandle> arenas = Array<rhi::BufferHandle>();
            u32 used = kArenaSize;
            // Free parts by size.
            HashMap<u32, Array<UniformPart>> free;
            Array<PendingFree> pending = Array<PendingFree>();
            HashMap<u64, MaterialParts> materials;
            Array<ObjectSlot> objects = Array<ObjectSlot>();
            bool enabled = false;
            u64 frame = 0;
        };

        Blocks& B()
        {
            static Blocks b;
            return b;
        }

        UniformPart Allocate(u32 size)
        {
            Blocks& b = B();
            size = u32(AlignUp(Max(size, 16u), kAlignment));
            if (Array<UniformPart>* list = b.free.Find(size); list && !list->Empty())
            {
                UniformPart p = list->Back();
                list->Pop();
                return p;
            }
            if (b.used + size > kArenaSize)
            {
                b.arenas.Push(b.device->CreateBuffer({.size = kArenaSize, .usage = rhi::BufferUniform}));
                b.used = 0;
            }
            UniformPart p{b.arenas.Back(), b.used, size};
            b.used += size;
            return p;
        }

        void Free(const UniformPart& p)
        {
            if (p.size)
                B().pending.Push({p, B().frame});
        }

        void Write(rhi::CommandList& cl, const UniformPart& p, const void* data, u32 size) { cl.UpdateBuffer(p.buffer, p.offset, data, Min(size, p.size)); }
    }

    bool PersistentBlocksEnabled() { return B().enabled && B().device == Device(); }

    void BeginPersistentBlocks(rhi::Device* device, u64 frame)
    {
        Blocks& b = B();
        if (b.device != device)
            ReleasePersistentBlocks();
        b.device = device;
        b.frame = frame;
        const rhi::DeviceCaps* caps = device ? &device->Caps() : nullptr;
        b.enabled = caps && caps->uniformBufferOffsets && caps->uniformBufferPartialUpdates && GraphicsSettings::useScriptableRenderPipelineBatching();
        // Parts freed long enough ago go back to the free lists; materials not drawn for a while let theirs go.
        usize kept = 0;
        for (usize i = 0; i < b.pending.Size(); i++)
        {
            const PendingFree& f = b.pending[i];
            if (f.frame + kFramesInFlight <= frame)
            {
                Array<UniformPart>* list = b.free.Find(f.part.size);
                if (!list)
                {
                    b.free.Set(f.part.size, Array<UniformPart>());
                    list = b.free.Find(f.part.size);
                }
                list->Push(f.part);
            }
            else
                b.pending[kept++] = f;
        }
        b.pending.Resize(kept);
        Array<u64> idle;
        for (auto e : b.materials)
            if (e.value.lastFrame + kMaterialIdle < frame)
                idle.Push(e.key);
        for (u64 id : idle)
        {
            for (const UniformPart& p : b.materials.Find(id)->parts)
                Free(p);
            b.materials.Remove(id);
        }
        // Renderers that are gone.
        u32 count = RendererManager::Get().Count();
        while (b.objects.Size() > count)
        {
            Free(b.objects.Back().part);
            b.objects.Pop();
        }
    }

    Span<const UniformPart> MaterialBlockParts(rhi::CommandList& cl, const MaterialPassData& data)
    {
        Blocks& b = B();
        MaterialParts* m = b.materials.Find(data.uid);
        if (!m)
        {
            b.materials.Set(data.uid, MaterialParts());
            m = b.materials.Find(data.uid);
        }
        m->lastFrame = b.frame;
        if (m->writes != data.writes || m->parts.Size() != data.blocks.Size())
        {
            for (const UniformPart& p : m->parts)
                Free(p);
            m->parts.Clear();
            for (const Array<u8>& block : data.blocks)
            {
                UniformPart p = Allocate(u32(block.Size()));
                Write(cl, p, block.Data(), u32(block.Size()));
                m->parts.Push(p);
            }
            m->writes = data.writes;
        }
        return m->parts;
    }

    UniformPart ObjectBlockPart(rhi::CommandList& cl, u32 renderIndex, const ObjectLights& lights)
    {
        Blocks& b = B();
        const RenderData& d = RendererManager::Get().Render();
        if (renderIndex >= d.Count())
            return {};
        while (b.objects.Size() <= renderIndex)
            b.objects.Emplace();
        ObjectSlot& slot = b.objects[renderIndex];
        if (slot.part.size == 0 || slot.version != d.version[renderIndex] || !(slot.lights == lights))
        {
            Free(slot.part);
            slot.part = Allocate(kDrawBlockBytes);
            const Matrix4x4& world = d.world[renderIndex];
            struct
            {
                Matrix4x4 m[2];
                ObjectLights lights;
            } block{{world, world.inverse()}, lights};
            static_assert(sizeof(block) == kDrawBlockBytes);
            Write(cl, slot.part, &block, sizeof(block));
            slot.version = d.version[renderIndex];
            slot.lights = lights;
        }
        return slot.part;
    }

    void ReleasePersistentBlocks()
    {
        Blocks& b = B();
        if (b.device)
            for (rhi::BufferHandle h : b.arenas)
                b.device->Destroy(h);
        b.arenas.Clear();
        b.used = kArenaSize;
        b.free.Clear();
        b.pending.Clear();
        b.materials.Clear();
        b.objects.Clear();
        b.device = nullptr;
        b.enabled = false;
    }
}
