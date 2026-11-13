#include <Helix/Engine/GameObject/Lifecycle.h>
#include <Helix/Engine/Rendering/Implementation/RendererManager.h>
#include <Helix/Engine/Transform/Hierarchy.h>

#include <cmath>

namespace helix::engine
{
    void (*detail::gVisibilityChanged)(Renderer*, bool) = nullptr;

    // ---------------------------------------------------------------------------------------------- bounds

    Bounds TransformBounds(const Bounds& local, const Matrix4x4& m)
    {
        Vector3 c = m.MultiplyPoint3x4(local.center);
        const Vector3& e = local.extents;
        Vector3 x(std::fabs(m.m00) * e.x + std::fabs(m.m01) * e.y + std::fabs(m.m02) * e.z,
                  std::fabs(m.m10) * e.x + std::fabs(m.m11) * e.y + std::fabs(m.m12) * e.z,
                  std::fabs(m.m20) * e.x + std::fabs(m.m21) * e.y + std::fabs(m.m22) * e.z);
        Bounds b;
        b.center = c;
        b.extents = x;
        return b;
    }

    // ---------------------------------------------------------------------------------------------- manager

    RendererManager& RendererManager::Get()
    {
        static RendererManager m;
        return m;
    }

    RendererManager::RendererManager() { transformSystem_ = RegisterTransformSystem("Renderers"); }

    void RendererManager::Add(Renderer& r)
    {
        u32& index = RendererAccess::Index(r);
        if (index != ~0u)
            return;
        Transform* t = r.transform();
        index = u32(renderers_.Size());
        renderers_.Push(r.GetEntityId());
        transforms_.Push(t ? t->GetEntityId() : EntityId());
        world_.Push(t ? t->localToWorldMatrix() : Matrix4x4::identity);
        localBounds_.Push(Bounds());
        worldBounds_.Push(Bounds());
        meshes_.Push(EntityId());
        meshVersions_.Push(~0u);
        dirty_.Push(u8(1));
        layerMasks_.Push(0);
        sortingOrders_.Push(0);
        drawn_.Push(0);
        worldOverride_.Push(0);
        if (t)
        {
            byTransform_.Set(t->GetEntityId(), index);
            SetTransformInterest(t, transformSystem_, true);
        }
        RenderData& d = render_;
        for (Array<f32>* a : {&d.centerX, &d.centerY, &d.centerZ, &d.extentX, &d.extentY, &d.extentZ})
            a->Push(0.0f);
        d.world.Push(Matrix4x4::identity);
        d.renderer.Push(r.GetEntityId());
        d.mesh.Push(EntityId());
        d.layerMask.Push(0);
        d.sortingOrder.Push(0);
        d.drawn.Push(0);
        d.visible.Push(0);
        d.wasVisible.Push(0);
        Refresh(index, r);
    }

    namespace
    {
        template <class T>
        void SwapRemove(Array<T>& a, u32 i)
        {
            u32 last = u32(a.Size()) - 1;
            if (i != last)
                a[i] = a[last];
            a.RemoveAt(last);
        }
    }

    void RendererManager::Remove(Renderer& r)
    {
        u32& index = RendererAccess::Index(r);
        if (index == ~0u)
            return;
        u32 i = index;
        index = ~0u;
        if (Transform* t = r.transform())
        {
            SetTransformInterest(t, transformSystem_, false);
            byTransform_.Remove(t->GetEntityId());
        }
        u32 last = u32(renderers_.Size()) - 1;
        if (i != last)
        {
            // The last renderer takes the freed place.
            if (Renderer* moved = EntityIdToObject<Renderer>(renderers_[last]))
                RendererAccess::Index(*moved) = i;
            if (transforms_[last].IsValid())
                byTransform_.Set(transforms_[last], i);
        }
        SwapRemove(renderers_, i);
        SwapRemove(transforms_, i);
        SwapRemove(world_, i);
        SwapRemove(localBounds_, i);
        SwapRemove(worldBounds_, i);
        SwapRemove(meshes_, i);
        SwapRemove(meshVersions_, i);
        SwapRemove(dirty_, i);
        SwapRemove(layerMasks_, i);
        SwapRemove(sortingOrders_, i);
        SwapRemove(drawn_, i);
        SwapRemove(worldOverride_, i);
        RenderData& d = render_;
        for (Array<f32>* a : {&d.centerX, &d.centerY, &d.centerZ, &d.extentX, &d.extentY, &d.extentZ})
            SwapRemove(*a, i);
        SwapRemove(d.world, i);
        SwapRemove(d.renderer, i);
        SwapRemove(d.mesh, i);
        SwapRemove(d.layerMask, i);
        SwapRemove(d.sortingOrder, i);
        SwapRemove(d.drawn, i);
        SwapRemove(d.visible, i);
        SwapRemove(d.wasVisible, i);
    }

