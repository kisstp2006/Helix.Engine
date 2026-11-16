#include <Helix/Engine/Shader/ComputeShader.h>

#include <Helix/Core/Hashing/Hash.h>
#include <Helix/Core/Logging/Log.h>
#include <Helix/Core/Strings/Format.h>
#include <Helix/Engine/Rendering/Graphics.h>
#include <Helix/Engine/Rendering/Implementation/RenderCore.h>
#include <Helix/Engine/Shader/Implementation/ShaderAccess.h>
#include <Helix/Engine/Shader/Shader.h>
#include <Helix/Engine/Texture/RenderTexture.h>
#include <Helix/RHI/CommandList/CommandList.h>
#include <Helix/RHI/Device/Device.h>
#include <Helix/ShaderCompiler/Frontend/Frontend.h>

#include <cstring>

namespace helix::engine
{
    using namespace shadercompiler;

    namespace
    {
        LogChannel ComputeLog{"Engine"};

        u64 VariantKey(u32 kernel, ShaderTarget target, Span<const KeywordValue> keywords)
        {
            u64 h = CombineHash(MixHash(kernel), MixHash(u64(target) + 0x1000));
            for (const KeywordValue& k : keywords)
                h = CombineHash(h, CombineHash(Hash64(k.name.Data(), k.name.Size()), MixHash(u64(k.value))));
            return h;
        }

        // "weights[3]": the value "weights", element 3.
        StringView SplitElement(StringView name, u32& element)
        {
            element = 0;
            if (!name.Empty() && name[name.Size() - 1] == ']')
                if (usize open = name.FindLast('['); open != StringView::npos)
                {
                    for (usize i = open + 1; i + 1 < name.Size(); i++)
                        element = element * 10 + u32(name[i] - '0');
                    return name.Substr(0, open);
                }
            return name;
        }

        // `count` numbers of a value from element `first` on as floats (ints by their value).
        void Floats(const PropertySheet& sheet, const PropertyValue& v, u32 first, u32 count, f32* out)
        {
            Span<const f32> n = sheet.Numbers(v);
            for (u32 i = 0; i < count; i++)
            {
                if (first + i >= n.Size())
                {
                    out[i] = 0;
                    continue;
                }
                if (v.kind == PropertyKind::Int)
                {
                    i32 bits;
                    std::memcpy(&bits, &n[first + i], 4);
                    out[i] = f32(bits);
                }
                else
                    out[i] = n[first + i];
            }
        }

        // A value into a block member of any numeric type (an int into an int member bit for bit).
        void WriteMember(const ReflectedMember& m, const PropertySheet& sheet, const PropertyValue& v, u32 element, Array<u8>& block)
        {
            u32 width = PropertySheet::Width(v.kind);
            u32 components = m.size / 4;
            if (components > width)
                components = width;
            if (components > 16 || m.offset + components * 4 > block.Size())
                return;
            u8* at = block.Data() + m.offset;
            Span<const f32> n = sheet.Numbers(v);
            if (v.kind == PropertyKind::Int && m.type.base != BaseType::Float)
            {
                if (!n.Empty())
                    std::memcpy(at, &n[0], 4);
                return;
            }
            f32 values[16];
            Floats(sheet, v, element * PropertySheet::Width(v.kind), components, values);
            for (u32 i = 0; i < components; i++)
            {
                if (m.type.base == BaseType::Int || m.type.base == BaseType::Bool)
                {
                    i32 x = i32(values[i]);
                    std::memcpy(at + i * 4, &x, 4);
                }
                else if (m.type.base == BaseType::UInt)
                {
                    u32 x = values[i] < 0 ? 0u : u32(values[i]);
                    std::memcpy(at + i * 4, &x, 4);
                }
                else
                    std::memcpy(at + i * 4, &values[i], 4);
            }
        }

        bool Compile(const ComputeDescription& desc, const ComputeKernel& kernel, const ShaderProgram& program, ComputeVariant& v)
        {
            TranslateOptions options;
            options.keywords = Span<const KeywordValue>(v.keywords.Data(), v.keywords.Size());
            options.references = Span<const Assembly* const>(program.references.Data(), program.references.Size());
            options.symbols = Span<const DebugInfo* const>(program.symbols.Data(), program.symbols.Size());
            const Assembly& assembly = desc.assembly ? *desc.assembly : program.main->assembly;
            Module module;
            String& error = v.error;
            return TranslateEntryPoint(assembly, kernel.method, Stage::Compute, module, error, options) && Reflect(module, v.reflection, error) &&
                   CompileModule(module, v.target, v.compute, error, &Shader::Cache());
        }

