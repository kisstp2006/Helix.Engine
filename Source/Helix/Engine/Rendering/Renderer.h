#pragma once

#include <Helix/Engine/GameObject/GameObject.h>
#include <Helix/Engine/Material/Material.h>
#include <Helix/Engine/Mesh/Mesh.h>

// MeshFilter, Renderer and MeshRenderer. The components are thin: what drawing needs lives in the renderer manager's
// dense arrays (world matrix, bounds, mesh, layer, flags), which follow the Transform system and are copied for the
// render side at the end of the frame.
namespace helix::engine
{
    HX_ENUM(Namespace = "HelixEngine.Rendering")
    enum class ShadowCastingMode : i32
    {
        Off = 0,
        On = 1,
        TwoSided = 2,
        ShadowsOnly = 3,
    };

    HX_CLASS(Namespace = "HelixEngine", Sealed)
    class MeshFilter final : public Component
    {
        HX_ENGINE_CLASS(MeshFilter, Component, ClassId::MeshFilter, ClassDisallowMultiple)

    public:
        MeshFilter() = default;
        MeshFilter(const MeshFilter& other) : Component(other), mesh_(other.mesh_) {}

        HX_PROPERTY() Mesh* sharedMesh() const { return mesh_.Get(); }
        void SetSharedMesh(Mesh* mesh);
        // The filter's own copy of the shared mesh, made on first access (later accesses give the same copy).
        HX_PROPERTY() Mesh* mesh();
        void SetMesh(Mesh* mesh) { SetSharedMesh(mesh); }

        void OnDestroyNative();

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("m_Mesh", mesh_);
        }

