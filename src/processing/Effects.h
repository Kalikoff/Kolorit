#pragma once

#include "core/ColorSpace.h"
#include "core/ImageTask.h"
#include "core/Palette.h"

#include <QColor>
#include <QString>
#include <QVector>

namespace kolorit::effects {

/// Invert colors
ImageOp invert();

/// Grayscale (Rec. 601 luma)
ImageOp grayscale();

/// Sepia
ImageOp sepia();

/// Posterize: every channel is rounded to levels steps (2..256)
ImageOp posterize(int levels);

/// Pixelate in blockSize×blockSize blocks (the block color is averaged)
ImageOp pixelate(int blockSize);

/// Correction: brightness, contrast and saturation in the range -100..100
ImageOp adjust(double brightness, double contrast, double saturation);

/// Remove artifacts and noise.
/// First a median filter of radius medianRadius (0 disables it) removes speckle noise,
/// then passes rounds of a bilateral filter smooth grain and JPEG compression artifacts
/// (blocking, "mosquito" noise around edges) without blurring the edges themselves:
/// radius is the spatial radius in px, tolerance (1..100) is how different colors may
/// be and still be averaged; the lower it is, the more strictly edges are preserved.
ImageOp denoise(int medianRadius, int radius, double tolerance, int passes);

/// Gradient map parameters
struct GradientMapParams {
    Palette palette;        ///< colors from shadows to highlights (empty: black → white)
    bool smooth = true;     ///< smooth transitions between colors; otherwise steps
    bool reverse = false;   ///< palette in reverse order
    double balance = 0.0;   ///< midtone shift -1..1 (a gamma on luminance)
    double opacity = 1.0;   ///< strength: 0 is the source, 1 is the full map
};

/// Gradient map: pixel luminance → position along the palette.
/// Unlike repainting (nearest color), the palette is used as a gradient: toning,
/// duotone, heat map.
ImageOp gradientMap(const GradientMapParams& params);

/// Duotone / tritone parameters
struct DuotoneParams {
    QColor shadows = QColor(0x1b, 0x1f, 0x3b);     ///< shadow color
    QColor highlights = QColor(0xf5, 0xe6, 0xc8);  ///< highlight color
    bool useMidtones = false;                      ///< tritone: a third color for the midtones
    QColor midtones = QColor(0xc0, 0x39, 0x2b);
    double balance = 0.0;                          ///< midtone shift -1..1
    double opacity = 1.0;                          ///< blend strength with the source
};

/// Duotone / tritone: two or three colors spread smoothly over luminance (a special case
/// of the gradient map)
ImageOp duotone(const DuotoneParams& params);

/// Pixel-art effect parameters
struct PixelArtParams {
    int blockSize = 8;              ///< size of a "pixel", px
    int levels = 8;                 ///< levels per channel when quantizing (< 2: no quantization)
    double outlineThreshold = 0.2;  ///< how different neighboring blocks must be to get an outline, 0..1
    double outlineDarkness = 0.7;   ///< outline darkening, 0..1 (1 is black)
    double outlineWidth = 1.0;      ///< outline width as a fraction of the block, 0..1
    Palette palette;                ///< if not empty, quantize to the palette instead of levels
    ColorModel model = ColorModel::RGB;
};

/// Pixel art: enlarges pixels with averaging, quantizes colors (levels or a palette) and
/// outlines shapes; the line is drawn on the dark side of the edge between blocks of
/// different colors.
ImageOp pixelArt(const PixelArtParams& params);

/// Halftone screen parameters
struct HalftoneParams {
    double cellSize = 4.0;     ///< screen pitch, px
    double angle = 15.0;       ///< rotation of the black ink screen, degrees
    bool colorSeparation = true;   ///< four CMYK inks with their own angles; otherwise a single black
    double sharpness = 0.45;   ///< dot edge sharpness, 0..1 (0 soft, 1 almost crisp)
    bool onWhite = true;       ///< print on white; otherwise light dots on dark
};

/// Newspaper halftone: the image is broken into dots of varying size.
///
/// With color separation every CMYK ink gets its own screen angle (15°, 75°, 0° and
/// 45°) as in offset printing: with this rotation the dots form rosettes and produce no
/// moiré. The black-and-white variant prints a single ink at the common angle.
ImageOp halftone(const HalftoneParams& params);

/// Color splash parameters
struct ColorSplashParams {
    double hue = 0.0;          ///< center of the hue range, degrees 0..360
    double tolerance = 30.0;   ///< half-width of the range, degrees
    double softness = 15.0;    ///< width of the soft edge of the range, degrees
    double minSaturation = 0.15;   ///< below this saturation a color counts as gray, 0..1
    bool invert = false;       ///< desaturate the chosen range instead of everything else
};

/// Color splash: the chosen hue range stays in color, everything else turns gray.
/// The range is defined on the hue circle, so red across the 0°/360° seam is not split.
ImageOp colorSplash(const ColorSplashParams& params);

/// Levels parameters. Black and white points are in 0..255 units.
struct LevelsParams {
    double blackPoint = 0.0;     ///< everything darker becomes black
    double whitePoint = 255.0;   ///< everything lighter becomes white
    double gamma = 1.0;          ///< 0.1..10; above 1 lightens the midtones
    double outputBlack = 0.0;    ///< where the black point maps to
    double outputWhite = 255.0;  ///< where the white point maps to
    /// Per-channel black point offsets, used to correct a color cast in the shadows
    double redBlack = 0.0;
    double greenBlack = 0.0;
    double blueBlack = 0.0;
};

/// Levels: black and white points, gamma and output range, plus separate per-channel
/// black points to fix a color cast in the shadows. Unlike adjust(), which only has
/// brightness, contrast and saturation, this controls the shape of the whole curve.
ImageOp levels(const LevelsParams& params);

/// A three-dimensional color lookup table (3D LUT) in the .cube format.
///
/// The format is shared by DaVinci Resolve, Premiere and Photoshop, so ready-made film
/// grades reach Kolorit through it. Values are interpolated trilinearly; 1D tables
/// (LUT_1D_SIZE) are read too and applied to each channel separately.
class CubeLut {
public:
    /// Read a .cube file. On error returns an invalid table and fills errorMessage.
    static CubeLut load(const QString& path, QString* errorMessage = nullptr);