        void PlanValues(ComputeVariant& v)
        {
            for (u32 b = 0; b < v.reflection.blocks.Size(); b++)
            {
                const ReflectedBlock& rb = v.reflection.blocks[b];
                if (rb.name == StringView(kKeywordBlockName))
                    continue;
                for (const ReflectedMember& m : rb.members)
                {
                    u32 element = 0;
                    StringView name = SplitElement(m.name, element);
                    v.members.Push({b, &m, engine::PropertyToID(name), element});
                }
            }
            for (const ReflectedResource& r : v.reflection.resources)
                v.resourceIds.Push(engine::PropertyToID(r.name));
        }

        bool MakeGpu(ComputeVariant& v, rhi::Device& d, StringView name)
        {
            if (v.device == &d && v.pipeline.IsValid())
                return true;
            v.device = &d;
            Array<rhi::ShaderBinding> bindings;
            for (const GlslBinding& b : v.compute.bindings)
                bindings.Push({b.name, b.kind == GlobalKind::Uniform ? rhi::BindingKind::UniformBuffer : rhi::BindingKind::Texture, b.slot});
            rhi::ShaderDesc desc;
            desc.stage = rhi::ShaderStage::Compute;
            if (v.target == ShaderTarget::Dxbc)
            {
                desc.bytecode = v.compute.code.Data();
                desc.bytecodeSize = v.compute.code.Size();
            }
            else
                desc.source = v.compute.Text();
            desc.bindings = bindings.Data();
            desc.bindingCount = u32(bindings.Size());
            desc.debugName = name;
            v.shader = d.CreateShader(desc);
            v.pipeline = v.shader.IsValid() ? d.CreateComputePipeline({v.shader, name}) : rhi::PipelineHandle();
            return v.pipeline.IsValid();
        }

        void DestroyGpu(ComputeVariant& v)
        {
            rhi::Device* d = render::Device();
            if (d && d == v.device)
            {
                d->Destroy(v.pipeline);
                d->Destroy(v.shader);
            }
            v.pipeline = rhi::PipelineHandle();
            v.shader = rhi::ShaderHandle();
            v.device = nullptr;
        }
    }

    // ---------------------------------------------------------------------------------------------- objects

    ComputeShader::ComputeShader(const ComputeShader& other)
        : Object(other), program_(other.program_), index_(other.index_), values_(other.values_), kernels_(other.kernels_), keywords_(other.keywords_)
    {
    }

    ComputeShader::~ComputeShader() { ClearVariants(); }

    void ComputeShader::ClearVariants()
    {
        for (auto e : variants_)
        {
            DestroyGpu(*e.value);
            delete e.value;
        }
        variants_.Clear();
    }

    ComputeShader* ComputeShader::Create(std::shared_ptr<ShaderProgram> program, u32 index)
    {
        ComputeShader* s = NewObject<ComputeShader>();
        if (program && index < program->computes.Size())
        {
            s->SetName(program->computes[index].name);
            s->kernels_.Resize(program->computes[index].kernels.Size());
        }
        s->program_ = Move(program);
        s->index_ = index;
        return s;
    }

    ComputeShader* ComputeShader::Find(StringView name)
    {
        ComputeShader* found = nullptr;
        ForEachObjectOfClass(ClassOf<ComputeShader>(), [&](Object* o) {
            if (o->Name() == name)
            {
                found = static_cast<ComputeShader*>(o);
                return false;
            }
            return true;
        });
        return found;
    }

    const ComputeDescription* ComputeShader::Description() const { return program_ && index_ < program_->computes.Size() ? &program_->computes[index_] : nullptr; }

    i32 ComputeShader::KernelIndex(StringView name) const
    {
        const ComputeDescription* d = Description();
        return d ? d->FindKernel(name) : -1;
    }

