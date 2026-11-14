#include <Helix/Engine/Texture/RenderTexture.h>

#include <Helix/Core/Logging/Log.h>
#include <Helix/Core/Strings/Format.h>
#include <Helix/Engine/GameObject/Lifecycle.h>

namespace helix::engine
{
    namespace
    {
        LogChannel RenderTextureLog{"Engine"};

        // Render texture formats and their linear and sRGB graphics formats (None: the devices have no such format).
        struct FormatRow
        {
            RenderTextureFormat format;
            GraphicsFormat linear, srgb;
        };
        constexpr FormatRow kFormats[] = {
            {RenderTextureFormat::ARGB32, GraphicsFormat::R8G8B8A8_UNorm, GraphicsFormat::R8G8B8A8_SRGB},
            {RenderTextureFormat::Default, GraphicsFormat::R8G8B8A8_UNorm, GraphicsFormat::R8G8B8A8_SRGB},
            {RenderTextureFormat::BGRA32, GraphicsFormat::B8G8R8A8_UNorm, GraphicsFormat::B8G8R8A8_SRGB},
            {RenderTextureFormat::ARGBHalf, GraphicsFormat::R16G16B16A16_SFloat, GraphicsFormat::R16G16B16A16_SFloat},
            {RenderTextureFormat::DefaultHDR, GraphicsFormat::R16G16B16A16_SFloat, GraphicsFormat::R16G16B16A16_SFloat},
            {RenderTextureFormat::ARGBFloat, GraphicsFormat::R32G32B32A32_SFloat, GraphicsFormat::R32G32B32A32_SFloat},
            {RenderTextureFormat::RGFloat, GraphicsFormat::R32G32_SFloat, GraphicsFormat::R32G32_SFloat},
            {RenderTextureFormat::RGHalf, GraphicsFormat::R16G16_SFloat, GraphicsFormat::R16G16_SFloat},
            {RenderTextureFormat::RFloat, GraphicsFormat::R32_SFloat, GraphicsFormat::R32_SFloat},
            {RenderTextureFormat::RHalf, GraphicsFormat::R16_SFloat, GraphicsFormat::R16_SFloat},
            {RenderTextureFormat::R8, GraphicsFormat::R8_UNorm, GraphicsFormat::R8_UNorm},
            {RenderTextureFormat::RG16, GraphicsFormat::R8G8_UNorm, GraphicsFormat::R8G8_UNorm},
            {RenderTextureFormat::ARGBInt, GraphicsFormat::R32G32B32A32_SInt, GraphicsFormat::R32G32B32A32_SInt},
            {RenderTextureFormat::RGInt, GraphicsFormat::R32G32_SInt, GraphicsFormat::R32G32_SInt},
            {RenderTextureFormat::RInt, GraphicsFormat::R32_SInt, GraphicsFormat::R32_SInt},
            {RenderTextureFormat::RGB111110Float, GraphicsFormat::B10G11R11_UFloatPack32, GraphicsFormat::B10G11R11_UFloatPack32},
            {RenderTextureFormat::ARGB2101010, GraphicsFormat::A2B10G10R10_UNormPack32, GraphicsFormat::A2B10G10R10_UNormPack32},
        };

        struct DepthRow
        {
            GraphicsFormat format;
            i32 bits;
            bool stencil;
        };
        constexpr DepthRow kDepths[] = {
            {GraphicsFormat::D16_UNorm, 16, false},         {GraphicsFormat::D24_UNorm, 24, false},
            {GraphicsFormat::D24_UNorm_S8_UInt, 24, true},  {GraphicsFormat::D32_SFloat, 32, false},
            {GraphicsFormat::D32_SFloat_S8_UInt, 32, true}, {GraphicsFormat::S8_UInt, 0, true},
        };

        struct Pooled
        {
            EntityId texture;
            RenderTextureDescriptor descriptor;
            bool inUse = true;
            u32 lastUsed = 0;
        };

        struct Pool
        {
            Array<Pooled> items = Array<Pooled>();
            u32 frame = 0;
            u32 serial = 0;
            EntityId active;
        };

        Pool& P()
        {
            static Pool p;
            return p;
        }
    }