    void RendererManager::Touch(const Renderer& r)
    {
        u32 i = r.ManagerIndex();
        if (i == ~0u)
            return;
        // Mesh or bounds may have changed: look at them again.
        meshVersions_[i] = ~0u;
        dirty_[i] = 1;
    }

    // Local and world bounds from the mesh and the overrides.
    void RendererManager::Refresh(u32 i, Renderer& r)
    {
        Mesh* m = r.RenderedMesh();
        meshes_[i] = m ? m->GetEntityId() : EntityId();
        meshVersions_[i] = m ? m->Version() : 0u;
        const Bounds* local = RendererAccess::LocalOverride(r);
        localBounds_[i] = local ? *local : m ? m->bounds() : Bounds();
        const Bounds* world = RendererAccess::WorldOverride(r);
        worldOverride_[i] = world ? 1 : 0;
        worldBounds_[i] = world ? *world : TransformBounds(localBounds_[i], world_[i]);
        GameObject* g = r.gameObject();
        layerMasks_[i] = g ? 1u << g->layer() : 0u;
        sortingOrders_[i] = r.sortingOrder();
        drawn_[i] = RendererAccess::Drawn(r) ? 1 : 0;
        dirty_[i] = 1;
    }

    void RendererManager::Update()
    {
        // Moved transforms: their world matrix and bounds (hierarchies in parallel; each index is written by one).
        DispatchTransformChanges(transformSystem_, [this](TransformHierarchy& h, u32 node) {
            Transform* t = h.transforms[node];
            const u32* i = t ? byTransform_.Find(t->GetEntityId()) : nullptr;
            if (!i)
                return;
            world_[*i] = h.world[node];
            if (!worldOverride_[*i])
                worldBounds_[*i] = TransformBounds(localBounds_[*i], world_[*i]);
            dirty_[*i] = 1;
        });
        // Meshes that changed (another mesh, new vertices) and renderers touched since the last frame.
        for (u32 i = 0; i < renderers_.Size(); i++)
        {
            Mesh* m = EntityIdToObject<Mesh>(meshes_[i]);
            u32 version = m ? m->Version() : 0u;
            if (version == meshVersions_[i] && (m || !meshes_[i].IsValid()))
                continue;
            if (Renderer* r = EntityIdToObject<Renderer>(renderers_[i]))
                Refresh(i, *r);
        }
    }

    void RendererManager::Sync()
    {
        RenderData& d = render_;
        for (u32 i = 0; i < renderers_.Size(); i++)
        {
            if (!dirty_[i])
                continue;
            dirty_[i] = 0;
            const Bounds& b = worldBounds_[i];
            d.centerX[i] = b.center.x, d.centerY[i] = b.center.y, d.centerZ[i] = b.center.z;
            d.extentX[i] = b.extents.x, d.extentY[i] = b.extents.y, d.extentZ[i] = b.extents.z;
            d.world[i] = world_[i];
            d.mesh[i] = meshes_[i];
            d.layerMask[i] = layerMasks_[i];
            d.sortingOrder[i] = sortingOrders_[i];
            d.drawn[i] = drawn_[i];
        }
    }

    // ---------------------------------------------------------------------------------------------- MeshFilter

