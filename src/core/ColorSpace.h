#pragma once

#include <QImage>
#include <QRgb>
#include <QString>
#include <QList>

namespace kolorit {

/// Color model in which the nearest palette color is searched for
enum class ColorModel {
    RGB,   ///< Euclidean distance in RGB
    HSL,   ///< HSL bicone (hue is the angle, saturation the radius, lightness the height)
    HSV,   ///< HSV cone
    XYZ,   ///< CIE XYZ (D65)
    Lab,   ///< CIE L*a*b*: perceptually uniform, ΔE76 distance
};

/// A point in the 3D space of the chosen model.
/// Every model is mapped to Euclidean coordinates so colors are compared with one formula.
struct Vec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

namespace colorspace {

/// Convert an sRGB color to the coordinates of the chosen model
Vec3 toModel(QRgb rgb, ColorModel model);

/// Squared Euclidean distance between two points
inline float distanceSq(const Vec3& a, const Vec3& b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

/// sRGB → HSL. h in radians [0, 2π), s and l in [0, 1]
void rgbToHsl(int r, int g, int b, float& h, float& s, float& l);

/// sRGB → HSV. h in radians [0, 2π), s and v in [0, 1]
void rgbToHsv(int r, int g, int b, float& h, float& s, float& v);

/// sRGB → CIE XYZ (D65), white has Y = 1
void rgbToXyz(int r, int g, int b, float& x, float& y, float& z);

/// sRGB → CIE L*a*b* (D65)
void rgbToLab(int r, int g, int b, float& L, float& a, float& bb);

/// CIE XYZ (D65) → sRGB. Out-of-gamut colors are clamped to the boundary.
QRgb xyzToRgb(float x, float y, float z);

/// CIE L*a*b* (D65) → sRGB. Out-of-gamut colors are clamped to the boundary.
QRgb labToRgb(float L, float a, float bb);

/// sRGB → YCbCr (full JPEG range, 0..255)
inline void rgbToYCbCr(float r, float g, float b, float& y, float& cb, float& cr)
{
    y  =         0.299f    * r + 0.587f    * g + 0.114f    * b;
    cb = 128.f - 0.168736f * r - 0.331264f * g + 0.5f      * b;
    cr = 128.f + 0.5f      * r - 0.418688f * g - 0.081312f * b;
}

/// YCbCr → sRGB (not clamped: the caller clamps)
inline void yCbCrToRgb(float y, float cb, float cr, float& r, float& g, float& b)
{
    const float dcb = cb - 128.f;
    const float dcr = cr - 128.f;
    r = y + 1.402f    * dcr;
    g = y - 0.344136f * dcb - 0.714136f * dcr;
    b = y + 1.772f    * dcb;
}

/// Relative luma (Rec. 601), 0..255
inline float luma(QRgb c)
{
    return 0.299f * qRed(c) + 0.587f * qGreen(c) + 0.114f * qBlue(c);
}

/// Clamp a value to 0..255 and round it to a byte
inline int clampByte(float v)
{
    return v <= 0.f ? 0 : (v >= 255.f ? 255 : int(v + 0.5f));
}

inline int clampByte(int v)
{
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

/// Convert the image to sRGB if the file carried a color profile of its own.
///
/// All processing treats colors as sRGB values: the linearization table, the
/// conversions to XYZ and Lab, the luma weights. Phone photos today are mostly
/// Display P3 and DSLRs produce AdobeRGB; in such files the same bytes mean different
/// colors, and without a conversion the picture drifts before repainting even starts.
/// Qt reads the profile itself; only the conversion is left to do here.
///
/// If profileName is given, it receives a description of the source profile, but only
/// when a conversion was actually needed.
QImage toSrgb(QImage image, QString* profileName = nullptr);

/// Display name of a model
QString modelName(ColorModel model);

/// All available models in the order they are shown in the interface
QList<ColorModel> allModels();

} // namespace colorspace
} // namespace kolorit