    // ---------------------------------------------------------------------------------------------- formats

    GraphicsFormat GraphicsFormatUtility::GetGraphicsFormat(RenderTextureFormat format, bool isSRGB)
    {
        for (const FormatRow& r : kFormats)
            if (r.format == format)
                return isSRGB ? r.srgb : r.linear;
        return GraphicsFormat::None;
    }

    RenderTextureFormat GraphicsFormatUtility::GetRenderTextureFormat(GraphicsFormat format)
    {
        if (format == GraphicsFormat::None)
            return RenderTextureFormat::Depth;
        for (const FormatRow& r : kFormats)
            if ((r.linear == format || r.srgb == format) && r.format != RenderTextureFormat::Default && r.format != RenderTextureFormat::DefaultHDR)
                return r.format;
        return RenderTextureFormat::Default;
    }

    bool GraphicsFormatUtility::IsSRGBFormat(GraphicsFormat format)
    {
        return format == GraphicsFormat::R8_SRGB || format == GraphicsFormat::R8G8_SRGB || format == GraphicsFormat::R8G8B8_SRGB ||
               format == GraphicsFormat::R8G8B8A8_SRGB || format == GraphicsFormat::B8G8R8_SRGB || format == GraphicsFormat::B8G8R8A8_SRGB;
    }

    bool GraphicsFormatUtility::IsDepthFormat(GraphicsFormat format)
    {
        for (const DepthRow& d : kDepths)
            if (d.format == format)
                return d.bits > 0;
        return false;
    }

    bool GraphicsFormatUtility::IsStencilFormat(GraphicsFormat format)
    {
        for (const DepthRow& d : kDepths)
            if (d.format == format)
                return d.stencil;
        return false;
    }

    GraphicsFormat GraphicsFormatUtility::GetDepthStencilFormat(i32 minimumDepthBits)
    {
        if (minimumDepthBits <= 0)
            return GraphicsFormat::None;
        if (minimumDepthBits <= 16)
            return GraphicsFormat::D16_UNorm;
        if (minimumDepthBits <= 24)
            return GraphicsFormat::D24_UNorm_S8_UInt;
        return GraphicsFormat::D32_SFloat_S8_UInt;
    }

    i32 GraphicsFormatUtility::DepthBits(GraphicsFormat format)
    {
        for (const DepthRow& d : kDepths)
            if (d.format == format)
                return d.bits;
        return 0;
    }

    // ---------------------------------------------------------------------------------------------- descriptor

    RenderTextureDescriptor::RenderTextureDescriptor(i32 w, i32 h, RenderTextureFormat format, i32 depthBufferBits, i32 mips)
        : width(w), height(h), mipCount(mips)
    {
        SetColorFormat(format);
        SetDepthBufferBits(depthBufferBits);
    }

    RenderTextureDescriptor::RenderTextureDescriptor(i32 w, i32 h, GraphicsFormat format, i32 depthBufferBits, i32 mips)
        : width(w), height(h), mipCount(mips), graphicsFormat(format)
    {
        sRGB = GraphicsFormatUtility::IsSRGBFormat(format);
        SetDepthBufferBits(depthBufferBits);
    }

    RenderTextureFormat RenderTextureDescriptor::colorFormat() const { return GraphicsFormatUtility::GetRenderTextureFormat(graphicsFormat); }

    void RenderTextureDescriptor::SetColorFormat(RenderTextureFormat format)
    {
        if (format == RenderTextureFormat::Depth || format == RenderTextureFormat::Shadowmap)
        {
            graphicsFormat = GraphicsFormat::None;
            if (depthStencilFormat == GraphicsFormat::None)
                depthStencilFormat = GraphicsFormat::D24_UNorm_S8_UInt;
            shadowSamplingMode = format == RenderTextureFormat::Shadowmap ? ShadowSamplingMode::CompareDepths : ShadowSamplingMode::None;
            return;
        }
        graphicsFormat = GraphicsFormatUtility::GetGraphicsFormat(format, sRGB);
    }