    private:
        PPtr<Mesh> mesh_;
        // The copy `mesh()` made; destroyed with the filter.
        PPtr<Mesh> owned_;
    };

    class Renderer;
    namespace detail
    {
        // Called when a renderer becomes visible or invisible to the cameras (the script runtime's OnBecameVisible /
        // OnBecameInvisible).
        extern HX_IMPORT_DATA void (*gVisibilityChanged)(Renderer* renderer, bool visible);
    }

    HX_CLASS(Namespace = "HelixEngine")
    class Renderer : public Component
    {
        HX_ENGINE_CLASS(Renderer, Component, ClassId::Renderer, ClassAbstract | ClassDisallowMultiple)

    public:
        HX_PROPERTY() bool enabled() const { return enabled_; }
        void SetEnabled(bool value);
        // Some camera drew it in the last rendered frame.
        HX_PROPERTY(ReadOnly) bool isVisible() const;

        // World bounds; an override set by SetBounds stays until ResetBounds.
        HX_PROPERTY() Bounds bounds() const;
        void SetBounds(const Bounds& value);
        HX_FUNCTION() void ResetBounds();
        // Bounds in the GameObject's space (the mesh's by default).
        HX_PROPERTY() Bounds localBounds() const;
        void SetLocalBounds(const Bounds& value);
        HX_FUNCTION() void ResetLocalBounds();
        HX_PROPERTY(ReadOnly) Matrix4x4 localToWorldMatrix() const;
        HX_PROPERTY(ReadOnly) Matrix4x4 worldToLocalMatrix() const;

        HX_PROPERTY() Material* sharedMaterial() const { return materials_.Empty() ? nullptr : materials_[0].Get(); }
        void SetSharedMaterial(Material* material);
        void GetSharedMaterials(Array<Material*>& out) const;
        void SetSharedMaterials(Span<Material* const> materials);
        i32 sharedMaterialCount() const { return i32(materials_.Size()); }
        // The renderer's own copies of its materials, made on first access.
        HX_PROPERTY() Material* material();
        void SetMaterial(Material* material) { SetSharedMaterial(material); }
        void GetMaterials(Array<Material*>& out);
        void SetMaterials(Span<Material* const> materials) { SetSharedMaterials(materials); }
        HX_PROPERTY(Name = "sharedMaterials", Setter = "SetSharedMaterials") Array<Material*> SharedMaterialsApi() const
        {
            Array<Material*> out;
            GetSharedMaterials(out);
            return out;
        }
        HX_PROPERTY(Name = "materials", Setter = "SetMaterials") Array<Material*> MaterialsApi()
        {
            Array<Material*> out;
            GetMaterials(out);
            return out;
        }

        // Values on top of the materials; with a material index for that material only.
        void SetPropertyBlock(const MaterialPropertyBlock* block);
        void SetPropertyBlock(const MaterialPropertyBlock* block, i32 materialIndex);
        void GetPropertyBlock(MaterialPropertyBlock& out) const;
        void GetPropertyBlock(MaterialPropertyBlock& out, i32 materialIndex) const;
        HX_FUNCTION() bool HasPropertyBlock() const;
        // The blocks by id (C#'s MaterialPropertyBlock is no native object; a C++ script passes its block).
        HX_FUNCTION(Name = "SetPropertyBlock", Internal) void SetPropertyBlockById(PropertyBlockId properties) { SetPropertyBlock(MaterialPropertyBlock::Find(properties)); }
        HX_FUNCTION(Name = "SetPropertyBlock", Internal) void SetPropertyBlockById(PropertyBlockId properties, i32 materialIndex)
        {
            SetPropertyBlock(MaterialPropertyBlock::Find(properties), materialIndex);
        }
        HX_FUNCTION(Name = "GetPropertyBlock", Internal) void GetPropertyBlockById(PropertyBlockId dest) const
        {
            if (MaterialPropertyBlock* b = MaterialPropertyBlock::Find(dest))
                GetPropertyBlock(*b);
        }
        HX_FUNCTION(Name = "GetPropertyBlock", Internal) void GetPropertyBlockById(PropertyBlockId dest, i32 materialIndex) const
        {
            if (MaterialPropertyBlock* b = MaterialPropertyBlock::Find(dest))
                GetPropertyBlock(*b, materialIndex);
        }

        HX_PROPERTY() ShadowCastingMode shadowCastingMode() const { return shadowCasting_; }
        void SetShadowCastingMode(ShadowCastingMode value) { shadowCasting_ = value; }
        HX_PROPERTY() bool receiveShadows() const { return receiveShadows_; }
        void SetReceiveShadows(bool value) { receiveShadows_ = value; }
        HX_PROPERTY() bool staticShadowCaster() const { return staticShadowCaster_; }
        void SetStaticShadowCaster(bool value) { staticShadowCaster_ = value; }
        HX_PROPERTY() u32 renderingLayerMask() const { return renderingLayerMask_; }
        void SetRenderingLayerMask(u32 value) { renderingLayerMask_ = value; }
        HX_PROPERTY() i32 rendererPriority() const { return rendererPriority_; }
        void SetRendererPriority(i32 value) { rendererPriority_ = value; }
        HX_PROPERTY() i32 sortingLayerID() const { return sortingLayerID_; }
        void SetSortingLayerID(i32 value) { sortingLayerID_ = value; }
        HX_PROPERTY() i32 sortingOrder() const { return sortingOrder_; }
        void SetSortingOrder(i32 value);
        HX_PROPERTY() bool forceRenderingOff() const { return forceRenderingOff_; }
        void SetForceRenderingOff(bool value);
        HX_PROPERTY() bool allowOcclusionWhenDynamic() const { return allowOcclusionWhenDynamic_; }
        void SetAllowOcclusionWhenDynamic(bool value) { allowOcclusionWhenDynamic_ = value; }

        // ---------------------------------------------------------------------------------------------- native
        // The mesh this renderer draws (MeshRenderer: the MeshFilter's).
        virtual Mesh* RenderedMesh() const = 0;
        Span<const PPtr<Material>> Materials() const { return Span<const PPtr<Material>>(materials_.Data(), materials_.Size()); }
        // The values of the property blocks for one material (the block of the index over the general one).
        const PropertySheet* PropertyBlock(i32 materialIndex) const;
        // The index of the renderer in the manager's arrays, ~0u while it is not taking part.
        u32 ManagerIndex() const { return managerIndex_; }

        void OnEnableNative();
        void OnDisableNative();
        void OnDestroyNative();
        void OnLayerChangedNative();

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            u8 e = enabled_;
            v.Field("m_Enabled", e);
            enabled_ = e != 0;
            v.Field("m_CastShadows", shadowCasting_);
            v.Field("m_ReceiveShadows", receiveShadows_);
            v.Field("m_StaticShadowCaster", staticShadowCaster_);
            v.Field("m_AllowOcclusionWhenDynamic", allowOcclusionWhenDynamic_);
            v.Field("m_RenderingLayerMask", renderingLayerMask_);
            v.Field("m_RendererPriority", rendererPriority_);
            v.Field("m_Materials", materials_);
            v.Field("m_SortingLayerID", sortingLayerID_);
            v.Field("m_SortingOrder", sortingOrder_);
        }

    protected:
        Renderer() = default;
        Renderer(const Renderer& other);

        // Something drawing depends on changed (materials, mesh, flags): the manager copies it again.
        void Touch();

    private:
        friend struct RendererAccess;
        Array<PPtr<Material>> materials_ = Array<PPtr<Material>>();
        // Copies `material()` made, by material index (null where the shared one is used).
        Array<PPtr<Material>> owned_ = Array<PPtr<Material>>();
        PropertySheet block_;
        Array<PropertySheet> materialBlocks_ = Array<PropertySheet>();
        Bounds localBounds_;
        Bounds worldBounds_;
        bool hasLocalBounds_ = false;
        bool hasWorldBounds_ = false;
        bool enabled_ = true;
        bool receiveShadows_ = true;
        bool staticShadowCaster_ = false;
        bool allowOcclusionWhenDynamic_ = true;
        bool forceRenderingOff_ = false;
        ShadowCastingMode shadowCasting_ = ShadowCastingMode::On;
        u32 renderingLayerMask_ = 1;
        i32 rendererPriority_ = 0;
        i32 sortingLayerID_ = 0;
        i32 sortingOrder_ = 0;
        u32 managerIndex_ = ~0u;
    };

    HX_CLASS(Namespace = "HelixEngine", Sealed)
    class MeshRenderer final : public Renderer
    {
        HX_ENGINE_CLASS(MeshRenderer, Renderer, ClassId::MeshRenderer, ClassDisallowMultiple)

    public:
        MeshRenderer() = default;
        MeshRenderer(const MeshRenderer& other) : Renderer(other), additionalVertexStreams_(other.additionalVertexStreams_) {}

        Mesh* additionalVertexStreams() const { return additionalVertexStreams_.Get(); }
        void SetAdditionalVertexStreams(Mesh* mesh) { additionalVertexStreams_ = PPtr<Mesh>(mesh); }
        // Static batching combines meshes; without it the first submesh is 0.
        i32 subMeshStartIndex() const { return 0; }

        Mesh* RenderedMesh() const override;

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("m_AdditionalVertexStreams", additionalVertexStreams_);
        }

    private:
        PPtr<Mesh> additionalVertexStreams_;
    };
}
