#include <Helix/Engine/Rendering/Camera.h>
#include <Helix/Engine/Rendering/Graphics.h>
#include <Helix/Engine/Rendering/Light.h>

#include <Helix/Core/Logging/Log.h>
#include <Helix/Math/Mathf.h>

#include <cmath>
#include <cstring>

namespace helix::engine
{
    namespace
    {
        LogChannel CameraLog{"Engine"};
    }

    void (*detail::gRenderStage)(Component*, detail::RenderStage) = nullptr;

    namespace
    {
        struct CameraState
        {
            Array<EntityId> cameras = Array<EntityId>();
            Array<EntityId> lights = Array<EntityId>();
            Array<Light*> lightCache = Array<Light*>();
            EntityId current;
            u32 width = 1, height = 1;
        };

        CameraState& State()
        {
            static CameraState s;
            return s;
        }

        void Remove(Array<EntityId>& list, EntityId id)
        {
            for (usize i = 0; i < list.Size(); i++)
                if (list[i] == id)
                {
                    list.RemoveAt(i);
                    return;
                }
        }
    }

    // ---------------------------------------------------------------------------------------------- camera list

    Camera::Camera(const Camera& o)
        : Behaviour(o), fieldOfView_(o.fieldOfView_), near_(o.near_), far_(o.far_), orthographic_(o.orthographic_),
          orthographicSize_(o.orthographicSize_), aspect_(o.aspect_), rect_(o.rect_), depth_(o.depth_), cullingMask_(o.cullingMask_),
          eventMask_(o.eventMask_), clearFlags_(o.clearFlags_), background_(o.background_), cameraType_(o.cameraType_),
          occlusionCulling_(o.occlusionCulling_), allowHDR_(o.allowHDR_), allowMSAA_(o.allowMSAA_), depthTextureMode_(o.depthTextureMode_),
          customProjection_(o.customProjection_), customView_(o.customView_), customCulling_(o.customCulling_), targetDisplay_(o.targetDisplay_),
          scene_(o.scene_), projection_(o.projection_), view_(o.view_), culling_(o.culling_)
    {
        std::memcpy(layerCullDistances_, o.layerCullDistances_, sizeof(layerCullDistances_));
    }

    void Camera::OnEnableNative() { State().cameras.Push(GetEntityId()); }
    void Camera::OnDisableNative() { Remove(State().cameras, GetEntityId()); }

    Camera* Camera::main()
    {
        for (EntityId id : State().cameras)
            if (Camera* c = EntityIdToObject<Camera>(id); c && c->CompareTag("MainCamera"))
                return c;
        return nullptr;
    }

    Camera* Camera::current() { return EntityIdToObject<Camera>(State().current); }
    void Camera::SetCurrent(Camera* camera) { State().current = camera ? camera->GetEntityId() : EntityId(); }
    i32 Camera::allCamerasCount() { return i32(State().cameras.Size()); }

    void Camera::GetAllCameras(Array<Camera*>& out)
    {
        out.Clear();
        for (EntityId id : State().cameras)
            if (Camera* c = EntityIdToObject<Camera>(id))
                out.Push(c);
        // By depth, then in the order they were enabled.
        for (usize i = 1; i < out.Size(); i++)
            for (usize k = i; k > 0 && out[k - 1]->depth() > out[k]->depth(); k--)
            {
                Camera* t = out[k];
                out[k] = out[k - 1];
                out[k - 1] = t;
            }
    }

    void Camera::SetDisplaySize(u32 width, u32 height)
    {
        State().width = width ? width : 1;
        State().height = height ? height : 1;
    }

    Vector2 Camera::DisplaySize() { return Vector2(f32(State().width), f32(State().height)); }

    // ---------------------------------------------------------------------------------------------- settings

    void Camera::SetFieldOfView(f32 degrees) { fieldOfView_ = degrees < 1e-5f ? 1e-5f : degrees > 179 ? 179 : degrees; }

