#pragma once

#include "core/ImageTask.h"

#include <QColor>
#include <QDateTime>
#include <QImage>
#include <QList>
#include <QSize>
#include <QString>
#include <QVariantMap>

#include <cstdint>

namespace kolorit {

/// Channel (or pair of channels) kept in the shifted layer of the chromatic aberration
enum class Channel { Red, Green, Blue, Yellow, Magenta, Cyan };

/// Bit mask of RGB channels (R=1, G=2, B=4)
unsigned channelMask(Channel c);
QString channelName(Channel c);
QList<Channel> allChannels();

/// Status label in the upper corner, as on a VCR
enum class OsdLabel { None, Play, Rec, Pause };
QString osdLabelName(OsdLabel l);

/// Corner where the date and time are shown
enum class OsdCorner { TopLeft, TopRight, BottomLeft, BottomRight };
QString osdCornerName(OsdCorner c);

/// The full set of VHS effect parameters
struct VhsParams {
    // --- Chromatic aberration of the image (left and right layers) ---
    Channel imageLeft = Channel::Blue;
    Channel imageRight = Channel::Red;
    int imageOffset = 8;                ///< layer shift, px

    // --- Color bleed (horizontal blur of the color-difference channels) ---
    int chromaBleed = 6;                ///< radius, px (0 disables it)

    // --- Color correction ---
    double brightness = 0.0;            ///< -100..100
    double contrast = 0.0;              ///< -100..100
    double saturation = -10.0;          ///< -100..100

    // --- Line jitter and the tracking band ---
    int lineJitter = 2;                 ///< random shift of every line, px
    bool tracking = true;
    double trackingPosition = 0.82;     ///< band position as a fraction of the height, 0..1
    int trackingHeight = 28;            ///< band height, px
    int trackingShift = 36;             ///< largest shift inside the band, px

    // --- Scanlines ---
    bool scanlines = true;
    int scanlinePeriod = 3;             ///< every scanlinePeriod-th line
    int scanlineThickness = 1;          ///< thickness of the darkened line, px
    double scanlineIntensity = 0.18;    ///< darkening 0..1

    // --- Grain and vignette ---
    double noise = 0.08;                ///< noise amplitude 0..1
    double vignette = 0.25;             ///< corner darkening strength 0..1

    // --- On-screen display (OSD) ---
    bool showTime = true;
    bool showDate = true;
    QString customText;                 ///< an extra line under the date
    OsdLabel label = OsdLabel::Play;
    OsdCorner corner = OsdCorner::BottomLeft;
    double fontSizePercent = 5.0;       ///< font height in % of the image height
    QColor textColor = QColor(216, 216, 216);
    Channel textLeft = Channel::Blue;
    Channel textRight = Channel::Red;
    int textOffset = 2;                 ///< text aberration, px
    bool useCurrentTime = true;
    QDateTime dateTime = QDateTime::currentDateTime();

    // --- Randomness ---
    uint32_t seed = 1;

    /// A copy with pixel parameters multiplied by factor (for the downscaled preview)
    VhsParams scaled(double factor) const;

    QVariantMap toVariantMap() const;
    static VhsParams fromVariantMap(const QVariantMap& map);
};

namespace vhs {

/// Load the VHS font from the resources. Call once on the main thread after QGuiApplication is created.
void registerFonts();

/// Family of the VHS font (after registerFonts); a monospace system font if it is missing
QString fontFamily();

/// Render the overlay layer (text on black) at the size of the image.
/// Runs on the main thread: QPainter text rendering must not happen on the pool.
QImage renderOsd(const VhsParams& params, const QSize& imageSize);

/// The effect operation. osdLayer is the result of renderOsd() (may be null).
ImageOp makeOp(const VhsParams& params, const QImage& osdLayer);

} // namespace vhs
} // namespace kolorit
