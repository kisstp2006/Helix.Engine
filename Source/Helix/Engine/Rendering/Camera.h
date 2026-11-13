#pragma once

#include <Helix/Engine/GameObject/GameObject.h>
#include <Helix/Engine/Math/MathTransfer.h>
#include <Helix/Math/Color.h>
#include <Helix/Math/Geometry.h>

// Camera: a view of the scene. Camera space is right handed with the view along -z (worldToCameraMatrix); projection
// matrices have the reference's form (clip depth -1 to 1), and the renderer turns them into what the GPU takes. Screen
// coordinates are pixels from the bottom left of the target, viewport coordinates 0 to 1, z the distance in world units.
namespace helix::engine
{
    HX_ENUM(Namespace = "HelixEngine")
    enum class CameraClearFlags : i32
    {
        Skybox = 1,
        SolidColor = 2,
        Depth = 3,
        Nothing = 4,
    };

    HX_ENUM(Namespace = "HelixEngine")
    enum class CameraType : i32
    {
        Game = 1,
        SceneView = 2,
        Preview = 4,
        VR = 8,
        Reflection = 16,
    };

    HX_ENUM(Namespace = "HelixEngine", Attributes = "[System.Flags]")
    enum class DepthTextureMode : i32
    {
        None = 0,
        Depth = 1,
        DepthNormals = 2,
        MotionVectors = 4,
    };

    namespace detail
    {
        // Where rendering is (the script runtime's camera messages): around a camera's culling and drawing, for each
        // renderer it draws, and after it drew.
        enum class RenderStage : u8
        {
            PreCull,
            PreRender,
            PostRender,
            WillRenderObject,
            RenderObject,
        };
        // `at` is the camera, the renderer (WillRenderObject) or null (RenderObject).
        extern HX_IMPORT_DATA void (*gRenderStage)(Component* at, RenderStage stage);
    }

    HX_CLASS(Namespace = "HelixEngine", Sealed)
    class Camera final : public Behaviour
    {
        HX_ENGINE_CLASS(Camera, Behaviour, ClassId::Camera, ClassDisallowMultiple)

    public:
        Camera() = default;
        Camera(const Camera& other);

        // The first enabled camera tagged MainCamera; null when there is none.
        HX_PROPERTY(ReadOnly) static Camera* main();
        // The camera being rendered, null outside rendering.
        HX_PROPERTY(ReadOnly) static Camera* current();
        HX_PROPERTY(ReadOnly) static i32 allCamerasCount();
        // The enabled cameras, by depth.
        static void GetAllCameras(Array<Camera*>& out);
        HX_PROPERTY(Name = "allCameras", ReadOnly) static Array<Camera*> AllCamerasApi()
        {
            Array<Camera*> out;
            GetAllCameras(out);
            return out;
        }
        // The size of the default target in pixels (the display; set by the host or the renderer).
        static void SetDisplaySize(u32 width, u32 height);
        static Vector2 DisplaySize();

