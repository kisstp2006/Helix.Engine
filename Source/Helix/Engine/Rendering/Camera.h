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
    enum class CameraClearFlags : i32
    {
        Skybox = 1,
        SolidColor = 2,
        Depth = 3,
        Nothing = 4,
    };

    enum class CameraType : i32
    {
        Game = 1,
        SceneView = 2,
        Preview = 4,
        VR = 8,
        Reflection = 16,
    };

    enum class DepthTextureMode : i32
    {
        None = 0,
        Depth = 1,
        DepthNormals = 2,
        MotionVectors = 4,
    };

    class Camera final : public Behaviour
    {
        HX_ENGINE_CLASS(Camera, Behaviour, ClassId::Camera, ClassDisallowMultiple)

    public:
        Camera() = default;
        Camera(const Camera& other);

        // The first enabled camera tagged MainCamera; null when there is none.
        static Camera* main();
        // The camera being rendered, null outside rendering.
        static Camera* current();
        static i32 allCamerasCount();
        // The enabled cameras, by depth.
        static void GetAllCameras(Array<Camera*>& out);
        // The size of the default target in pixels (the display; set by the host or the renderer).
        static void SetDisplaySize(u32 width, u32 height);
        static Vector2 DisplaySize();

        f32 fieldOfView() const { return fieldOfView_; }
        void SetFieldOfView(f32 degrees);
        f32 nearClipPlane() const { return near_; }
        void SetNearClipPlane(f32 value) { near_ = value; }
        f32 farClipPlane() const { return far_; }
        void SetFarClipPlane(f32 value) { far_ = value; }
        bool orthographic() const { return orthographic_; }
        void SetOrthographic(bool value) { orthographic_ = value; }
        f32 orthographicSize() const { return orthographicSize_; }
        void SetOrthographicSize(f32 value) { orthographicSize_ = value; }
        // Width over height: the target's unless set.
        f32 aspect() const;
        void SetAspect(f32 value) { aspect_ = value; }
        void ResetAspect() { aspect_ = 0; }
        // Viewport rectangle in 0 to 1 of the target.
        Rect rect() const { return rect_; }
        void SetRect(const Rect& value) { rect_ = value; }
        Rect pixelRect() const;
        void SetPixelRect(const Rect& value);
        i32 pixelWidth() const;
        i32 pixelHeight() const;
        f32 depth() const { return depth_; }
        void SetDepth(f32 value) { depth_ = value; }
        u32 cullingMask() const { return cullingMask_; }
        void SetCullingMask(u32 value) { cullingMask_ = value; }
        u32 eventMask() const { return eventMask_; }
        void SetEventMask(u32 value) { eventMask_ = value; }
        CameraClearFlags clearFlags() const { return clearFlags_; }
        void SetClearFlags(CameraClearFlags value) { clearFlags_ = value; }
        Color backgroundColor() const { return background_; }
        void SetBackgroundColor(Color value) { background_ = value; }
        CameraType cameraType() const { return cameraType_; }
        void SetCameraType(CameraType value) { cameraType_ = value; }
        bool useOcclusionCulling() const { return occlusionCulling_; }
        void SetUseOcclusionCulling(bool value) { occlusionCulling_ = value; }
        bool allowHDR() const { return allowHDR_; }
        void SetAllowHDR(bool value) { allowHDR_ = value; }
        bool allowMSAA() const { return allowMSAA_; }
        void SetAllowMSAA(bool value) { allowMSAA_ = value; }
        DepthTextureMode depthTextureMode() const { return depthTextureMode_; }
        void SetDepthTextureMode(DepthTextureMode value) { depthTextureMode_ = value; }
        // Per layer: objects further than this are culled (0 takes the far plane).
        Span<const f32> layerCullDistances() const { return Span<const f32>(layerCullDistances_, 32); }
        void SetLayerCullDistances(Span<const f32> distances);

        // ---------------------------------------------------------------------------------------------- matrices
        Matrix4x4 worldToCameraMatrix() const;
        void SetWorldToCameraMatrix(const Matrix4x4& value);
        void ResetWorldToCameraMatrix() { customView_ = false; }
        Matrix4x4 cameraToWorldMatrix() const { return worldToCameraMatrix().inverse(); }
        Matrix4x4 projectionMatrix() const;
        void SetProjectionMatrix(const Matrix4x4& value);
        void ResetProjectionMatrix() { customProjection_ = false; }
        Matrix4x4 nonJitteredProjectionMatrix() const { return projectionMatrix(); }
        // The projection as the GPU takes it: clip depth 0 to 1.
        Matrix4x4 GpuProjectionMatrix() const;
        // projection times worldToCamera (what frustum planes come from).
        Matrix4x4 cullingMatrix() const { return projectionMatrix() * worldToCameraMatrix(); }

        // ---------------------------------------------------------------------------------------------- conversions
        Vector3 WorldToViewportPoint(Vector3 position) const;
        Vector3 WorldToScreenPoint(Vector3 position) const;
        Vector3 ViewportToWorldPoint(Vector3 position) const;
        Vector3 ScreenToWorldPoint(Vector3 position) const;
        Vector3 ScreenToViewportPoint(Vector3 position) const;
        Vector3 ViewportToScreenPoint(Vector3 position) const;
        Ray ViewportPointToRay(Vector3 position) const;
        Ray ScreenPointToRay(Vector3 position) const;
        // The corners of the viewport rectangle at distance z, in camera space: bottom left, top left, top right,
        // bottom right.
        void CalculateFrustumCorners(const Rect& viewport, f32 z, Vector3 out[4]) const;

        // The settings of another camera.
        void CopyFrom(const Camera& other);
        // Renders this camera now through the active renderer.
        void Render();

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
        Matrix4x4 projection_;
        Matrix4x4 view_;
    };

    // The frustum of a camera or a matrix, and box tests (planes in the order left, right, bottom, top, near, far).
    struct GeometryUtility
    {
        static void CalculateFrustumPlanes(const Camera& camera, Plane out[6]) { helix::CalculateFrustumPlanes(camera.cullingMatrix(), out); }
        static void CalculateFrustumPlanes(const Matrix4x4& worldToProjection, Plane out[6]) { helix::CalculateFrustumPlanes(worldToProjection, out); }
        static bool TestPlanesAABB(const Plane planes[6], const Bounds& bounds) { return helix::TestPlanesAABB(planes, bounds); }
        // The bounds of points under a matrix.
        static Bounds CalculateBounds(Span<const Vector3> positions, const Matrix4x4& transform);
    };

}