    void MeshFilter::SetSharedMesh(Mesh* mesh)
    {
        mesh_ = PPtr<Mesh>(mesh);
        if (GameObject* g = gameObject())
            if (Renderer* r = g->GetComponent<Renderer>())
                RendererManager::Get().Touch(*r);
    }

    Mesh* MeshFilter::mesh()
    {
        Mesh* shared = mesh_.Get();
        if (!shared || (owned_.Get() && owned_.Get() == shared))
            return shared;
        Mesh* copy = static_cast<Mesh*>(CloneObject(*shared));
        copy->SetName(Format("{} Instance", shared->Name()));
        owned_ = PPtr<Mesh>(copy);
        SetSharedMesh(copy);
        return copy;
    }

    void MeshFilter::OnDestroyNative()
    {
        if (Mesh* m = owned_.Get())
            DestroyImmediate(m, true);
    }

    // ---------------------------------------------------------------------------------------------- Renderer

    Renderer::Renderer(const Renderer& o)
        : Component(o), materials_(o.materials_), block_(o.block_), materialBlocks_(o.materialBlocks_), localBounds_(o.localBounds_),
          worldBounds_(o.worldBounds_), hasLocalBounds_(o.hasLocalBounds_), hasWorldBounds_(o.hasWorldBounds_), enabled_(o.enabled_),
          receiveShadows_(o.receiveShadows_), staticShadowCaster_(o.staticShadowCaster_), allowOcclusionWhenDynamic_(o.allowOcclusionWhenDynamic_),
          forceRenderingOff_(o.forceRenderingOff_), shadowCasting_(o.shadowCasting_), renderingLayerMask_(o.renderingLayerMask_),
          rendererPriority_(o.rendererPriority_), sortingLayerID_(o.sortingLayerID_), sortingOrder_(o.sortingOrder_)
    {
    }

    void Renderer::Touch() { RendererManager::Get().Touch(*this); }

    void Renderer::OnEnableNative() { RendererManager::Get().Add(*this); }
    void Renderer::OnDisableNative() { RendererManager::Get().Remove(*this); }

    void Renderer::OnDestroyNative()
    {
        RendererManager::Get().Remove(*this);
        for (const PPtr<Material>& m : owned_)
            if (Material* material = m.Get())
                DestroyImmediate(material, true);
        owned_.Clear();
    }

    void Renderer::OnLayerChangedNative() { Touch(); }

    void Renderer::SetEnabled(bool value)
    {
        enabled_ = value;
        Touch();
    }

    bool Renderer::isVisible() const
    {
        u32 i = managerIndex_;
        return i != ~0u && RendererManager::Get().Render().visible[i] != 0;
    }

    Bounds Renderer::bounds() const
    {
        // Up to date at once (the manager's copy follows the transforms at the end of the frame).
        if (hasWorldBounds_)
            return worldBounds_;
        return TransformBounds(localBounds(), localToWorldMatrix());
    }

    void Renderer::SetBounds(const Bounds& value)
    {
        worldBounds_ = value;
        hasWorldBounds_ = true;
        Touch();
    }

    void Renderer::ResetBounds()
    {
        hasWorldBounds_ = false;
        Touch();
    }

    Bounds Renderer::localBounds() const
    {
        if (hasLocalBounds_)
            return localBounds_;
        Mesh* m = RenderedMesh();
        return m ? m->bounds() : Bounds();
    }

    void Renderer::SetLocalBounds(const Bounds& value)
    {
        localBounds_ = value;
        hasLocalBounds_ = true;
        Touch();
    }

    void Renderer::ResetLocalBounds()
    {
        hasLocalBounds_ = false;
        Touch();
    }

    Matrix4x4 Renderer::localToWorldMatrix() const
    {
        Transform* t = transform();
        return t ? t->localToWorldMatrix() : Matrix4x4::identity;
    }

    Matrix4x4 Renderer::worldToLocalMatrix() const
    {
        Transform* t = transform();
        return t ? t->worldToLocalMatrix() : Matrix4x4::identity;
    }