    i32 ComputeShader::FindKernel(StringView name) const
    {
        i32 k = KernelIndex(name);
        if (k < 0)
            HX_LOG_ERROR(ComputeLog, "FindKernel failed: compute shader '{}' has no kernel '{}'.", Name(), name);
        return k;
    }

    bool ComputeShader::HasKernel(StringView name) const { return KernelIndex(name) >= 0; }

    i32 ComputeShader::KernelCount() const
    {
        const ComputeDescription* d = Description();
        return d ? i32(d->kernels.Size()) : 0;
    }

    u32 ComputeShader::KernelGroupSize(i32 kernelIndex, i32 axis) const
    {
        const ComputeDescription* d = Description();
        if (!d || kernelIndex < 0 || u32(kernelIndex) >= d->kernels.Size() || axis < 0 || axis > 2)
            return 0;
        return d->kernels[u32(kernelIndex)].groupSize[axis];
    }

    void ComputeShader::GetKernelThreadGroupSizes(i32 kernelIndex, u32& x, u32& y, u32& z) const
    {
        x = KernelGroupSize(kernelIndex, 0);
        y = KernelGroupSize(kernelIndex, 1);
        z = KernelGroupSize(kernelIndex, 2);
    }

    bool ComputeShader::ComputeTarget(ShaderTarget& out)
    {
        rhi::Device* d = render::Device();
        if (!d || !d->Caps().compute || !d->Caps().storageBuffers)
            return false;
        const rhi::DeviceCaps& c = d->Caps();
        switch (c.backend)
        {
        case rhi::Backend::D3D11:
            out = ShaderTarget::Dxbc;
            return true;
        case rhi::Backend::OpenGL:
            out = c.apiMajor > 4 || (c.apiMajor == 4 && c.apiMinor >= 5) ? ShaderTarget::Glsl450 : ShaderTarget::Glsl430;
            return c.apiMajor > 4 || (c.apiMajor == 4 && c.apiMinor >= 3);
        case rhi::Backend::OpenGLES:
            out = c.apiMinor >= 2 ? ShaderTarget::Essl320 : ShaderTarget::Essl310;
            return c.apiMajor > 3 || c.apiMinor >= 1;
        default:
            return false;
        }
    }

    bool ComputeShader::IsSupported(i32 kernelIndex)
    {
        ShaderTarget target;
        return ComputeTarget(target) && GetVariant(kernelIndex, target) != nullptr;
    }

    ComputeShader::KernelResources* ComputeShader::Kernel(i32 kernelIndex)
    {
        if (kernelIndex < 0 || usize(kernelIndex) >= kernels_.Size())
        {
            HX_LOG_ERROR(ComputeLog, "Compute shader '{}': kernel index ({}) out of range.", Name(), kernelIndex);
            return nullptr;
        }
        return &kernels_[u32(kernelIndex)];
    }

    // ---------------------------------------------------------------------------------------------- values

    void ComputeShader::SetInts(i32 nameID, Span<const i32> values)
    {
        // Ints are kept as floats of their value, as SetInt's (the members take them by their value).
        Array<f32> f;
        for (i32 v : values)
            f.Push(f32(v));
        values_.SetFloatArray(nameID, Span<const f32>(f.Data(), f.Size()));
    }

    void ComputeShader::SetTexture(i32 kernelIndex, i32 nameID, Texture* texture, i32 mipLevel)
    {
        if (KernelResources* k = Kernel(kernelIndex))
        {
            k->sheet.SetTexture(nameID, texture ? texture->GetEntityId() : EntityId());
            k->mips.Set(nameID, mipLevel);
        }
    }

    void ComputeShader::SetTextureFromGlobal(i32 kernelIndex, i32 nameID, i32 globalTextureNameID)
    {
        if (KernelResources* k = Kernel(kernelIndex))
        {
            k->sheet.SetTexture(nameID, Shader::Globals().GetTexture(globalTextureNameID));
            k->mips.Set(nameID, 0);
        }
    }

    void ComputeShader::SetBufferId(i32 kernelIndex, i32 nameID, u64 buffer)
    {
        if (KernelResources* k = Kernel(kernelIndex))
            k->sheet.SetBuffer(nameID, buffer);
    }

    // ---------------------------------------------------------------------------------------------- keywords