    Rect Camera::pixelRect() const
    {
        RenderTexture* target = targetTexture();
        Vector2 size = target ? Vector2(f32(target->width()), f32(target->height())) : DisplaySize();
        // The viewport rectangle clipped to the target.
        f32 x0 = rect_.x < 0 ? 0 : rect_.x, y0 = rect_.y < 0 ? 0 : rect_.y;
        f32 x1 = rect_.x + rect_.width > 1 ? 1 : rect_.x + rect_.width, y1 = rect_.y + rect_.height > 1 ? 1 : rect_.y + rect_.height;
        return Rect{x0 * size.x, y0 * size.y, (x1 > x0 ? x1 - x0 : 0) * size.x, (y1 > y0 ? y1 - y0 : 0) * size.y};
    }

    void Camera::SetPixelRect(const Rect& value)
    {
        RenderTexture* target = targetTexture();
        Vector2 size = target ? Vector2(f32(target->width()), f32(target->height())) : DisplaySize();
        rect_ = Rect{value.x / size.x, value.y / size.y, value.width / size.x, value.height / size.y};
    }

    i32 Camera::pixelWidth() const { return i32(pixelRect().width); }
    i32 Camera::pixelHeight() const { return i32(pixelRect().height); }

    f32 Camera::aspect() const
    {
        if (aspect_ > 0)
            return aspect_;
        Rect r = pixelRect();
        return r.height > 0 ? r.width / r.height : 1.0f;
    }

    void Camera::SetLayerCullDistances(Span<const f32> distances)
    {
        for (usize i = 0; i < 32; i++)
            layerCullDistances_[i] = i < distances.Size() ? distances[i] : 0;
    }

    // ---------------------------------------------------------------------------------------------- matrices

    Matrix4x4 Camera::worldToCameraMatrix() const
    {
        if (customView_)
            return view_;
        Transform* t = transform();
        Matrix4x4 world = t ? Matrix4x4::TRS(t->position(), t->rotation(), Vector3::one) : Matrix4x4::identity;
        // Camera space looks along -z.
        return Matrix4x4::Scale(Vector3(1, 1, -1)) * world.inverse();
    }

    void Camera::SetWorldToCameraMatrix(const Matrix4x4& value)
    {
        view_ = value;
        customView_ = true;
    }

    Matrix4x4 Camera::projectionMatrix() const
    {
        if (customProjection_)
            return projection_;
        f32 a = aspect();
        if (orthographic_)
            return Matrix4x4::Ortho(-orthographicSize_ * a, orthographicSize_ * a, -orthographicSize_, orthographicSize_, near_, far_);
        return Matrix4x4::Perspective(fieldOfView_, a, near_, far_);
    }

    void Camera::SetProjectionMatrix(const Matrix4x4& value)
    {
        projection_ = value;
        customProjection_ = true;
    }

    Matrix4x4 Camera::GpuProjectionMatrix() const
    {
        // Clip depth -1..1 to 0..1: z' = (z + w) / 2.
        Matrix4x4 p = projectionMatrix();
        p.m20 = 0.5f * (p.m20 + p.m30);
        p.m21 = 0.5f * (p.m21 + p.m31);
        p.m22 = 0.5f * (p.m22 + p.m32);
        p.m23 = 0.5f * (p.m23 + p.m33);
        return p;
    }

    // ---------------------------------------------------------------------------------------------- conversions

    Vector3 Camera::WorldToViewportPoint(Vector3 position) const
    {
        Vector3 view = worldToCameraMatrix().MultiplyPoint3x4(position);
        Vector4 clip = projectionMatrix() * Vector4(view.x, view.y, view.z, 1.0f);
        f32 w = std::fabs(clip.w) > 1e-20f ? clip.w : 1e-20f;
        return Vector3(clip.x / w * 0.5f + 0.5f, clip.y / w * 0.5f + 0.5f, -view.z);
    }

