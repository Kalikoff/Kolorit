#include "core/ColorSpace.h"

#include <QColorSpace>
#include <QObject>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace kolorit::colorspace {

namespace {

constexpr float kTwoPi = 2.f * std::numbers::pi_v<float>;

/// sRGB linearization table (the gamma curve), built once and thread-safe
const std::array<float, 256>& linearLut()
{
    static const std::array<float, 256> lut = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) {
            const float c = i / 255.f;
            t[size_t(i)] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return t;
    }();
    return lut;
}

/// Helper function of L*a*b*
inline float labF(float t)
{
    constexpr float eps = 216.f / 24389.f;   // (6/29)^3
    constexpr float kappa = 24389.f / 27.f;  // (29/3)^3
    return t > eps ? std::cbrt(t) : (kappa * t + 16.f) / 116.f;
}

/// Inverse of labF
inline float labFInv(float t)
{
    constexpr float eps = 6.f / 29.f;
    return t > eps ? t * t * t : 3.f * eps * eps * (t - 4.f / 29.f);
}

/// Linear channel value → gamma-encoded sRGB, 0..255
inline int encodeSrgb(float c)
{
    c = std::clamp(c, 0.f, 1.f);
    const float v = c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f;
    return int(std::lround(v * 255.f));
}

} // namespace

void rgbToHsl(int r, int g, int b, float& h, float& s, float& l)
{
    const float rf = r / 255.f, gf = g / 255.f, bf = b / 255.f;
    const float mx = std::max({rf, gf, bf});
    const float mn = std::min({rf, gf, bf});
    const float d = mx - mn;

    l = (mx + mn) * 0.5f;
    if (d < 1e-6f) {
        h = 0.f;
        s = 0.f;
        return;
    }

    s = d / (1.f - std::fabs(2.f * l - 1.f));

    float hue;
    if (mx == rf)      hue = std::fmod((gf - bf) / d, 6.f);
    else if (mx == gf) hue = (bf - rf) / d + 2.f;
    else               hue = (rf - gf) / d + 4.f;
    if (hue < 0.f) hue += 6.f;
    h = hue / 6.f * kTwoPi;
}

void rgbToHsv(int r, int g, int b, float& h, float& s, float& v)
{
    const float rf = r / 255.f, gf = g / 255.f, bf = b / 255.f;
    const float mx = std::max({rf, gf, bf});
    const float mn = std::min({rf, gf, bf});
    const float d = mx - mn;

    v = mx;
    s = mx < 1e-6f ? 0.f : d / mx;
    if (d < 1e-6f) {
        h = 0.f;
        return;
    }

    float hue;
    if (mx == rf)      hue = std::fmod((gf - bf) / d, 6.f);
    else if (mx == gf) hue = (bf - rf) / d + 2.f;
    else               hue = (rf - gf) / d + 4.f;
    if (hue < 0.f) hue += 6.f;
    h = hue / 6.f * kTwoPi;
}

void rgbToXyz(int r, int g, int b, float& x, float& y, float& z)
{
    const auto& lut = linearLut();
    const float rl = lut[size_t(r)], gl = lut[size_t(g)], bl = lut[size_t(b)];
    x = 0.4124564f * rl + 0.3575761f * gl + 0.1804375f * bl;
    y = 0.2126729f * rl + 0.7151522f * gl + 0.0721750f * bl;
    z = 0.0193339f * rl + 0.1191920f * gl + 0.9503041f * bl;
}

void rgbToLab(int r, int g, int b, float& L, float& a, float& bb)
{
    float x, y, z;
    rgbToXyz(r, g, b, x, y, z);
    // D65 white point
    const float fx = labF(x / 0.95047f);
    const float fy = labF(y / 1.00000f);
    const float fz = labF(z / 1.08883f);
    L  = 116.f * fy - 16.f;
    a  = 500.f * (fx - fy);
    bb = 200.f * (fy - fz);
}

QRgb xyzToRgb(float x, float y, float z)
{
    // Inverse sRGB matrix (D65)
    const float rl =  3.2404542f * x - 1.5371385f * y - 0.4985314f * z;
    const float gl = -0.9692660f * x + 1.8760108f * y + 0.0415560f * z;
    const float bl =  0.0556434f * x - 0.2040259f * y + 1.0572252f * z;
    return qRgb(encodeSrgb(rl), encodeSrgb(gl), encodeSrgb(bl));
}

QRgb labToRgb(float L, float a, float bb)
{
    const float fy = (L + 16.f) / 116.f;
    const float fx = fy + a / 500.f;
    const float fz = fy - bb / 200.f;
    // D65 white point
    return xyzToRgb(labFInv(fx) * 0.95047f, labFInv(fy) * 1.00000f, labFInv(fz) * 1.08883f);
}

Vec3 toModel(QRgb rgb, ColorModel model)
{
    const int r = qRed(rgb), g = qGreen(rgb), b = qBlue(rgb);
    Vec3 v;
    switch (model) {
    case ColorModel::RGB:
        v = {float(r), float(g), float(b)};
        break;
    case ColorModel::HSL: {
        // Bicone: chroma shrinks towards black and white, so grays compare correctly
        // regardless of their "accidental" hue
        float h, s, l;
        rgbToHsl(r, g, b, h, s, l);
        const float chroma = s * (1.f - std::fabs(2.f * l - 1.f));
        v = {chroma * std::cos(h), chroma * std::sin(h), l};
        break;
    }
    case ColorModel::HSV: {
        float h, s, val;
        rgbToHsv(r, g, b, h, s, val);
        const float radius = s * val;
        v = {radius * std::cos(h), radius * std::sin(h), val};
        break;
    }
    case ColorModel::XYZ:
        rgbToXyz(r, g, b, v.x, v.y, v.z);
        break;
    case ColorModel::Lab:
        rgbToLab(r, g, b, v.x, v.y, v.z);
        break;
    }
    return v;
}

QImage toSrgb(QImage image, QString* profileName)
{
    const QColorSpace space = image.colorSpace();
    if (image.isNull() || !space.isValid())
        return image;
    const QColorSpace srgb(QColorSpace::SRgb);
    if (space == srgb)
        return image;

    // The conversion is defined for formats with real channels; indexed and
    // monochrome images are expanded first
    if (image.format() != QImage::Format_ARGB32 && image.format() != QImage::Format_RGB32 &&
        image.format() != QImage::Format_ARGB32_Premultiplied && image.format() != QImage::Format_RGBX8888 &&
        image.format() != QImage::Format_RGBA8888)
        image = image.convertToFormat(QImage::Format_ARGB32);

    if (profileName) {
        const QString description = space.description();
        *profileName = description.isEmpty() ? QObject::tr("embedded profile") : description;
    }
    image.convertToColorSpace(srgb);
    return image;
}

QString modelName(ColorModel model)
{
    switch (model) {
    case ColorModel::RGB: return QStringLiteral("RGB");
    case ColorModel::HSL: return QStringLiteral("HSL");
    case ColorModel::HSV: return QStringLiteral("HSV");
    case ColorModel::XYZ: return QStringLiteral("XYZ");
    case ColorModel::Lab: return QStringLiteral("CIE Lab");
    }
    return {};
}

QList<ColorModel> allModels()
{
    return {ColorModel::RGB, ColorModel::HSL, ColorModel::HSV, ColorModel::XYZ, ColorModel::Lab};
}

} // namespace kolorit::colorspace
