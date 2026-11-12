#pragma once

#include <Helix/Math/Color.h>
#include <Helix/Math/Geometry.h>
#include <Helix/Math/Random.h>
#include <Helix/Scripting/Bindings/Tags.h>

// The members of the C# value types that are more than arithmetic, as calls into Helix.Math (internal in C#): one
// implementation for both script languages, so they give the same results. Leaf calls: no GC transition.
namespace helix::engine
{
    HX_CLASS(Namespace = "HelixEngine.Internal", Static, Internal)
    class MathNative
    {
    public:
        // Vector3
        HX_FUNCTION(Leaf) static Vector3 Vector3Slerp(Vector3 a, Vector3 b, f32 t) { return Vector3::Slerp(a, b, t); }
        HX_FUNCTION(Leaf) static Vector3 Vector3SlerpUnclamped(Vector3 a, Vector3 b, f32 t) { return Vector3::SlerpUnclamped(a, b, t); }
        HX_FUNCTION(Leaf)
        static Vector3 Vector3RotateTowards(Vector3 current, Vector3 target, f32 maxRadiansDelta, f32 maxMagnitudeDelta)
        {
            return Vector3::RotateTowards(current, target, maxRadiansDelta, maxMagnitudeDelta);
        }
        HX_FUNCTION(Leaf) static void Vector3OrthoNormalize(Vector3& normal, Vector3& tangent) { Vector3::OrthoNormalize(normal, tangent); }
        HX_FUNCTION(Leaf)
        static void Vector3OrthoNormalize3(Vector3& normal, Vector3& tangent, Vector3& binormal) { Vector3::OrthoNormalize(normal, tangent, binormal); }
        HX_FUNCTION(Leaf)
        static Vector3 Vector3SmoothDamp(Vector3 current, Vector3 target, Vector3& currentVelocity, f32 smoothTime, f32 maxSpeed, f32 deltaTime)
        {
            return Vector3::SmoothDamp(current, target, currentVelocity, smoothTime, maxSpeed, deltaTime);
        }

        // Quaternion
        HX_FUNCTION(Leaf) static Quaternion QuaternionEuler(f32 x, f32 y, f32 z) { return Quaternion::Euler(x, y, z); }
        HX_FUNCTION(Leaf) static Vector3 QuaternionEulerAngles(Quaternion q) { return q.eulerAngles(); }
        HX_FUNCTION(Leaf) static Quaternion QuaternionAngleAxis(f32 angle, Vector3 axis) { return Quaternion::AngleAxis(angle, axis); }
        HX_FUNCTION(Leaf) static void QuaternionToAngleAxis(Quaternion q, f32& angle, Vector3& axis) { q.ToAngleAxis(angle, axis); }
        HX_FUNCTION(Leaf) static Quaternion QuaternionLookRotation(Vector3 forward, Vector3 upwards) { return Quaternion::LookRotation(forward, upwards); }
        HX_FUNCTION(Leaf)
        static Quaternion QuaternionFromToRotation(Vector3 fromDirection, Vector3 toDirection) { return Quaternion::FromToRotation(fromDirection, toDirection); }
        HX_FUNCTION(Leaf) static Quaternion QuaternionNormalize(Quaternion q) { return Quaternion::Normalize(q); }
        HX_FUNCTION(Leaf) static f32 QuaternionAngle(Quaternion a, Quaternion b) { return Quaternion::Angle(a, b); }
        HX_FUNCTION(Leaf) static Quaternion QuaternionSlerpUnclamped(Quaternion a, Quaternion b, f32 t) { return Quaternion::SlerpUnclamped(a, b, t); }
        HX_FUNCTION(Leaf) static Quaternion QuaternionLerpUnclamped(Quaternion a, Quaternion b, f32 t) { return Quaternion::LerpUnclamped(a, b, t); }
        HX_FUNCTION(Leaf)
        static Quaternion QuaternionRotateTowards(Quaternion from, Quaternion to, f32 maxDegreesDelta) { return Quaternion::RotateTowards(from, to, maxDegreesDelta); }