    Vector3 Camera::ViewportToScreenPoint(Vector3 p) const
    {
        Rect r = pixelRect();
        return Vector3(r.x + p.x * r.width, r.y + p.y * r.height, p.z);
    }

    Vector3 Camera::ScreenToViewportPoint(Vector3 p) const
    {
        Rect r = pixelRect();
        return Vector3(r.width > 0 ? (p.x - r.x) / r.width : 0, r.height > 0 ? (p.y - r.y) / r.height : 0, p.z);
    }

    Vector3 Camera::WorldToScreenPoint(Vector3 position) const { return ViewportToScreenPoint(WorldToViewportPoint(position)); }

    Vector3 Camera::ViewportToWorldPoint(Vector3 p) const
    {
        Matrix4x4 proj = projectionMatrix();
        f32 nx = p.x * 2.0f - 1.0f, ny = p.y * 2.0f - 1.0f, d = p.z;
        Vector3 view;
        if (orthographic_ && !customProjection_)
            view = Vector3((nx - proj.m03) / proj.m00, (ny - proj.m13) / proj.m11, -d);
        else
            view = Vector3(d * (nx + proj.m02) / proj.m00, d * (ny + proj.m12) / proj.m11, -d);
        return cameraToWorldMatrix().MultiplyPoint3x4(view);
    }

    Vector3 Camera::ScreenToWorldPoint(Vector3 position) const { return ViewportToWorldPoint(ScreenToViewportPoint(position)); }

    Ray Camera::ViewportPointToRay(Vector3 position) const
    {
        Vector3 a = ViewportToWorldPoint(Vector3(position.x, position.y, near_));
        Vector3 b = ViewportToWorldPoint(Vector3(position.x, position.y, near_ + 1.0f));
        return Ray(a, b - a);
    }

    Ray Camera::ScreenPointToRay(Vector3 position) const { return ViewportPointToRay(ScreenToViewportPoint(position)); }

    void Camera::CalculateFrustumCorners(const Rect& viewport, f32 z, Vector3 out[4]) const
    {
        const Vector2 corners[4] = {{viewport.x, viewport.y},
                                    {viewport.x, viewport.y + viewport.height},
                                    {viewport.x + viewport.width, viewport.y + viewport.height},
                                    {viewport.x + viewport.width, viewport.y}};
        Matrix4x4 view = worldToCameraMatrix();
        for (u32 i = 0; i < 4; i++)
            out[i] = view.MultiplyPoint3x4(ViewportToWorldPoint(Vector3(corners[i].x, corners[i].y, z)));
    }

    void Camera::CopySettings(const Camera& o)
    {
        fieldOfView_ = o.fieldOfView_, near_ = o.near_, far_ = o.far_, orthographic_ = o.orthographic_, orthographicSize_ = o.orthographicSize_;
        aspect_ = o.aspect_, rect_ = o.rect_, depth_ = o.depth_, cullingMask_ = o.cullingMask_, eventMask_ = o.eventMask_;
        clearFlags_ = o.clearFlags_, background_ = o.background_, cameraType_ = o.cameraType_, occlusionCulling_ = o.occlusionCulling_;
        allowHDR_ = o.allowHDR_, allowMSAA_ = o.allowMSAA_, depthTextureMode_ = o.depthTextureMode_;
        customProjection_ = o.customProjection_, customView_ = o.customView_, projection_ = o.projection_, view_ = o.view_;
        customCulling_ = o.customCulling_, culling_ = o.culling_, targetDisplay_ = o.targetDisplay_, scene_ = o.scene_;
        targetTexture_ = o.targetTexture_, forceIntoRenderTexture_ = o.forceIntoRenderTexture_;
        std::memcpy(layerCullDistances_, o.layerCullDistances_, sizeof(layerCullDistances_));
    }