    bool RenderTextureDescriptor::operator==(const RenderTextureDescriptor& o) const
    {
        return width == o.width && height == o.height && msaaSamples == o.msaaSamples && volumeDepth == o.volumeDepth && mipCount == o.mipCount &&
               graphicsFormat == o.graphicsFormat && depthStencilFormat == o.depthStencilFormat && stencilFormat == o.stencilFormat &&
               dimension == o.dimension && shadowSamplingMode == o.shadowSamplingMode && vrUsage == o.vrUsage && memoryless == o.memoryless &&
               sRGB == o.sRGB && useMipMap == o.useMipMap && autoGenerateMips == o.autoGenerateMips && enableRandomWrite == o.enableRandomWrite &&
               bindMS == o.bindMS && useDynamicScale == o.useDynamicScale;
    }

    RenderTextureDescriptorData RenderTextureDescriptorData::From(const RenderTextureDescriptor& d)
    {
        RenderTextureDescriptorData r;
        r.width = d.width, r.height = d.height, r.msaaSamples = d.msaaSamples, r.volumeDepth = d.volumeDepth, r.mipCount = d.mipCount;
        r.graphicsFormat = i32(d.graphicsFormat), r.depthStencilFormat = i32(d.depthStencilFormat), r.stencilFormat = i32(d.stencilFormat);
        r.dimension = i32(d.dimension), r.shadowSamplingMode = i32(d.shadowSamplingMode), r.vrUsage = i32(d.vrUsage), r.memoryless = i32(d.memoryless);
        r.flags = (d.sRGB ? 1 : 0) | (d.useMipMap ? 2 : 0) | (d.autoGenerateMips ? 4 : 0) | (d.enableRandomWrite ? 8 : 0) | (d.bindMS ? 16 : 0) |
                  (d.useDynamicScale ? 32 : 0);
        return r;
    }

    RenderTextureDescriptor RenderTextureDescriptorData::ToDescriptor() const
    {
        RenderTextureDescriptor d;
        d.width = width, d.height = height, d.msaaSamples = msaaSamples, d.volumeDepth = volumeDepth, d.mipCount = mipCount;
        d.graphicsFormat = GraphicsFormat(graphicsFormat), d.depthStencilFormat = GraphicsFormat(depthStencilFormat), d.stencilFormat = GraphicsFormat(stencilFormat);
        d.dimension = TextureDimension(dimension), d.shadowSamplingMode = ShadowSamplingMode(shadowSamplingMode), d.vrUsage = VRTextureUsage(vrUsage);
        d.memoryless = RenderTextureMemoryless(memoryless);
        d.sRGB = (flags & 1) != 0, d.useMipMap = (flags & 2) != 0, d.autoGenerateMips = (flags & 4) != 0, d.enableRandomWrite = (flags & 8) != 0;
        d.bindMS = (flags & 16) != 0, d.useDynamicScale = (flags & 32) != 0;
        return d;
    }

    // ---------------------------------------------------------------------------------------------- RenderTexture

    RenderTexture::RenderTexture(const RenderTexture& other) : Texture(other), desc_(other.desc_)
    {
        Changed();
    }

    RenderTexture::~RenderTexture()
    {
        if (P().active == GetEntityId())
            P().active = EntityId();
    }

    RenderTexture* RenderTexture::New(const RenderTextureDescriptor& descriptor)
    {
        if (descriptor.width <= 0 || descriptor.height <= 0)
        {
            HX_LOG_ERROR(RenderTextureLog, "RenderTexture.Create failed: width & height must be larger than 0.");
            return nullptr;
        }
        auto* t = NewObject<RenderTexture>();
        t->desc_ = descriptor;
        if (t->desc_.msaaSamples < 1)
            t->desc_.msaaSamples = 1;
        t->linear_ = !descriptor.sRGB;
        t->readable_ = false;
        t->Changed();
        return t;
    }

    RenderTexture* RenderTexture::New(i32 width, i32 height, i32 depth, RenderTextureFormat format, RenderTextureReadWrite readWrite)
    {
        RenderTextureDescriptor d;
        d.width = width, d.height = height;
        d.sRGB = readWrite == RenderTextureReadWrite::sRGB;
        d.SetColorFormat(format);
        d.SetDepthBufferBits(depth);
        return New(d);
    }

    RenderTexture* RenderTexture::New(i32 width, i32 height, i32 depth, GraphicsFormat format) { return New(RenderTextureDescriptor(width, height, format, depth)); }