        HX_PROPERTY() f32 fieldOfView() const { return fieldOfView_; }
        void SetFieldOfView(f32 degrees);
        HX_PROPERTY() f32 nearClipPlane() const { return near_; }
        void SetNearClipPlane(f32 value) { near_ = value; }
        HX_PROPERTY() f32 farClipPlane() const { return far_; }
        void SetFarClipPlane(f32 value) { far_ = value; }
        HX_PROPERTY() bool orthographic() const { return orthographic_; }
        void SetOrthographic(bool value) { orthographic_ = value; }
        HX_PROPERTY() f32 orthographicSize() const { return orthographicSize_; }
        void SetOrthographicSize(f32 value) { orthographicSize_ = value; }
        // Width over height: the target's unless set.
        HX_PROPERTY() f32 aspect() const;
        void SetAspect(f32 value) { aspect_ = value; }
        HX_FUNCTION() void ResetAspect() { aspect_ = 0; }
        // Viewport rectangle in 0 to 1 of the target.
        HX_PROPERTY() Rect rect() const { return rect_; }
        void SetRect(const Rect& value) { rect_ = value; }
        HX_PROPERTY() Rect pixelRect() const;
        void SetPixelRect(const Rect& value);
        HX_PROPERTY(ReadOnly) i32 pixelWidth() const;
        HX_PROPERTY(ReadOnly) i32 pixelHeight() const;
        // Without dynamic resolution the scaled size is the size.
        HX_PROPERTY(ReadOnly) i32 scaledPixelWidth() const { return pixelWidth(); }
        HX_PROPERTY(ReadOnly) i32 scaledPixelHeight() const { return pixelHeight(); }
        // The display the camera draws to (one display until M7's multi display output).
        HX_PROPERTY() i32 targetDisplay() const { return targetDisplay_; }
        void SetTargetDisplay(i32 value) { targetDisplay_ = value; }
        // Only the objects of this scene are drawn when it is valid (with M7's culling; stored until then).
        HX_PROPERTY() Scene scene() const { return scene_; }
        void SetScene(Scene value) { scene_ = value; }
        HX_PROPERTY() f32 depth() const { return depth_; }
        void SetDepth(f32 value) { depth_ = value; }
        u32 cullingMask() const { return cullingMask_; }
        void SetCullingMask(u32 value) { cullingMask_ = value; }
        u32 eventMask() const { return eventMask_; }
        void SetEventMask(u32 value) { eventMask_ = value; }
        // The masks as the script API has them (int).
        HX_PROPERTY(Name = "cullingMask", Setter = "SetCullingMaskApi") i32 CullingMaskApi() const { return i32(cullingMask_); }
        void SetCullingMaskApi(i32 value) { cullingMask_ = u32(value); }
        HX_PROPERTY(Name = "eventMask", Setter = "SetEventMaskApi") i32 EventMaskApi() const { return i32(eventMask_); }
        void SetEventMaskApi(i32 value) { eventMask_ = u32(value); }
        HX_PROPERTY() CameraClearFlags clearFlags() const { return clearFlags_; }
        void SetClearFlags(CameraClearFlags value) { clearFlags_ = value; }
        HX_PROPERTY() Color backgroundColor() const { return background_; }
        void SetBackgroundColor(Color value) { background_ = value; }
        HX_PROPERTY() CameraType cameraType() const { return cameraType_; }
        void SetCameraType(CameraType value) { cameraType_ = value; }
        HX_PROPERTY() bool useOcclusionCulling() const { return occlusionCulling_; }
        void SetUseOcclusionCulling(bool value) { occlusionCulling_ = value; }
        HX_PROPERTY() bool allowHDR() const { return allowHDR_; }
        void SetAllowHDR(bool value) { allowHDR_ = value; }
        HX_PROPERTY() bool allowMSAA() const { return allowMSAA_; }
        void SetAllowMSAA(bool value) { allowMSAA_ = value; }
        HX_PROPERTY() DepthTextureMode depthTextureMode() const { return depthTextureMode_; }
        void SetDepthTextureMode(DepthTextureMode value) { depthTextureMode_ = value; }
        // Per layer: objects further than this are culled (0 takes the far plane).
        Span<const f32> layerCullDistances() const { return Span<const f32>(layerCullDistances_, 32); }
        void SetLayerCullDistances(Span<const f32> distances);
        HX_PROPERTY(Name = "layerCullDistances", Setter = "SetLayerCullDistances") Array<f32> LayerCullDistancesApi() const
        {
            Array<f32> out;
            out.Append(layerCullDistances_, 32);
            return out;
        }

        // ---------------------------------------------------------------------------------------------- matrices
        HX_PROPERTY() Matrix4x4 worldToCameraMatrix() const;
        void SetWorldToCameraMatrix(const Matrix4x4& value);
        HX_FUNCTION() void ResetWorldToCameraMatrix() { customView_ = false; }
        HX_PROPERTY(ReadOnly) Matrix4x4 cameraToWorldMatrix() const { return worldToCameraMatrix().inverse(); }
        HX_PROPERTY() Matrix4x4 projectionMatrix() const;
        void SetProjectionMatrix(const Matrix4x4& value);
        HX_FUNCTION() void ResetProjectionMatrix() { customProjection_ = false; }
        HX_PROPERTY() Matrix4x4 nonJitteredProjectionMatrix() const { return projectionMatrix(); }
        void SetNonJitteredProjectionMatrix(const Matrix4x4& value) { SetProjectionMatrix(value); }
        // The projection as the GPU takes it: clip depth 0 to 1.
        Matrix4x4 GpuProjectionMatrix() const;
        // projection times worldToCamera (what frustum planes come from) unless set.
        HX_PROPERTY() Matrix4x4 cullingMatrix() const { return customCulling_ ? culling_ : projectionMatrix() * worldToCameraMatrix(); }
        void SetCullingMatrix(const Matrix4x4& value) { culling_ = value, customCulling_ = true; }
        HX_FUNCTION() void ResetCullingMatrix() { customCulling_ = false; }
        // The projection with its near plane on clipPlane (a plane in camera space).
        HX_FUNCTION() Matrix4x4 CalculateObliqueMatrix(Vector4 clipPlane) const;
        HX_FUNCTION() static f32 FieldOfViewToFocalLength(f32 fieldOfView, f32 sensorSize);
        HX_FUNCTION() static f32 FocalLengthToFieldOfView(f32 focalLength, f32 sensorSize);
        HX_FUNCTION() static f32 HorizontalToVerticalFieldOfView(f32 horizontalFieldOfView, f32 aspectRatio);
        HX_FUNCTION() static f32 VerticalToHorizontalFieldOfView(f32 verticalFieldOfView, f32 aspectRatio);