    void Camera::Reset() { CopySettings(Camera()); }

    Matrix4x4 Camera::CalculateObliqueMatrix(Vector4 clipPlane) const
    {
        // The near plane moved onto the clip plane: the corner of the frustum opposite the plane fixes the scale.
        Matrix4x4 p = projectionMatrix();
        auto sign = [](f32 v) { return v > 0 ? 1.0f : v < 0 ? -1.0f : 0.0f; };
        Vector4 q = p.inverse() * Vector4(sign(clipPlane.x), sign(clipPlane.y), 1, 1);
        f32 d = Vector4::Dot(clipPlane, q);
        if (d == 0)
            return p;
        Vector4 c = clipPlane * (2.0f / d);
        p.m20 = c.x - p.m30, p.m21 = c.y - p.m31, p.m22 = c.z - p.m32, p.m23 = c.w - p.m33;
        return p;
    }

    f32 Camera::FieldOfViewToFocalLength(f32 fieldOfView, f32 sensorSize) { return sensorSize * 0.5f / std::tan(fieldOfView * 0.5f * mathf::Deg2Rad); }
    f32 Camera::FocalLengthToFieldOfView(f32 focalLength, f32 sensorSize)
    {
        if (focalLength < mathf::Epsilon)
        {
            HX_LOG_ERROR(CameraLog, "Camera focal length must be greater than zero.");
            return 0;
        }
        return 2.0f * std::atan(sensorSize * 0.5f / focalLength) * mathf::Rad2Deg;
    }
    f32 Camera::HorizontalToVerticalFieldOfView(f32 horizontalFieldOfView, f32 aspectRatio)
    {
        return 2.0f * std::atan(std::tan(horizontalFieldOfView * 0.5f * mathf::Deg2Rad) / aspectRatio) * mathf::Rad2Deg;
    }
    f32 Camera::VerticalToHorizontalFieldOfView(f32 verticalFieldOfView, f32 aspectRatio)
    {
        return 2.0f * std::atan(std::tan(verticalFieldOfView * 0.5f * mathf::Deg2Rad) * aspectRatio) * mathf::Rad2Deg;
    }

    void Camera::CopyFrom(const Camera& o)
    {
        CopySettings(o);
        if (Transform* t = transform())
            if (Transform* other = o.transform())
                t->SetPositionAndRotation(other->position(), other->rotation());
    }

    void Camera::Render() { Graphics::RenderCamera(*this); }

    Array<Plane> GeometryUtility::FrustumPlanesOf(Camera* camera)
    {
        Array<Plane> out;
        out.Resize(6);
        if (camera)
            CalculateFrustumPlanes(*camera, out.Data());
        return out;
    }

    Array<Plane> GeometryUtility::FrustumPlanesOf(Matrix4x4 worldToProjectionMatrix)
    {
        Array<Plane> out;
        out.Resize(6);
        CalculateFrustumPlanes(worldToProjectionMatrix, out.Data());
        return out;
    }

    bool GeometryUtility::TestPlanes(Span<const Plane> planes, Bounds bounds)
    {
        Vector3 c = bounds.center, e = bounds.extents;
        for (const Plane& p : planes)
        {
            Vector3 n = p.normal;
            f32 r = e.x * std::fabs(n.x) + e.y * std::fabs(n.y) + e.z * std::fabs(n.z);
            if (Vector3::Dot(n, c) + p.distance + r < 0)
                return false;
        }
        return true;
    }

    bool GeometryUtility::TryCreatePlaneFromPolygon(Span<const Vector3> vertices, Plane& plane)
    {
        plane = Plane();
        if (vertices.Size() < 3)
            return false;
        Vector3 n, centroid;
        for (usize i = 0; i < vertices.Size(); i++)
        {
            const Vector3& a = vertices[i];
            const Vector3& b = vertices[(i + 1) % vertices.Size()];
            n.x += (a.y - b.y) * (a.z + b.z);
            n.y += (a.z - b.z) * (a.x + b.x);
            n.z += (a.x - b.x) * (a.y + b.y);
            centroid += a;
        }
        f32 length = n.magnitude();
        if (length < 1e-12f)
            return false;
        centroid /= f32(vertices.Size());
        plane = Plane(n / length, centroid);
        return true;
    }

