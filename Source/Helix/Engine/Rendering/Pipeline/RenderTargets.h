#pragma once

#include <Helix/Engine/Object/EntityId.h>
#include <Helix/Engine/Texture/RenderTexture.h>
#include <Helix/Scripting/Bindings/Tags.h>

// Where command buffers draw and what they copy: the reference's RenderTargetIdentifier (a built in target, a
// temporary render texture by its property id, or a texture) with a mip, a cube face and a slice.
namespace helix::engine
{
    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class BuiltinRenderTextureType : i32
    {
        PropertyName = -4,
        BufferPtr = -3,
        RenderTexture = -2,
        BindableTexture = -1,
        None = 0,
        CurrentActive = 1,
        CameraTarget = 2,
        Depth = 3,
        DepthNormals = 4,
        ResolvedDepth = 5,
        PrepassNormalsSpec = 7,
        PrepassLight = 8,
        PrepassLightSpec = 9,
        GBuffer0 = 10,
        GBuffer1 = 11,
        GBuffer2 = 12,
        GBuffer3 = 13,
        Reflections = 14,
        MotionVectors = 15,
        GBuffer4 = 16,
        GBuffer5 = 17,
        GBuffer6 = 18,
        GBuffer7 = 19,
    };

    // What a pass keeps of a target before it draws: Load and DontCare as their names say; Clear loads (clears come
    // from ClearRenderTarget).
    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class RenderBufferLoadAction : i32
    {
        Load = 0,
        Clear = 1,
        DontCare = 2,
    };

    // Every target is stored; a multisampled one is resolved when it stops being the target.
    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class RenderBufferStoreAction : i32
    {
        Store = 0,
        Resolve = 1,
        StoreAndResolve = 2,
        DontCare = 3,
    };

    HX_ENUM(Namespace = "HelixEngine.Rendering", Attributes = "[System.Flags]")
    enum class RTClearFlags : i32
    {
        None = 0,
        Color = 1,
        Depth = 2,
        Stencil = 4,
        All = 7,
        DepthStencil = 6,
        ColorDepth = 3,
        ColorStencil = 5,
    };

    HX_ENUM(Namespace = "HelixEngine")
    enum class CubemapFace : i32
    {
        Unknown = -1,
        PositiveX = 0,
        NegativeX = 1,
        PositiveY = 2,
        NegativeY = 3,
        PositiveZ = 4,
        NegativeZ = 5,
    };

    struct RenderTargetIdentifier
    {
        static constexpr i32 AllDepthSlices = -1;

        BuiltinRenderTextureType type = BuiltinRenderTextureType::None;
        // The temporary render texture's property id (PropertyName).
        i32 nameID = -1;
        // The texture (RenderTexture, BindableTexture).
        EntityId texture;
        i32 mipLevel = 0;
        CubemapFace face = CubemapFace::Unknown;
        i32 depthSlice = 0;

        RenderTargetIdentifier() = default;
        RenderTargetIdentifier(BuiltinRenderTextureType builtin) : type(builtin) {}
        RenderTargetIdentifier(i32 propertyId) : type(BuiltinRenderTextureType::PropertyName), nameID(propertyId) {}
        RenderTargetIdentifier(Texture* t);
        RenderTargetIdentifier(const RenderTargetIdentifier& other, i32 mip, CubemapFace cubeFace = CubemapFace::Unknown, i32 slice = 0)
            : RenderTargetIdentifier(other)
        {
            mipLevel = mip, face = cubeFace, depthSlice = slice;
        }
        // The layer of a cube face or an array slice (0 for 2D textures and all slices).
        u32 Layer() const { return face != CubemapFace::Unknown ? u32(face) : depthSlice > 0 ? u32(depthSlice) : 0u; }
        bool operator==(const RenderTargetIdentifier& o) const
        {
            return type == o.type && nameID == o.nameID && texture == o.texture && mipLevel == o.mipLevel && face == o.face && depthSlice == o.depthSlice;
        }
    };

    // A RenderTargetIdentifier as plain values for C# (internal).
    HX_STRUCT(Namespace = "HelixEngine.Internal", Internal)
    struct RenderTargetData
    {
        HX_FIELD() i32 type = 0;
        HX_FIELD() i32 nameID = -1;
        HX_FIELD() u64 texture = 0;
        HX_FIELD() i32 mipLevel = 0;
        HX_FIELD() i32 face = -1;
        HX_FIELD() i32 depthSlice = 0;

        RenderTargetIdentifier ToIdentifier() const
        {
            RenderTargetIdentifier r;
            r.type = BuiltinRenderTextureType(type), r.nameID = nameID, r.texture = EntityId::FromULong(texture), r.mipLevel = mipLevel;
            r.face = CubemapFace(face), r.depthSlice = depthSlice;
            return r;
        }
    };
}