    void ComputeShader::EnableKeyword(StringView keyword)
    {
        for (const String& k : keywords_)
            if (k == keyword)
                return;
        keywords_.Push(String(keyword));
    }

    void ComputeShader::DisableKeyword(StringView keyword)
    {
        for (usize i = 0; i < keywords_.Size(); i++)
            if (keywords_[i] == keyword)
            {
                keywords_.RemoveAt(i);
                return;
            }
    }

    bool ComputeShader::IsKeywordEnabled(StringView keyword) const
    {
        for (const String& k : keywords_)
            if (k == keyword)
                return true;
        return false;
    }

    void ComputeShader::SetShaderKeywords(Span<const String> keywords)
    {
        keywords_.Clear();
        for (const String& k : keywords)
            EnableKeyword(k);
    }

    const Array<ShaderKeywordInfo>& ComputeShader::KeywordSpace() const
    {
        if (keywordSpaceMade_)
            return keywordSpace_;
        keywordSpaceMade_ = true;
        if (const ComputeDescription* d = Description())
            for (const ShaderKeyword& k : d->keywords)
            {
                auto add = [&](String name) {
                    ShaderKeywordInfo info;
                    info.name = Move(name);
                    info.dynamic = k.dynamic;
                    info.overridable = k.global;
                    keywordSpace_.Push(Move(info));
                };
                if (k.valueNames.Empty())
                    add(k.name);
                for (const String& member : k.valueNames)
                    add(Format("{}_{}", k.name, member));
            }
        return keywordSpace_;
    }

    Array<LocalKeyword> ComputeShader::enabledKeywords() const
    {
        Array<LocalKeyword> out;
        const Array<ShaderKeywordInfo>& space = KeywordSpace();
        for (u32 i = 0; i < space.Size(); i++)
            if (IsKeywordEnabled(space[i].name))
            {
                LocalKeyword k;
                k.shader = GetEntityId().value;
                k.index = i;
                out.Push(k);
            }
        return out;
    }

    // ---------------------------------------------------------------------------------------------- variants

    const ComputeVariant* ComputeShader::GetVariant(i32 kernelIndex, ShaderTarget target)
    {
        const ComputeDescription* d = Description();
        if (!d || kernelIndex < 0 || u32(kernelIndex) >= d->kernels.Size())
            return nullptr;
        // Every keyword of the definition with its value: the enabled one, a global keyword for global declarations,
        // else off (the first member of an enum keyword).
        Array<KeywordValue> keywords;
        auto on = [&](const ShaderKeyword& k, StringView name) { return IsKeywordEnabled(name) || (k.global && Shader::IsKeywordEnabled(name)); };
        for (const ShaderKeyword& k : d->keywords)
        {
            KeywordValue v;
            v.name = k.name;
            if (k.valueNames.Empty())
                v.value = on(k, k.name) ? 1 : 0;
            else
            {
                v.value = k.values.Empty() ? 0 : k.values[0];
                for (usize m = 0; m < k.valueNames.Size() && m < k.values.Size(); m++)
                    if (on(k, Format("{}_{}", k.name, k.valueNames[m])))
                    {
                        v.value = k.values[m];
                        break;
                    }
            }
            keywords.Push(v);
        }
        u64 key = VariantKey(u32(kernelIndex), target, Span<const KeywordValue>(keywords.Data(), keywords.Size()));
        ComputeVariant* v = nullptr;
        if (ComputeVariant** found = variants_.Find(key))
            v = *found;
        else
        {
            v = new ComputeVariant();
            v->kernel = u32(kernelIndex);
            v->target = target;
            v->keywords = Move(keywords);
            v->ok = Compile(*d, d->kernels[u32(kernelIndex)], *program_, *v);
            if (v->ok)
                PlanValues(*v);
            else
                HX_LOG_ERROR(ComputeLog, "Compute shader error in '{}', kernel {}: {}", d->name, d->kernels[u32(kernelIndex)].name, v->error);
            variants_.Set(key, v);
        }
        return v->ok ? v : nullptr;
    }

