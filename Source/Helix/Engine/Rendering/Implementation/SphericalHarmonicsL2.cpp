#include <Helix/Engine/Rendering/SphericalHarmonicsL2.h>

namespace helix::engine
{
    namespace
    {
        // The normalization constants of the basis functions.
        constexpr f32 kY0 = 0.282094792f;
        constexpr f32 kY1 = 0.488602512f;
        constexpr f32 kY2 = 1.092548431f;
        constexpr f32 kY20 = 0.315391565f;
        constexpr f32 kY22 = 0.546274215f;
        constexpr f32 kPi = 3.14159265f;
        // The cosine lobe's convolution of each band, over pi (a surface's diffuse light): 1, 2/3, 1/4.
        constexpr f32 kBand[9] = {1.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f};
    }

    void SphericalHarmonicsBasis(Vector3 n, f32 (&b)[9])
    {
        b[0] = kY0;
        b[1] = kY1 * n.y;
        b[2] = kY1 * n.z;
        b[3] = kY1 * n.x;
        b[4] = kY2 * n.x * n.y;
        b[5] = kY2 * n.y * n.z;
        b[6] = kY20 * (3.0f * n.z * n.z - 1.0f);
        b[7] = kY2 * n.x * n.z;
        b[8] = kY22 * (n.x * n.x - n.y * n.y);
    }

    void SphericalHarmonicsL2::AddAmbientLight(Color color)
    {
        // A constant: only the first coefficient, Evaluate gives the color.
        coefficients[0][0] += color.r / kY0;
        coefficients[1][0] += color.g / kY0;
        coefficients[2][0] += color.b / kY0;
    }

    void SphericalHarmonicsL2::AddDirectionalLight(Vector3 direction, Color color, f32 intensity)
    {
        // The light's direction projected (a delta of radiance), convolved with the cosine lobe over pi times pi: a
        // surface facing the light gets about color * intensity.
        f32 b[9];
        SphericalHarmonicsBasis(direction.normalized(), b);
        const f32 rgb[3] = {color.r * intensity, color.g * intensity, color.b * intensity};
        for (i32 c = 0; c < 3; c++)
            for (i32 i = 0; i < 9; i++)
                coefficients[c][i] += rgb[c] * b[i] * kBand[i] * kPi;
    }

    Color SphericalHarmonicsL2::Evaluate(Vector3 direction) const
    {
        f32 b[9];
        SphericalHarmonicsBasis(direction, b);
        f32 rgb[3] = {};
        for (i32 c = 0; c < 3; c++)
            for (i32 i = 0; i < 9; i++)
                rgb[c] += coefficients[c][i] * b[i];
        return Color(rgb[0], rgb[1], rgb[2], 1.0f);
    }

    void SphericalHarmonicsL2::Evaluate(Span<const Vector3> directions, Span<Color> results) const
    {
        for (usize i = 0; i < directions.Size() && i < results.Size(); i++)
            results[i] = Evaluate(directions[i]);
    }

    void SphericalHarmonicsL2::ShaderConstants(Vector4 (&out)[7]) const
    {
        for (i32 c = 0; c < 3; c++)
        {
            const f32* k = coefficients[c];
            // The linear part and the constant (Y20's constant moved into it).
            out[c] = Vector4(kY1 * k[3], kY1 * k[1], kY1 * k[2], kY0 * k[0] - kY20 * k[6]);
            // n.xyzz * n.yzzx: xy, yz, zz, zx.
            out[3 + c] = Vector4(kY2 * k[4], kY2 * k[5], 3.0f * kY20 * k[6], kY2 * k[7]);
        }
        out[6] = Vector4(kY22 * coefficients[0][8], kY22 * coefficients[1][8], kY22 * coefficients[2][8], 1.0f);
    }

    SphericalHarmonicsL2 SphericalHarmonicsL2::operator+(const SphericalHarmonicsL2& o) const
    {
        SphericalHarmonicsL2 r;
        for (i32 c = 0; c < 3; c++)
            for (i32 i = 0; i < 9; i++)
                r.coefficients[c][i] = coefficients[c][i] + o.coefficients[c][i];
        return r;
    }

    SphericalHarmonicsL2 SphericalHarmonicsL2::operator*(f32 scale) const
    {
        SphericalHarmonicsL2 r;
        for (i32 c = 0; c < 3; c++)
            for (i32 i = 0; i < 9; i++)
                r.coefficients[c][i] = coefficients[c][i] * scale;
        return r;
    }

    bool SphericalHarmonicsL2::operator==(const SphericalHarmonicsL2& o) const
    {
        for (i32 c = 0; c < 3; c++)
            for (i32 i = 0; i < 9; i++)
                if (coefficients[c][i] != o.coefficients[c][i])
                    return false;
        return true;
    }
}