        // Matrix4x4
        HX_FUNCTION(Leaf) static Matrix4x4 MatrixInverse(Matrix4x4 m) { return m.inverse(); }
        HX_FUNCTION(Leaf) static f32 MatrixDeterminant(Matrix4x4 m) { return m.determinant(); }
        HX_FUNCTION(Leaf) static Matrix4x4 MatrixTRS(Vector3 pos, Quaternion q, Vector3 s) { return Matrix4x4::TRS(pos, q, s); }
        HX_FUNCTION(Leaf) static Matrix4x4 MatrixRotate(Quaternion q) { return Matrix4x4::Rotate(q); }
        HX_FUNCTION(Leaf)
        static Matrix4x4 MatrixPerspective(f32 fov, f32 aspect, f32 zNear, f32 zFar) { return Matrix4x4::Perspective(fov, aspect, zNear, zFar); }
        HX_FUNCTION(Leaf)
        static Matrix4x4 MatrixOrtho(f32 left, f32 right, f32 bottom, f32 top, f32 zNear, f32 zFar)
        {
            return Matrix4x4::Ortho(left, right, bottom, top, zNear, zFar);
        }
        HX_FUNCTION(Leaf)
        static Matrix4x4 MatrixFrustum(f32 left, f32 right, f32 bottom, f32 top, f32 zNear, f32 zFar)
        {
            return Matrix4x4::Frustum(left, right, bottom, top, zNear, zFar);
        }
        HX_FUNCTION(Leaf) static Matrix4x4 MatrixLookAt(Vector3 from, Vector3 to, Vector3 up) { return Matrix4x4::LookAt(from, to, up); }
        HX_FUNCTION(Leaf) static Quaternion MatrixRotation(Matrix4x4 m) { return m.rotation(); }
        HX_FUNCTION(Leaf) static Vector3 MatrixLossyScale(Matrix4x4 m) { return m.lossyScale(); }
        HX_FUNCTION(Leaf) static FrustumPlanes MatrixDecomposeProjection(Matrix4x4 m) { return m.decomposeProjection(); }
        HX_FUNCTION(Leaf) static bool MatrixValidTRS(Matrix4x4 m) { return m.ValidTRS(); }
        HX_FUNCTION(Leaf) static bool MatrixInverse3DAffine(Matrix4x4 input, Matrix4x4& result) { return Matrix4x4::Inverse3DAffine(input, result); }
        HX_FUNCTION(Leaf) static Plane MatrixTransformPlane(Matrix4x4 m, Plane plane) { return m.TransformPlane(plane); }

        // Color and Mathf
        HX_FUNCTION(Leaf) static void ColorRGBToHSV(Color rgbColor, f32& H, f32& S, f32& V) { Color::RGBToHSV(rgbColor, H, S, V); }
        HX_FUNCTION(Leaf) static Color ColorHSVToRGB(f32 H, f32 S, f32 V, bool hdr) { return Color::HSVToRGB(H, S, V, hdr); }
        HX_FUNCTION(Leaf) static f32 GammaToLinearSpace(f32 value) { return mathf::GammaToLinearSpace(value); }
        HX_FUNCTION(Leaf) static f32 LinearToGammaSpace(f32 value) { return mathf::LinearToGammaSpace(value); }
        HX_FUNCTION(Leaf) static Color CorrelatedColorTemperatureToRGB(f32 kelvin) { return mathf::CorrelatedColorTemperatureToRGB(kelvin); }
        HX_FUNCTION(Leaf) static f32 PerlinNoise(f32 x, f32 y) { return mathf::PerlinNoise(x, y); }
        HX_FUNCTION(Leaf) static f32 PerlinNoise1D(f32 x) { return mathf::PerlinNoise1D(x); }
        HX_FUNCTION(Leaf) static u16 FloatToHalf(f32 value) { return mathf::FloatToHalf(value); }
        HX_FUNCTION(Leaf) static f32 HalfToFloat(u16 value) { return mathf::HalfToFloat(value); }
        HX_FUNCTION(Leaf)
        static f32 SmoothDamp(f32 current, f32 target, f32& currentVelocity, f32 smoothTime, f32 maxSpeed, f32 deltaTime)
        {
            return mathf::SmoothDamp(current, target, currentVelocity, smoothTime, maxSpeed, deltaTime);
        }
    };
}