    bool ComputeShader::Record(rhi::CommandList& cl, i32 kernelIndex, u32 x, u32 y, u32 z, GraphicsBuffer* indirect, u32 argsOffset)
    {
        const ComputeDescription* d = Description();
        if (!d)
        {
            HX_LOG_ERROR(ComputeLog, "Compute shader '{}' has no definition.", Name());
            return false;
        }
        KernelResources* kernel = Kernel(kernelIndex);
        if (!kernel)
            return false;
        rhi::Device* device = render::Device();
        ShaderTarget target;
        if (!ComputeTarget(target))
        {
            HX_LOG_ERROR(ComputeLog, "Compute shader '{}': this device runs no compute shaders.", Name());
            return false;
        }
        ComputeVariant* v = const_cast<ComputeVariant*>(GetVariant(kernelIndex, target));
        if (!v)
            return false;
        StringView kernelName = d->kernels[u32(kernelIndex)].name;
        if (!MakeGpu(*v, *device, kernelName))
        {
            HX_LOG_ERROR(ComputeLog, "Compute shader '{}', kernel {}: the device did not take the compiled kernel.", Name(), kernelName);
            return false;
        }
        const PropertySheet& globals = Shader::Globals();
        // Every resource must be there before anything is recorded.
        struct Bound
        {
            const ReflectedResource* resource;
            rhi::BufferHandle buffer;
            rhi::TextureHandle texture;
            rhi::SamplerHandle sampler;
            u32 mip;
        };
        Array<Bound> bound;
        for (u32 i = 0; i < v->reflection.resources.Size(); i++)
        {
            const ReflectedResource& r = v->reflection.resources[i];
            i32 id = v->resourceIds[i];
            Bound b{&r, {}, {}, {}, 0};
            if (r.kind == ResourceKind::StorageBuffer)
            {
                u64 bufferId = kernel->sheet.GetBuffer(id);
                if (!bufferId)
                    bufferId = globals.GetBuffer(id);
                GraphicsBuffer* buffer = GraphicsBuffer::Find(bufferId);
                if (!buffer)
                {
                    HX_LOG_ERROR(ComputeLog, "Compute shader ({}): Property ({}) at kernel index ({}) is not set", Name(), r.name, kernelIndex);
                    return false;
                }
                b.buffer = buffer->Gpu();
            }
            else
            {
                const PropertyValue* value = kernel->sheet.Find(id);
                EntityId textureId = value && value->kind == PropertyKind::Texture ? value->texture : globals.GetTexture(id);
                Texture* texture = EntityIdToObject<Texture>(textureId);
                if (r.kind == ResourceKind::StorageTexture)
                {
                    auto* rt = texture && texture->Is<RenderTexture>() ? static_cast<RenderTexture*>(texture) : nullptr;
                    const render::GpuRenderTarget* g = rt ? render::RenderTargetOf(*rt) : nullptr;
                    if (!g || !rt->enableRandomWrite())
                    {
                        HX_LOG_ERROR(ComputeLog, "Compute shader ({}): Property ({}) at kernel index ({}) needs a render texture with enableRandomWrite",
                                     Name(), r.name, kernelIndex);
                        return false;
                    }
                    b.texture = g->color;
                    const i32* mip = kernel->mips.Find(id);
                    b.mip = mip && *mip > 0 ? u32(*mip) : 0;
                }
                else
                {
                    if (!texture)
                    {
                        HX_LOG_ERROR(ComputeLog, "Compute shader ({}): Property ({}) at kernel index ({}) is not set", Name(), r.name, kernelIndex);
                        return false;
                    }
                    const render::GpuTexture* g = render::TextureOf(*texture);
                    if (!g || !g->texture.IsValid())
                        g = render::TextureOf(*Texture2D::whiteTexture());
                    b.texture = g->texture;
                    b.sampler = g->sampler;
                }
            }
            bound.Push(b);
        }
        cl.SetPipeline(v->pipeline);
        // The uniform blocks: the shader's values by name, else the globals; the keyword block the variant's values.
        Array<Array<u8>> blocks;
        blocks.Resize(v->reflection.blocks.Size());
        for (u32 b = 0; b < v->reflection.blocks.Size(); b++)
        {
            const ReflectedBlock& rb = v->reflection.blocks[b];
            blocks[b].Resize(rb.size);
            std::memset(blocks[b].Data(), 0, blocks[b].Size());
            if (rb.name == StringView(kKeywordBlockName))
                for (const ReflectedMember& m : rb.members)
                    for (const KeywordValue& kv : v->keywords)
                        if (m.name == kv.name && m.offset + 4 <= blocks[b].Size())
                        {
                            i32 value = i32(kv.value);
                            std::memcpy(blocks[b].Data() + m.offset, &value, 4);
                        }
        }
        for (const ComputeVariant::Member& m : v->members)
        {
            const PropertySheet* sheet = &values_;
            const PropertyValue* value = values_.Find(m.id);
            if (!value)
                value = globals.Find(m.id), sheet = &globals;
            if (!value || value->kind == PropertyKind::Texture || value->kind == PropertyKind::Buffer)
                continue;
            u32 element = m.element;
            if (value->kind == PropertyKind::FloatArray)
            {
                // Floats lie one per element of a scalar array; a vector member takes them from its start.
                Span<const f32> n = sheet->Numbers(*value);
                u32 components = m.member->size / 4;
                Array<u8>& block = blocks[m.block];
                for (u32 c = 0; c < components && element + c < n.Size() && m.member->offset + (c + 1) * 4 <= block.Size(); c++)
                {
                    f32 f = n[element + c];
                    if (m.member->type.base == BaseType::Int || m.member->type.base == BaseType::Bool)
                    {
                        i32 bits = i32(f);
                        std::memcpy(block.Data() + m.member->offset + c * 4, &bits, 4);
                    }
                    else if (m.member->type.base == BaseType::UInt)
                    {
                        u32 bits = f < 0 ? 0u : u32(f);
                        std::memcpy(block.Data() + m.member->offset + c * 4, &bits, 4);
                    }
                    else
                        std::memcpy(block.Data() + m.member->offset + c * 4, &f, 4);
                }
                continue;
            }
            if (element >= value->count)
                continue;
            WriteMember(*m.member, *sheet, *value, element, blocks[m.block]);
        }
        for (u32 b = 0; b < v->reflection.blocks.Size(); b++)
        {
            rhi::TransientAllocation a;
            render::Upload(blocks[b].Data(), u32(blocks[b].Size()), a);
            cl.SetUniformBuffer(v->reflection.blocks[b].slot, a);
        }
        for (const Bound& b : bound)
        {
            if (b.resource->kind == ResourceKind::StorageBuffer)
                cl.SetStorageBuffer(b.resource->slot, b.buffer, b.resource->writable ? rhi::StorageAccess::ReadWrite : rhi::StorageAccess::ReadOnly);
            else if (b.resource->kind == ResourceKind::StorageTexture)
                cl.SetStorageTexture(b.resource->slot, b.texture, b.mip);
            else
                cl.SetTexture(b.resource->slot, b.texture, b.sampler);
        }
        if (indirect)
            cl.DispatchIndirect(indirect->Gpu(), argsOffset);
        else
            cl.Dispatch(x, y, z);
        return true;
    }

