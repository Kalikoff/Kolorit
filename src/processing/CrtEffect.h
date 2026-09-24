#pragma once

#include "core/ImageTask.h"

#include <QList>
#include <QString>

namespace kolorit {

/// CRT phosphor mask
enum class CrtMask {
    None,             ///< no mask
    ApertureGrille,   ///< aperture grille: vertical RGB stripes (Trinitron)
    ShadowMask,       ///< shadow mask: triads offset every other row
    SlotMask,         ///< slot mask: RGB stripes with horizontal gaps in a checkerboard pattern
};

QString crtMaskName(CrtMask mask);
QList<CrtMask> allCrtMasks();

/// "CRT television" effect parameters. Pixel values are given for the full resolution.
struct CrtParams {
    double curvature = 0.25;         ///< screen bulge, 0..1
    CrtMask mask = CrtMask::ApertureGrille;
    int maskPeriod = 3;              ///< width of an RGB triad, px
    double maskStrength = 0.35;      ///< darkening of the "foreign" subpixels, 0..1
    int scanlinePeriod = 3;          ///< scanline pitch, px
    double scanlineIntensity = 0.35; ///< scanline depth, 0..1
    int bloomRadius = 6;             ///< glow radius of bright areas, px (0 disables it)
    double bloomStrength = 0.35;     ///< glow strength, 0..1
    int aberration = 1;              ///< horizontal color misconvergence, px
    double vignette = 0.35;          ///< corner darkening, 0..1
    double glass = 0.2;              ///< reflection on the glass, 0..1
    double gain = 1.2;               ///< brightness gain (the mask and scanlines darken), 0.5..2
};

/// CRT television: a curved screen with rounded corners and a dark bezel, chromatic
/// aberration, phosphor glow, an RGB mask, scanlines, a vignette and a glass reflection.
ImageOp crtOp(const CrtParams& params);

} // namespace kolorit