        // ---------------------------------------------------------------------------------------------- conversions
        HX_FUNCTION() Vector3 WorldToViewportPoint(Vector3 position) const;
        HX_FUNCTION() Vector3 WorldToScreenPoint(Vector3 position) const;
        HX_FUNCTION() Vector3 ViewportToWorldPoint(Vector3 position) const;
        HX_FUNCTION() Vector3 ScreenToWorldPoint(Vector3 position) const;
        HX_FUNCTION() Vector3 ScreenToViewportPoint(Vector3 position) const;
        HX_FUNCTION() Vector3 ViewportToScreenPoint(Vector3 position) const;
        HX_FUNCTION() Ray ViewportPointToRay(Vector3 position) const;
        HX_FUNCTION() Ray ScreenPointToRay(Vector3 position) const;
        // The corners of the viewport rectangle at distance z, in camera space: bottom left, top left, top right,
        // bottom right.
        void CalculateFrustumCorners(const Rect& viewport, f32 z, Vector3 out[4]) const;
        // The corners for C#'s CalculateFrustumCorners (which fills an array).
        HX_FUNCTION(Internal) Array<Vector3> FrustumCorners(Rect viewport, f32 z) const
        {
            Vector3 c[4];
            CalculateFrustumCorners(viewport, z, c);
            Array<Vector3> out;
            out.Append(c, 4);
            return out;
        }

        // The settings of another camera, and its transform's position and rotation.
        void CopyFrom(const Camera& other);
        HX_FUNCTION(Name = "CopyFrom") void CopyFromApi(Camera* other)
        {
            if (other)
                CopyFrom(*other);
        }
        // The settings of a new camera.
        HX_FUNCTION() void Reset();
        // Renders this camera now through the active renderer.
        HX_FUNCTION() void Render();

        void OnEnableNative();
        void OnDisableNative();
        static void SetCurrent(Camera* camera);

        template <class V>
        void Transfer(V& v)
        {
            Base::Transfer(v);
            v.Field("m_ClearFlags", clearFlags_);
            v.Field("m_BackGroundColor", background_);
            v.Field("m_NormalizedViewPortRect", rect_);
            v.Field("near clip plane", near_);
            v.Field("far clip plane", far_);
            v.Field("field of view", fieldOfView_);
            v.Field("orthographic", orthographic_);
            v.Field("orthographic size", orthographicSize_);
            v.Field("m_Depth", depth_);
            v.Field("m_CullingMask", cullingMask_);
            v.Field("m_OcclusionCulling", occlusionCulling_);
            v.Field("m_AllowHDR", allowHDR_);
            v.Field("m_AllowMSAA", allowMSAA_);
        }

    private:

        f32 fieldOfView_ = 60;
        f32 near_ = 0.3f;
        f32 far_ = 1000;
        bool orthographic_ = false;
        f32 orthographicSize_ = 5;
        f32 aspect_ = 0;
        Rect rect_{0, 0, 1, 1};
        f32 depth_ = 0;
        u32 cullingMask_ = ~0u;
        u32 eventMask_ = ~0u;
        CameraClearFlags clearFlags_ = CameraClearFlags::Skybox;
        Color background_{0.19215687f, 0.3019608f, 0.4745098f, 0};
        CameraType cameraType_ = CameraType::Game;
        bool occlusionCulling_ = true;
        bool allowHDR_ = true;
        bool allowMSAA_ = true;
        DepthTextureMode depthTextureMode_ = DepthTextureMode::None;
        f32 layerCullDistances_[32] = {};
        bool customProjection_ = false;
        bool customView_ = false;
        bool customCulling_ = false;
        i32 targetDisplay_ = 0;
        Scene scene_;
        Matrix4x4 projection_;
        Matrix4x4 view_;
        Matrix4x4 culling_;

        void CopySettings(const Camera& other);
    };

    // The frustum of a camera or a matrix, and box tests (planes in the order left, right, bottom, top, near, far).
    HX_CLASS(Namespace = "HelixEngine", Static)
    class GeometryUtility
    {
    public:
        static void CalculateFrustumPlanes(const Camera& camera, Plane out[6]) { helix::CalculateFrustumPlanes(camera.cullingMatrix(), out); }
        static void CalculateFrustumPlanes(const Matrix4x4& worldToProjection, Plane out[6]) { helix::CalculateFrustumPlanes(worldToProjection, out); }
        static bool TestPlanesAABB(const Plane planes[6], const Bounds& bounds) { return helix::TestPlanesAABB(planes, bounds); }
        // The bounds of points under a matrix.
        HX_FUNCTION() static Bounds CalculateBounds(Span<const Vector3> positions, const Matrix4x4& transform);
        HX_FUNCTION(Name = "CalculateFrustumPlanes") static Array<Plane> FrustumPlanesOf(Camera* camera);
        HX_FUNCTION(Name = "CalculateFrustumPlanes") static Array<Plane> FrustumPlanesOf(Matrix4x4 worldToProjectionMatrix);
        // Any number of planes: the box is outside when it is fully behind one of them.
        HX_FUNCTION(Name = "TestPlanesAABB") static bool TestPlanes(Span<const Plane> planes, Bounds bounds);
        // The plane of a polygon (Newell's normal through the centroid); false for fewer than 3 points or no area.
        HX_FUNCTION(Out = "plane") static bool TryCreatePlaneFromPolygon(Span<const Vector3> vertices, Plane& plane);
    };

}