    RenderTexture* RenderTexture::New(const RenderTexture& other) { return New(other.desc_); }

    void RenderTexture::Changed()
    {
        data_.width = u32(desc_.width > 0 ? desc_.width : 0);
        data_.height = u32(desc_.height > 0 ? desc_.height : 0);
        data_.mips = desc_.useMipMap ? (desc_.mipCount > 0 ? u32(desc_.mipCount) : 0) : 1;
        if (desc_.useMipMap && desc_.mipCount <= 0)
        {
            u32 size = data_.width > data_.height ? data_.width : data_.height, mips = 1;
            while (size > 1)
                size >>= 1, mips++;
            data_.mips = mips;
        }
        linear_ = !desc_.sRGB;
        gpuVersion_++;
        version_++;
    }

    bool RenderTexture::Editable(const char* what) const
    {
        if (!created_)
            return true;
        HX_LOG_ERROR(RenderTextureLog, "Setting {} of already created render texture is not supported!", what);
        return false;
    }

#define HX_RT_SETTER(Name, member, what)       \
    void RenderTexture::Name                   \
    {                                          \
        if (!Editable(what))                   \
            return;                            \
        member;                                \
        Changed();                             \
    }
    HX_RT_SETTER(SetDescriptor(const RenderTextureDescriptor& d), desc_ = d, "descriptor")
    HX_RT_SETTER(SetWidth(i32 value), desc_.width = value, "width")
    HX_RT_SETTER(SetHeight(i32 value), desc_.height = value, "height")
    HX_RT_SETTER(SetDimension(TextureDimension value), desc_.dimension = value, "dimension")
    HX_RT_SETTER(SetDepth(i32 value), desc_.SetDepthBufferBits(value), "depth")
    HX_RT_SETTER(SetFormat(RenderTextureFormat value), desc_.SetColorFormat(value), "format")
    HX_RT_SETTER(SetGraphicsFormat(GraphicsFormat value), (desc_.graphicsFormat = value, desc_.sRGB = GraphicsFormatUtility::IsSRGBFormat(value)), "graphicsFormat")
    HX_RT_SETTER(SetDepthStencilFormat(GraphicsFormat value), desc_.depthStencilFormat = value, "depthStencilFormat")
    HX_RT_SETTER(SetStencilFormat(GraphicsFormat value), desc_.stencilFormat = value, "stencilFormat")
    HX_RT_SETTER(SetAntiAliasing(i32 value), desc_.msaaSamples = value < 1 ? 1 : value, "antiAliasing")
    HX_RT_SETTER(SetUseMipMap(bool value), desc_.useMipMap = value, "useMipMap")
    HX_RT_SETTER(SetVolumeDepth(i32 value), desc_.volumeDepth = value, "volumeDepth")
    HX_RT_SETTER(SetEnableRandomWrite(bool value), desc_.enableRandomWrite = value, "enableRandomWrite")
    HX_RT_SETTER(SetBindTextureMS(bool value), desc_.bindMS = value, "bindTextureMS")
    HX_RT_SETTER(SetMemorylessMode(RenderTextureMemoryless value), desc_.memoryless = value, "memorylessMode")
    HX_RT_SETTER(SetVrUsage(VRTextureUsage value), desc_.vrUsage = value, "vrUsage")
#undef HX_RT_SETTER

    void RenderTexture::SetAutoGenerateMips(bool value) { desc_.autoGenerateMips = value; }
    void RenderTexture::SetUseDynamicScale(bool value) { desc_.useDynamicScale = value; }

    bool RenderTexture::isPowerOfTwo() const
    {
        auto pow2 = [](i32 v) { return v > 0 && (v & (v - 1)) == 0; };
        return pow2(desc_.width) && pow2(desc_.height);
    }

    bool RenderTexture::Create()
    {
        if (desc_.width <= 0 || desc_.height <= 0)
        {
            HX_LOG_ERROR(RenderTextureLog, "RenderTexture.Create failed: width & height must be larger than 0.");
            return false;
        }
        if (desc_.graphicsFormat == GraphicsFormat::None && desc_.depthStencilFormat == GraphicsFormat::None)
        {
            HX_LOG_ERROR(RenderTextureLog, "RenderTexture.Create failed: format unsupported for random writes - {} ({}).", i32(desc_.graphicsFormat), Name());
            return false;
        }
        created_ = true;
        return true;
    }

