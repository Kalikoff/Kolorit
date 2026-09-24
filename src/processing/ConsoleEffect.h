#pragma once

#include "core/ImageTask.h"
#include "core/Palette.h"
#include "processing/PaletteMapper.h"

#include <QList>
#include <QSize>
#include <QString>

namespace kolorit {

/// Retro console whose limitations the image is fitted to
enum class Console {
    GameBoy,         ///< Game Boy (DMG): 160×144, 4 shades of green
    GameBoyPocket,   ///< Game Boy Pocket: 160×144, 4 shades of gray
    Nes,             ///< NES / Famicom: 256×240, PPU 2C02 palette, at most 4 colors per 16×16 block
    ZxSpectrum,      ///< ZX Spectrum: 256×192, 15 colors, 2 colors of the same brightness per 8×8 cell
};

QString consoleName(Console console);
QSize consoleResolution(Console console);
/// Hardware palette of the console (dark to light for the Game Boy)
Palette consolePalette(Console console);
QList<Console> allConsoles();

/// "Retro console" effect parameters
struct ConsoleParams {
    Console console = Console::GameBoy;
    Dithering dithering = Dithering::Bayer4;   ///< ordered dithering before quantization (error diffusion is not used)
    int ditherStrength = 48;                   ///< dithering amplitude in RGB units, 0..128
    bool attributeLimits = true;               ///< respect the limits on colors per block (NES, Spectrum)
    bool integerScale = false;                 ///< integer upscaling with borders; otherwise stretch to the source size
    double contrast = 0.15;                    ///< extra contrast before quantization, -1..1
};

/// Retro console: the image is downscaled to the console resolution, quantized to its
/// palette with dithering, the per-block color limits are respected if needed, and
/// then it is scaled back up with crisp pixels.
ImageOp consoleOp(const ConsoleParams& params);

} // namespace kolorit
