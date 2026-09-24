#pragma once

#include "core/Palette.h"

#include <QList>
#include <QString>

namespace kolorit::builtin {

/// Menu section of the builtin palettes
enum class PaletteGroup {
    Ramp,       ///< Ramps: gradient map, duotone, heat maps
    Retro,      ///< Palettes of retro platforms
    PixelArt,   ///< Pixel-art palettes
    Ui,         ///< UI themes and design systems
    Apps,       ///< Applications, print and the rest
};

/// Section name for the submenu
QString groupName(PaletteGroup group);

/// Section description for the status bar
QString groupHint(PaletteGroup group);

/// All sections in display order
QList<PaletteGroup> allGroups();

/// A builtin palette: menu name, colors and section
struct BuiltinPalette {
    QString name;
    Palette palette;
    PaletteGroup group = PaletteGroup::Apps;
};

/// All builtin palettes in menu order
QList<BuiltinPalette> palettes();

/// Builtin palettes of one section
QList<BuiltinPalette> palettes(PaletteGroup group);

/// Builtin palette by name (empty if there is none)
Palette byName(const QString& name);

/// Grayscale: count levels from black to white (2..256).
///
/// perceptual = false: an even step in sRGB bytes;
/// perceptual = true: an even step in lightness L*, so the steps look equal to the eye.
/// The largest L* step is then 12–17 % smaller, and that step is exactly what shows up
/// as a band on a smooth gradient.
Palette grayscale(int count, bool perceptual = false);

/// Tint of one color: count steps from almost black to almost white in the hue of base.
///
/// The ramp is built in CIE Lab: lightness changes while chroma falls off towards both
/// ends, because very dark and very light colors simply cannot have much of it. Unlike
/// hardware tinted palettes (Game Boy) such a ramp covers the whole tonal range, so
/// repainting into it does not collapse into two colors.
Palette tint(QRgb base, int count);

/// Ramp between two colors: count steps, interpolated in CIE Lab
Palette ramp(QRgb from, QRgb to, int count);

/// Ramp through a middle color: count steps, from → mid → to
Palette ramp(QRgb from, QRgb mid, QRgb to, int count);

/// Even per-channel quantization: levels per channel, levels³ colors (2..16).
/// 2 gives 8 colors, 3 gives 27 (Amstrad CPC), 4 gives 64 (EGA), 6 gives 216 (web-safe).
Palette levels(int levelsPerChannel);

/// Hue wheel: count colors of constant lightness and chroma (CIE Lab coordinates).
/// lightness is 0..100, chroma 0..128; colors outside the sRGB gamut are clamped.
Palette hueWheel(int count, double lightness, double chroma);

/// Web-safe palette: all combinations of 00/33/66/99/CC/FF, 216 colors
Palette webSafe();

/// Retro console palettes
Palette gameBoy();         ///< Game Boy (DMG): 4 shades of green, dark to light
Palette gameBoyPocket();   ///< Game Boy Pocket: 4 shades of gray
Palette nes();             ///< NES, the PPU 2C02 palette without duplicates
Palette zxSpectrum();      ///< ZX Spectrum: black, 7 normal and 7 bright colors

} // namespace kolorit::builtin