    Bounds GeometryUtility::CalculateBounds(Span<const Vector3> positions, const Matrix4x4& transform)
    {
        Bounds b;
        if (positions.Empty())
            return b;
        Vector3 first = transform.MultiplyPoint3x4(positions[0]);
        Vector3 mn = first, mx = first;
        for (const Vector3& p : positions)
        {
            Vector3 q = transform.MultiplyPoint3x4(p);
            mn = Vector3(q.x < mn.x ? q.x : mn.x, q.y < mn.y ? q.y : mn.y, q.z < mn.z ? q.z : mn.z);
            mx = Vector3(q.x > mx.x ? q.x : mx.x, q.y > mx.y ? q.y : mx.y, q.z > mx.z ? q.z : mx.z);
        }
        b.SetMinMax(mn, mx);
        return b;
    }

    // ---------------------------------------------------------------------------------------------- lights

    void Light::OnEnableNative() { State().lights.Push(GetEntityId()); }
    void Light::OnDisableNative() { Remove(State().lights, GetEntityId()); }

    Span<Light* const> Light::EnabledLights()
    {
        CameraState& s = State();
        s.lightCache.Clear();
        for (EntityId id : s.lights)
            if (Light* l = EntityIdToObject<Light>(id))
                s.lightCache.Push(l);
        return Span<Light* const>(s.lightCache.Data(), s.lightCache.Size());
    }

    namespace
    {
        // A blackbody color for a temperature in kelvin (a fit of the Planck locus), 0 to 1.
        Color Temperature(f32 kelvin)
        {
            f32 t = (kelvin < 1000 ? 1000 : kelvin > 40000 ? 40000 : kelvin) / 100.0f;
            f32 r, g, b;
            if (t <= 66)
            {
                r = 255;
                g = 99.4708025861f * std::log(t) - 161.1195681661f;
                b = t <= 19 ? 0 : 138.5177312231f * std::log(t - 10) - 305.0447927307f;
            }
            else
            {
                r = 329.698727446f * std::pow(t - 60, -0.1332047592f);
                g = 288.1221695283f * std::pow(t - 60, -0.0755148492f);
                b = 255;
            }
            auto unit = [](f32 v) { return (v < 0 ? 0 : v > 255 ? 255 : v) / 255.0f; };
            return Color(unit(r), unit(g), unit(b), 1);
        }
    }

    void Light::Reset()
    {
        const Light d;
        type_ = d.type_, color_ = d.color_, intensity_ = d.intensity_, range_ = d.range_, spotAngle_ = d.spotAngle_, innerSpotAngle_ = d.innerSpotAngle_;
        shadows_ = d.shadows_, shadowStrength_ = d.shadowStrength_, shadowBias_ = d.shadowBias_, shadowNormalBias_ = d.shadowNormalBias_;
        shadowNearPlane_ = d.shadowNearPlane_, cullingMask_ = d.cullingMask_, renderingLayerMask_ = d.renderingLayerMask_;
        bounceIntensity_ = d.bounceIntensity_, colorTemperature_ = d.colorTemperature_, useColorTemperature_ = d.useColorTemperature_;
        renderMode_ = d.renderMode_;
    }

    Color Light::FinalColor() const
    {
        Color c = color_;
        if (useColorTemperature_)
        {
            Color k = Temperature(colorTemperature_);
            c = Color(c.r * k.r, c.g * k.g, c.b * k.b, c.a);
        }
        return Color(c.r * intensity_, c.g * intensity_, c.b * intensity_, c.a);
    }
}
