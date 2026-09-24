#pragma once

#include "core/ColorSpace.h"
#include "core/ImageTask.h"
#include "core/Palette.h"

#include <QList>
#include <QString>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace kolorit {

/// Dithering method used when repainting
enum class Dithering {
    None,              ///< Every pixel is simply replaced with the nearest color
    Bayer2,            ///< Ordered, 2×2 matrix
    Bayer4,            ///< Ordered, 4×4 matrix
    Bayer8,            ///< Ordered, 8×8 matrix
    BlueNoise,         ///< Ordered, 64×64 blue-noise texture
    FloydSteinberg,    ///< Error diffusion, Floyd–Steinberg kernel
    Atkinson,          ///< Atkinson error diffusion: only 6/8 is spread, so the picture is more contrasty
    Sierra,            ///< Sierra error diffusion: softer than Floyd
    Stucki,            ///< Stucki error diffusion: wide kernel, smooth transitions
    JarvisJudiceNinke, ///< Jarvis–Judice–Ninke error diffusion: the widest kernel
};

QString ditheringName(Dithering d);
QList<Dithering> allDitherings();

/// Ordered dithering: the threshold depends only on the pixel coordinates
bool isOrderedDithering(Dithering d);

/// Error diffusion: a sequential pass, the threshold depends on the neighbors
bool isErrorDiffusion(Dithering d);

/// Ordered dithering threshold for the point (x, y) and the channel
/// (0 red, 1 green, 2 blue): an offset in [-0.5, 0.5).
/// Zero for None and for error diffusion.
///
/// The pattern of each channel is shifted in phase by a third and two thirds of the
/// period relative to its neighbor. If all three channels get the same threshold,
/// dithering only moves the pixel along the gray axis: the result is a luminance
/// pattern that renders intermediate colors more coarsely and draws a visible colored grid.
float orderedDitherOffset(Dithering d, int x, int y, int channel);

/// The same offset for all three channels: luminance-only dithering
inline float orderedDitherOffset(Dithering d, int x, int y)
{
    return orderedDitherOffset(d, x, y, 0);
}

/// Precomputed ordered dithering thresholds: what gets added to a channel of pixel (x, y).
///
/// The threshold depends only on the low bits of the coordinates and on the channel,
/// so it is a small table. Computing it per pixel (a switch, a modulo and a floating
/// point division, three times over) is noticeably more expensive than filling memory
/// once that then stays in cache entirely.
///
/// The 2×2 and 4×4 Bayer matrices are tiled up to a period of 8, blue noise has a period
/// of 64, so the loop is left with a mask and one read. The strength is already applied.
class OrderedDither {
public:
    OrderedDither(Dithering dithering, float strength);

    /// Offset for the channel (0 red, 1 green, 2 blue)
    float at(int x, int y, int channel) const
    {
        return m_table[(size_t((y & m_mask) * m_period + (x & m_mask)) * 3) + size_t(channel)];
    }

private:
    std::vector<float> m_table;
    int m_period = 8;
    int m_mask = 7;
};

/// One entry of a diffusion kernel: where to send which share of the error
struct DiffusionWeight {
    int dx = 0;
    int dy = 0;
    float weight = 0.f;
};

/// Error diffusion kernel of the chosen method (empty for the others)
QList<DiffusionWeight> diffusionKernel(Dithering d);

/// Repaint parameters
struct RepaintParams {
    ColorModel model = ColorModel::RGB;
    Dithering dithering = Dithering::None;
    int ditherStrength = 32;   ///< Amplitude of ordered dithering in RGB units (0..128)
};

/// Nearest palette color search in the chosen color model.
///
/// Palette colors are converted to model coordinates once. The result for every
/// requested color is remembered in a cache, so repeated colors (the vast majority in
/// photos) cost one memory read instead of a pass over the palette. The cache is safe
/// to use from any number of threads at once.
///
/// The cache key is the color reduced to 6 bits per channel (262,144 cells, 512 KB).
/// A cache over all 16.7 million RGB values took 32 MB, and clearing it cost about
/// 4.6 ms per operation regardless of the image size; in the preview that was more than
/// the work itself. Measurements show that 6 bits do not change the result: on every
/// palette checked, up to RAL with 213 colors and Rainbow with 360, the difference in
/// ΔE2000 stays below 0.02 against a visibility threshold of about 1.
///
/// The nearest color is searched for the center of the bucket rather than for the first
/// color that landed in it, so the result does not depend on the order in which threads
/// reached the pixels.
class PaletteMapper {
public:
    PaletteMapper(const Palette& palette, ColorModel model);

    int nearestIndex(QRgb rgb) const;
    QRgb nearest(QRgb rgb) const { return m_colors[size_t(nearestIndex(rgb))]; }
    int size() const { return int(m_colors.size()); }

    /// Operation that repaints the whole image
    static ImageOp makeOp(const Palette& palette, const RepaintParams& params);

private:
    std::vector<QRgb> m_colors;
    std::vector<Vec3> m_modelColors;
    ColorModel m_model;
    // A cached value is index + 1; zero means "not computed yet"
    std::unique_ptr<std::atomic<uint16_t>[]> m_cache;
};

} // namespace kolorit