    void RenderTexture::Release()
    {
        if (!created_)
            return;
        created_ = false;
        gpuVersion_++;
    }

    // The renderer resolves and makes mips through these (Rendering, which owns the GPU textures).
    namespace render
    {
        void ResolveRenderTexture(RenderTexture& source, RenderTexture* target);
        void GenerateRenderTextureMips(RenderTexture& texture);
    }

    void RenderTexture::ResolveAntiAliasedSurface() { render::ResolveRenderTexture(*this, nullptr); }
    void RenderTexture::ResolveAntiAliasedSurface(RenderTexture* target) { render::ResolveRenderTexture(*this, target); }

    void RenderTexture::GenerateMips()
    {
        if (!desc_.useMipMap)
        {
            HX_LOG_ERROR(RenderTextureLog, "Mip map generation failed: '{}' has no mips (useMipMap).", Name());
            return;
        }
        render::GenerateRenderTextureMips(*this);
    }

    // ---------------------------------------------------------------------------------------------- temporaries

    RenderTexture* RenderTexture::GetTemporary(const RenderTextureDescriptor& descriptor)
    {
        Pool& p = P();
        for (Pooled& item : p.items)
            if (!item.inUse && item.descriptor == descriptor)
                if (auto* t = EntityIdToObject<RenderTexture>(item.texture))
                {
                    item.inUse = true;
                    return t;
                }
        RenderTexture* t = New(descriptor);
        if (!t)
            return nullptr;
        t->SetName(Format("TempBuffer {} {}x{}", ++p.serial, descriptor.width, descriptor.height));
        t->hideFlags = HideFlags::HideAndDontSave;
        t->SetPersistent(true);
        t->Create();
        p.items.Push({t->GetEntityId(), descriptor, true, p.frame});
        return t;
    }

    RenderTexture* RenderTexture::GetTemporary(i32 width, i32 height, i32 depthBuffer, RenderTextureFormat format, RenderTextureReadWrite readWrite,
                                               i32 antiAliasing)
    {
        RenderTextureDescriptor d;
        d.width = width, d.height = height;
        d.sRGB = readWrite == RenderTextureReadWrite::sRGB;
        d.SetColorFormat(format);
        d.SetDepthBufferBits(depthBuffer);
        d.msaaSamples = antiAliasing < 1 ? 1 : antiAliasing;
        return GetTemporary(d);
    }

    RenderTexture* RenderTexture::GetTemporary(i32 width, i32 height, i32 depthBuffer, GraphicsFormat format)
    {
        return GetTemporary(RenderTextureDescriptor(width, height, format, depthBuffer));
    }

    void RenderTexture::ReleaseTemporary(RenderTexture* temp)
    {
        if (!temp)
            return;
        Pool& p = P();
        for (Pooled& item : p.items)
            if (item.texture == temp->GetEntityId())
            {
                item.inUse = false;
                item.lastUsed = p.frame;
                return;
            }
        HX_LOG_ERROR(RenderTextureLog, "Attempting to release RenderTexture that was not gotten as a temp buffer");
    }

    void RenderTexture::ReleaseUnusedTemporaries(u32 frame)
    {
        Pool& p = P();
        p.frame = frame;
        for (usize i = p.items.Size(); i-- > 0;)
        {
            Pooled& item = p.items[i];
            RenderTexture* t = EntityIdToObject<RenderTexture>(item.texture);
            if (!t || (!item.inUse && frame - item.lastUsed > 15))
            {
                if (t)
                    Object::DestroyImmediate(t, true);
                p.items.RemoveAt(i);
            }
        }
    }

    u32 RenderTexture::TemporaryCount() { return u32(P().items.Size()); }

    RenderTexture* RenderTexture::active() { return EntityIdToObject<RenderTexture>(P().active); }
    void RenderTexture::SetActive(RenderTexture* value) { P().active = value ? value->GetEntityId() : EntityId(); }
}