    void ComputeShader::Dispatch(i32 kernelIndex, i32 threadGroupsX, i32 threadGroupsY, i32 threadGroupsZ)
    {
        if (threadGroupsX <= 0 || threadGroupsY <= 0 || threadGroupsZ <= 0)
        {
            if (threadGroupsX < 0 || threadGroupsY < 0 || threadGroupsZ < 0)
                HX_LOG_ERROR(ComputeLog, "Compute shader '{}': thread group counts must not be negative.", Name());
            return;
        }
        rhi::Device* d = render::Device();
        if (!d)
            return;
        rhi::CommandList cl;
        if (Record(cl, kernelIndex, u32(threadGroupsX), u32(threadGroupsY), u32(threadGroupsZ)))
            d->Submit(cl);
    }

    void ComputeShader::DispatchIndirectId(i32 kernelIndex, u64 argsBuffer, u32 argsOffset)
    {
        GraphicsBuffer* args = GraphicsBuffer::Find(argsBuffer);
        rhi::Device* d = render::Device();
        if (!args || !d)
        {
            HX_LOG_ERROR(ComputeLog, "Compute shader '{}': DispatchIndirect needs an arguments buffer.", Name());
            return;
        }
        rhi::CommandList cl;
        if (Record(cl, kernelIndex, 0, 0, 0, args, argsOffset))
            d->Submit(cl);
    }
}