    /// Build a table from an operation: op is run over a size³ color cube.
    /// This turns any Kolorit effect into a file for a video editor.
    static CubeLut fromOp(const ImageOp& op, int size);

    bool isValid() const { return m_size >= 2; }
    int size() const { return m_size; }
    const QString& title() const { return m_title; }

    bool save(const QString& path, QString* errorMessage = nullptr) const;

    /// Operation applying the table to an image; strength is the blend with the source
    ImageOp makeOp(double strength = 1.0) const;

private:
    int m_size = 0;
    QString m_title;
    /// Nodes in .cube order: red changes fastest
    QVector<QRgb> m_nodes;
};

/// Style of the disc center
enum class DiscHub {
    ClearPlastic,    ///< clear hub of a real CD: rings, a chamfered hole
    DarkMechanism,   ///< dark "mechanism": grooves, spokes, an accent ring matching the image
    PrintToCenter,   ///< the print reaches the hole, only a thin clear rim remains
};

/// "Disc" effect parameters. Fractions are relative to the disc diameter.
struct DiscParams {
    double holeSize = 0.12;             ///< diameter of the center hole
    double hubSize = 0.28;              ///< diameter of the hub around the hole
    DiscHub hub = DiscHub::ClearPlastic;
    double zoom = 1.0;                  ///< image scale: at 1 the short side equals the diameter
    double offsetX = 0.0;               ///< image shift as a fraction of its width (-0.5..0.5)
    double offsetY = 0.0;               ///< image shift as a fraction of its height (-0.5..0.5)
    double sheen = 0.4;                 ///< rainbow diffraction, 0..1
    double sheenAngle = 30.0;           ///< angle of the sheen axis, degrees
    double gloss = 0.3;                 ///< glossy highlight from the light source, 0..1
    double tracks = 0.3;                ///< concentric tracks, 0..1
    double margin = 0.05;               ///< margin around the disc as a fraction of the side (room for the shadow)
    double shadow = 0.6;                ///< soft shadow under the disc, 0..1 (needs a margin)
    bool transparentBackground = true;  ///< transparent outside the disc, otherwise a dark background
};

/// Disc: the image is cropped to a circle and laid onto a CD/DVD with a hole and hub, a
/// clear rim and a chamfered edge, volumetric shading, diffraction rainbow, glossy
/// highlight, tracks and a soft shadow. The result is square, its side equal to the
/// short side of the image.
ImageOp disc(const DiscParams& params);

} // namespace kolorit::effects