    void Renderer::SetSharedMaterial(Material* material)
    {
        if (materials_.Empty())
            materials_.Push(PPtr<Material>(material));
        else
            materials_[0] = PPtr<Material>(material);
        Touch();
    }

    void Renderer::GetSharedMaterials(Array<Material*>& out) const
    {
        out.Clear();
        for (const PPtr<Material>& m : materials_)
            out.Push(m.Get());
    }

    void Renderer::SetSharedMaterials(Span<Material* const> materials)
    {
        materials_.Clear();
        for (Material* m : materials)
            materials_.Push(PPtr<Material>(m));
        Touch();
    }

    namespace
    {
        // The copy of a shared material that belongs to the renderer.
        Material* Own(PPtr<Material>& slot, PPtr<Material>& owned)
        {
            Material* shared = slot.Get();
            if (!shared || owned.Get() == shared)
                return shared;
            Material* copy = Material::Create(*shared);
            copy->SetName(Format("{} (Instance)", shared->Name()));
            owned = PPtr<Material>(copy);
            slot = PPtr<Material>(copy);
            return copy;
        }
    }

    Material* Renderer::material()
    {
        if (materials_.Empty())
            return nullptr;
        if (owned_.Size() < materials_.Size())
            owned_.Resize(materials_.Size());
        Material* m = Own(materials_[0], owned_[0]);
        Touch();
        return m;
    }

    void Renderer::GetMaterials(Array<Material*>& out)
    {
        out.Clear();
        if (owned_.Size() < materials_.Size())
            owned_.Resize(materials_.Size());
        for (u32 i = 0; i < materials_.Size(); i++)
            out.Push(Own(materials_[i], owned_[i]));
        Touch();
    }

    void Renderer::SetPropertyBlock(const MaterialPropertyBlock* block)
    {
        block_ = block ? block->Sheet() : PropertySheet();
        materialBlocks_.Clear();
    }

    void Renderer::SetPropertyBlock(const MaterialPropertyBlock* block, i32 materialIndex)
    {
        if (materialIndex < 0)
            return;
        if (materialBlocks_.Size() <= usize(materialIndex))
            materialBlocks_.Resize(usize(materialIndex) + 1);
        materialBlocks_[usize(materialIndex)] = block ? block->Sheet() : PropertySheet();
    }

    void Renderer::GetPropertyBlock(MaterialPropertyBlock& out) const
    {
        out.Clear();
        for (const PropertyValue& v : block_.Values())
            out.CopyFrom(block_, v);
    }

    void Renderer::GetPropertyBlock(MaterialPropertyBlock& out, i32 materialIndex) const
    {
        out.Clear();
        if (materialIndex < 0 || usize(materialIndex) >= materialBlocks_.Size())
            return;
        const PropertySheet& s = materialBlocks_[usize(materialIndex)];
        for (const PropertyValue& v : s.Values())
            out.CopyFrom(s, v);
    }

    bool Renderer::HasPropertyBlock() const
    {
        if (!block_.Empty())
            return true;
        for (const PropertySheet& s : materialBlocks_)
            if (!s.Empty())
                return true;
        return false;
    }

    const PropertySheet* Renderer::PropertyBlock(i32 materialIndex) const
    {
        if (materialIndex >= 0 && usize(materialIndex) < materialBlocks_.Size() && !materialBlocks_[usize(materialIndex)].Empty())
            return &materialBlocks_[usize(materialIndex)];
        return block_.Empty() ? nullptr : &block_;
    }

    void Renderer::SetSortingOrder(i32 value)
    {
        sortingOrder_ = value;
        Touch();
    }

    void Renderer::SetForceRenderingOff(bool value)
    {
        forceRenderingOff_ = value;
        Touch();
    }

    Mesh* MeshRenderer::RenderedMesh() const
    {
        GameObject* g = gameObject();
        MeshFilter* f = g ? g->GetComponent<MeshFilter>() : nullptr;
        return f ? f->sharedMesh() : nullptr;
    }
}
