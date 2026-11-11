#pragma once

#include <Helix/Engine/Rendering/Renderer.h>
#include <Helix/Math/Geometry.h>

// The renderers taking part (enabled component on an active GameObject) in dense arrays. The game side follows the
// Transform system and the components; Sync copies what changed into the render side, which culling and drawing read
// (the render thread of the pipelines milestone will own that side).
namespace helix::engine
{
    // What the render side reads, structure of arrays (bounds as the culling functions of Helix.Math take them).
    struct RenderData
    {
        Array<f32> centerX = Array<f32>(), centerY = Array<f32>(), centerZ = Array<f32>();
        Array<f32> extentX = Array<f32>(), extentY = Array<f32>(), extentZ = Array<f32>();
        Array<Matrix4x4> world = Array<Matrix4x4>();
        Array<EntityId> renderer = Array<EntityId>();
        Array<EntityId> mesh = Array<EntityId>();
        // 1 << the GameObject's layer.
        Array<u32> layerMask = Array<u32>();
        Array<i32> sortingOrder = Array<i32>();
        // Drawn at all (enabled, not forced off, not shadows only).
        Array<u8> drawn = Array<u8>();
        // Written by the renderer: some camera drew it.
        Array<u8> visible = Array<u8>();

        u32 Count() const { return u32(renderer.Size()); }
        BoundsSoA Bounds() const
        {
            return {centerX.Data(), centerY.Data(), centerZ.Data(), extentX.Data(), extentY.Data(), extentZ.Data(), centerX.Size()};
        }
    };

    struct RendererAccess
    {
        static u32& Index(Renderer& r) { return r.managerIndex_; }
        static const Bounds* LocalOverride(const Renderer& r) { return r.hasLocalBounds_ ? &r.localBounds_ : nullptr; }
        static const Bounds* WorldOverride(const Renderer& r) { return r.hasWorldBounds_ ? &r.worldBounds_ : nullptr; }
        static bool Drawn(const Renderer& r) { return r.enabled_ && !r.forceRenderingOff_ && r.shadowCasting_ != ShadowCastingMode::ShadowsOnly; }
    };

    class RendererManager
    {
    public:
        static RendererManager& Get();

        void Add(Renderer& r);
        void Remove(Renderer& r);
        // Something of the renderer changed: copy it again at the next Sync.
        void Touch(const Renderer& r);

        // The player loop, after the world matrices: world matrices and bounds of moved renderers, meshes that changed,
        // then the copy into the render side.
        void Update();
        void Sync();

        const RenderData& Render() const { return render_; }
        RenderData& RenderMutable() { return render_; }
        u32 Count() const { return u32(renderers_.Size()); }
        // The world bounds the game side holds for a renderer index.
        const Bounds& WorldBounds(u32 index) const { return worldBounds_[index]; }

    private:
        RendererManager();
        void Refresh(u32 index, Renderer& r);

        // Game side.
        Array<EntityId> renderers_ = Array<EntityId>();
        Array<EntityId> transforms_ = Array<EntityId>();
        Array<Matrix4x4> world_ = Array<Matrix4x4>();
        Array<Bounds> localBounds_ = Array<Bounds>();
        Array<Bounds> worldBounds_ = Array<Bounds>();
        Array<EntityId> meshes_ = Array<EntityId>();
        Array<u32> meshVersions_ = Array<u32>();
        Array<u8> dirty_ = Array<u8>();
        // What the render side needs from the component, read when it changes (never per frame).
        Array<u32> layerMasks_ = Array<u32>();
        Array<i32> sortingOrders_ = Array<i32>();
        Array<u8> drawn_ = Array<u8>();
        Array<u8> worldOverride_ = Array<u8>();
        HashMap<EntityId, u32> byTransform_;
        u32 transformSystem_ = 0;
        // Render side.
        RenderData render_;
    };

    // World bounds of local bounds under a matrix (the box around the moved box).
    Bounds TransformBounds(const Bounds& local, const Matrix4x4& world);
}
