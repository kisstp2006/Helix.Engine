#pragma once

#include <Helix/Core/Basics/Span.h>
#include <Helix/Math/Color.h>
#include <Helix/Math/Vector.h>

// Lighting of every direction as nine spherical harmonics coefficients per color channel (bands 0 to 2), as the
// reference's SphericalHarmonicsL2: coefficient i weighs the real basis function Y_i (0: Y00; 1, 2, 3: y, z, x; 4 to
// 8: xy, yz, 3z^2 - 1, xz, x^2 - y^2, each with its normalization), so Evaluate(n) = sum of c_i * Y_i(n). Lights
// added are already convolved with the cosine lobe: Evaluate gives the diffuse light of a surface facing n.
namespace helix::engine
{
    struct SphericalHarmonicsL2
    {
        // [channel: 0 red, 1 green, 2 blue][coefficient].
        f32 coefficients[3][9] = {};

        f32& operator()(i32 rgb, i32 coefficient) { return coefficients[rgb][coefficient]; }
        f32 operator()(i32 rgb, i32 coefficient) const { return coefficients[rgb][coefficient]; }

        void Clear() { *this = SphericalHarmonicsL2(); }
        // The same light from every direction.
        void AddAmbientLight(Color color);
        // A directional light coming from `direction` (toward the light, as the reference's -light.transform.forward).
        void AddDirectionalLight(Vector3 direction, Color color, f32 intensity);
        // The light of each direction.
        void Evaluate(Span<const Vector3> directions, Span<Color> results) const;
        Color Evaluate(Vector3 direction) const;

        // The shaders' form (the reference's unity_SHAr, SHAg, SHAb, SHBr, SHBg, SHBb, SHC): dot(SHA, (n, 1)) +
        // dot(SHB, n.xyzz * n.yzzx) + SHC * (x^2 - y^2) is Evaluate(n).
        void ShaderConstants(Vector4 (&out)[7]) const;

        SphericalHarmonicsL2 operator+(const SphericalHarmonicsL2& o) const;
        SphericalHarmonicsL2 operator*(f32 scale) const;
        bool operator==(const SphericalHarmonicsL2& o) const;
        bool operator!=(const SphericalHarmonicsL2& o) const { return !(*this == o); }
    };

    // The real spherical harmonics of a unit direction (with their normalization constants).
    void SphericalHarmonicsBasis(Vector3 n, f32 (&basis)[9]);
}
