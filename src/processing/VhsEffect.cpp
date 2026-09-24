#include "processing/VhsEffect.h"

#include "core/ColorSpace.h"
#include "core/ParallelFor.h"
#include "core/PixelOps.h"
#include "core/Random.h"

#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QLocale>
#include <QObject>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <vector>

namespace kolorit {

using colorspace::clampByte;

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

unsigned channelMask(Channel c)
{
    switch (c) {
    case Channel::Red:     return 1u;
    case Channel::Green:   return 2u;
    case Channel::Blue:    return 4u;
    case Channel::Yellow:  return 1u | 2u;
    case Channel::Magenta: return 1u | 4u;
    case Channel::Cyan:    return 2u | 4u;
    }
    return 0u;
}

QString channelName(Channel c)
{
    switch (c) {
    case Channel::Red:     return QObject::tr("Red");
    case Channel::Green:   return QObject::tr("Green");
    case Channel::Blue:    return QObject::tr("Blue");
    case Channel::Yellow:  return QObject::tr("Yellow");
    case Channel::Magenta: return QObject::tr("Magenta");
    case Channel::Cyan:    return QObject::tr("Cyan");
    }
    return {};
}

QList<Channel> allChannels()
{
    return {Channel::Red, Channel::Green, Channel::Blue, Channel::Yellow, Channel::Magenta, Channel::Cyan};
}

QString osdLabelName(OsdLabel l)
{
    switch (l) {
    case OsdLabel::None:  return QObject::tr("None");
    case OsdLabel::Play:  return QStringLiteral("PLAY ▶");
    case OsdLabel::Rec:   return QStringLiteral("● REC");
    case OsdLabel::Pause: return QStringLiteral("PAUSE ‖");
    }
    return {};
}

QString osdCornerName(OsdCorner c)
{
    switch (c) {
    case OsdCorner::TopLeft:     return QObject::tr("Top left");
    case OsdCorner::TopRight:    return QObject::tr("Top right");
    case OsdCorner::BottomLeft:  return QObject::tr("Bottom left");
    case OsdCorner::BottomRight: return QObject::tr("Bottom right");
    }
    return {};
}

// ---------------------------------------------------------------------------
// VhsParams
// ---------------------------------------------------------------------------

VhsParams VhsParams::scaled(double factor) const
{
    VhsParams p = *this;
    auto px = [factor](int v) { return int(std::lround(v * factor)); };
    p.imageOffset = px(imageOffset);
    p.textOffset = px(textOffset);
    p.chromaBleed = px(chromaBleed);
    p.lineJitter = px(lineJitter);
    p.trackingHeight = std::max(1, px(trackingHeight));
    p.trackingShift = px(trackingShift);
    p.scanlinePeriod = std::max(1, px(scanlinePeriod));
    p.scanlineThickness = std::clamp(px(scanlineThickness), 1, p.scanlinePeriod);
    return p;
}

QVariantMap VhsParams::toVariantMap() const
{
    QVariantMap m;
    m["imageLeft"] = int(imageLeft);
    m["imageRight"] = int(imageRight);
    m["imageOffset"] = imageOffset;
    m["chromaBleed"] = chromaBleed;
    m["brightness"] = brightness;
    m["contrast"] = contrast;
    m["saturation"] = saturation;
    m["lineJitter"] = lineJitter;
    m["tracking"] = tracking;
    m["trackingPosition"] = trackingPosition;
    m["trackingHeight"] = trackingHeight;
    m["trackingShift"] = trackingShift;
    m["scanlines"] = scanlines;
    m["scanlinePeriod"] = scanlinePeriod;
    m["scanlineThickness"] = scanlineThickness;
    m["scanlineIntensity"] = scanlineIntensity;
    m["noise"] = noise;
    m["vignette"] = vignette;
    m["showTime"] = showTime;
    m["showDate"] = showDate;
    m["customText"] = customText;
    m["label"] = int(label);
    m["corner"] = int(corner);
    m["fontSizePercent"] = fontSizePercent;
    m["textColor"] = textColor.name();
    m["textLeft"] = int(textLeft);
    m["textRight"] = int(textRight);
    m["textOffset"] = textOffset;
    m["useCurrentTime"] = useCurrentTime;
    m["dateTime"] = dateTime;
    m["seed"] = uint(seed);
    return m;
}

VhsParams VhsParams::fromVariantMap(const QVariantMap& m)
{
    VhsParams d;   // defaults for missing keys
    auto geti = [&](const char* key, int def) { return m.value(key, def).toInt(); };
    auto getd = [&](const char* key, double def) { return m.value(key, def).toDouble(); };
    auto getb = [&](const char* key, bool def) { return m.value(key, def).toBool(); };
    auto channel = [](int v) { return Channel(std::clamp(v, 0, 5)); };

    d.imageLeft = channel(geti("imageLeft", int(d.imageLeft)));
    d.imageRight = channel(geti("imageRight", int(d.imageRight)));
    d.imageOffset = geti("imageOffset", d.imageOffset);
    d.chromaBleed = geti("chromaBleed", d.chromaBleed);
    d.brightness = getd("brightness", d.brightness);
    d.contrast = getd("contrast", d.contrast);
    d.saturation = getd("saturation", d.saturation);
    d.lineJitter = geti("lineJitter", d.lineJitter);
    d.tracking = getb("tracking", d.tracking);
    d.trackingPosition = getd("trackingPosition", d.trackingPosition);
    d.trackingHeight = geti("trackingHeight", d.trackingHeight);
    d.trackingShift = geti("trackingShift", d.trackingShift);
    d.scanlines = getb("scanlines", d.scanlines);
    d.scanlinePeriod = geti("scanlinePeriod", d.scanlinePeriod);
    d.scanlineThickness = geti("scanlineThickness", d.scanlineThickness);
    d.scanlineIntensity = getd("scanlineIntensity", d.scanlineIntensity);
    d.noise = getd("noise", d.noise);
    d.vignette = getd("vignette", d.vignette);
    d.showTime = getb("showTime", d.showTime);
    d.showDate = getb("showDate", d.showDate);
    d.customText = m.value("customText", d.customText).toString();
    d.label = OsdLabel(std::clamp(geti("label", int(d.label)), 0, 3));
    d.corner = OsdCorner(std::clamp(geti("corner", int(d.corner)), 0, 3));
    d.fontSizePercent = getd("fontSizePercent", d.fontSizePercent);
    d.textColor = QColor(m.value("textColor", d.textColor.name()).toString());
    if (!d.textColor.isValid())
        d.textColor = QColor(216, 216, 216);
    d.textLeft = channel(geti("textLeft", int(d.textLeft)));
    d.textRight = channel(geti("textRight", int(d.textRight)));
    d.textOffset = geti("textOffset", d.textOffset);
    d.useCurrentTime = getb("useCurrentTime", d.useCurrentTime);
    d.dateTime = m.value("dateTime", d.dateTime).toDateTime();
    if (!d.dateTime.isValid())
        d.dateTime = QDateTime::currentDateTime();
    d.seed = m.value("seed", uint(d.seed)).toUInt();
    return d;
}

namespace vhs {

// ---------------------------------------------------------------------------
// Font and overlays
// ---------------------------------------------------------------------------

namespace {

QString& fontFamilyStorage()
{
    static QString family;
    return family;
}

inline float smoothstep(float e0, float e1, float x)
{
    const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

/// A number buffer living on a pool thread: saves a memory allocation per row
std::vector<float>& threadBuffer(size_t index, size_t size)
{
    thread_local std::vector<float> buffers[4];
    std::vector<float>& b = buffers[index];
    if (b.size() < size)
        b.resize(size);
    return b;
}

} // namespace

void registerFonts()
{
    if (!fontFamilyStorage().isEmpty())
        return;
    const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/VT323-Regular.ttf"));
    if (id >= 0) {
        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        if (!families.isEmpty())
            fontFamilyStorage() = families.first();
    }
}

QString fontFamily()
{
    if (!fontFamilyStorage().isEmpty())
        return fontFamilyStorage();
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

QImage renderOsd(const VhsParams& p, const QSize& size)
{
    if (size.isEmpty())
        return {};

    QStringList lines;
    const QDateTime dt = p.useCurrentTime ? QDateTime::currentDateTime() : p.dateTime;
    const QLocale locale = QLocale::c();   // English AM/PM and month names, as on camcorders
    if (p.showTime)
        lines << locale.toString(dt.time(), QStringLiteral("AP h:mm"));
    if (p.showDate)
        lines << locale.toString(dt.date(), QStringLiteral("MMM dd yyyy")).toUpper();
    if (!p.customText.trimmed().isEmpty())
        lines << p.customText.trimmed();

    if (lines.isEmpty() && p.label == OsdLabel::None)
        return {};

    // Text is drawn on black: the layer is then blended additively,
    // so black automatically becomes "transparent"
    QImage layer(size, QImage::Format_ARGB32);
    layer.fill(Qt::black);

    QPainter painter(&layer);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QFont font(fontFamily());
    font.setPixelSize(std::max(8, int(std::lround(size.height() * p.fontSizePercent / 100.0))));
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(p.textColor);

    const QFontMetrics fm(font);
    const int marginX = std::max(4, size.width() * 5 / 100);
    const int marginY = std::max(4, size.height() * 5 / 100);
    const int lineHeight = int(std::lround(fm.height() * 1.15));

    // --- Date/time/text block in the chosen corner ---
    const bool top = p.corner == OsdCorner::TopLeft || p.corner == OsdCorner::TopRight;
    const bool left = p.corner == OsdCorner::TopLeft || p.corner == OsdCorner::BottomLeft;
    const int blockHeight = int(lines.size()) * lineHeight;
    const int blockY = top ? marginY : size.height() - marginY - blockHeight;
    for (int i = 0; i < lines.size(); ++i) {
        const int textWidth = fm.horizontalAdvance(lines[i]);
        const int x = left ? marginX : size.width() - marginX - textWidth;
        painter.drawText(QPoint(x, blockY + i * lineHeight + fm.ascent()), lines[i]);
    }

    // --- Status label (PLAY / REC / PAUSE) in the upper corner not taken by the block ---
    if (p.label != OsdLabel::None) {
        const bool labelLeft = !(top && left && !lines.isEmpty());
        QString text;
        switch (p.label) {
        case OsdLabel::Play:  text = QStringLiteral("PLAY"); break;
        case OsdLabel::Rec:   text = QStringLiteral("REC"); break;
        case OsdLabel::Pause: text = QStringLiteral("PAUSE"); break;
        case OsdLabel::None:  break;
        }
        const int symbol = int(fm.ascent() * 0.7);
        const int gap = fm.horizontalAdvance(QLatin1Char(' '));
        const int textWidth = fm.horizontalAdvance(text);
        const int totalWidth = textWidth + gap + symbol;
        const int x = labelLeft ? marginX : size.width() - marginX - totalWidth;
        const int baseline = marginY + fm.ascent();
        const int symbolTop = baseline - fm.ascent() + (fm.ascent() - symbol) / 2;

        painter.setBrush(p.textColor);
        painter.setPen(Qt::NoPen);
        int textX = x;
        if (p.label == OsdLabel::Rec) {
            // Red recording dot before the text
            painter.setBrush(QColor(235, 45, 45));
            painter.drawEllipse(QRect(x, symbolTop, symbol, symbol));
            textX = x + symbol + gap;
        } else {
            const int sx = x + textWidth + gap;
            if (p.label == OsdLabel::Play) {
                QPainterPath tri;
                tri.moveTo(sx, symbolTop);
                tri.lineTo(sx + symbol, symbolTop + symbol / 2.0);
                tri.lineTo(sx, symbolTop + symbol);
                tri.closeSubpath();
                painter.drawPath(tri);
            } else {
                const int bar = std::max(2, symbol * 3 / 8);
                painter.drawRect(QRect(sx, symbolTop, bar, symbol));
                painter.drawRect(QRect(sx + symbol - bar, symbolTop, bar, symbol));
            }
        }
        painter.setPen(p.textColor);
        painter.drawText(QPoint(textX, baseline), text);
    }

    painter.end();
    return layer;
}

// ---------------------------------------------------------------------------
// The effect pipeline: four parallel passes over the rows
// ---------------------------------------------------------------------------

ImageOp makeOp(const VhsParams& params, const QImage& osdLayer)
{
    return [params, osdLayer](const QImage& source, QPromise<QImage>& promise) -> QImage {
        const VhsParams& p = params;
        const QImage in = toArgb32(source);
        if (in.isNull())
            return {};
        const int width = in.width();
        const int height = in.height();

        // ---------- Pass 1: color correction + color bleed (in YCbCr) ----------
        QImage graded(in.size(), QImage::Format_ARGB32);
        {
            const RowWriter writer(graded);
            const float bright = float(std::clamp(p.brightness, -100.0, 100.0)) * 1.28f;
            const float contrastFactor = p.contrast >= 0 ? 1.f + float(p.contrast) / 50.f : 1.f + float(p.contrast) / 100.f;
            const float satFactor = p.saturation >= 0 ? 1.f + float(p.saturation) / 50.f : 1.f + float(p.saturation) / 100.f;
            const int radius = std::max(0, p.chromaBleed);

            parallelRows(height, promise, {0, 35}, [&](int y) {
                const QRgb* s = constRow(in, y);
                QRgb* d = writer.row(y);
                std::vector<float>& Y = threadBuffer(0, size_t(width));
                std::vector<float>& Cb = threadBuffer(1, size_t(width));
                std::vector<float>& Cr = threadBuffer(2, size_t(width));

                for (int x = 0; x < width; ++x) {
                    float yy, cb, cr;
                    colorspace::rgbToYCbCr(float(qRed(s[x])), float(qGreen(s[x])), float(qBlue(s[x])), yy, cb, cr);
                    Y[size_t(x)] = (yy - 128.f) * contrastFactor + 128.f + bright;
                    Cb[size_t(x)] = (cb - 128.f) * satFactor + 128.f;
                    Cr[size_t(x)] = (cr - 128.f) * satFactor + 128.f;
                }

                if (radius > 0 && width > 1) {
                    // Horizontal box blur of chroma through prefix sums;
                    // luminance stays sharp, as on analog video
                    std::vector<float>& prefix = threadBuffer(3, size_t(width + 1) * 2);
                    float* pcb = prefix.data();
                    float* pcr = prefix.data() + (width + 1);
                    pcb[0] = 0.f;
                    pcr[0] = 0.f;
                    for (int x = 0; x < width; ++x) {
                        pcb[x + 1] = pcb[x] + Cb[size_t(x)];
                        pcr[x + 1] = pcr[x] + Cr[size_t(x)];
                    }
                    for (int x = 0; x < width; ++x) {
                        const int lo = std::max(0, x - radius);
                        const int hi = std::min(width - 1, x + radius);
                        const float inv = 1.f / float(hi - lo + 1);
                        Cb[size_t(x)] = (pcb[hi + 1] - pcb[lo]) * inv;
                        Cr[size_t(x)] = (pcr[hi + 1] - pcr[lo]) * inv;
                    }
                }

                for (int x = 0; x < width; ++x) {
                    float r, g, b;
                    colorspace::yCbCrToRgb(Y[size_t(x)], Cb[size_t(x)], Cr[size_t(x)], r, g, b);
                    d[x] = qRgba(clampByte(r), clampByte(g), clampByte(b), qAlpha(s[x]));
                }
            });
        }
        if (promise.isCanceled())
            return {};

        // ---------- Pass 2: line jitter + tracking band ----------
        QImage shifted(in.size(), QImage::Format_ARGB32);
        {
            const RowWriter writer(shifted);
            const int jitter = std::max(0, p.lineJitter);
            const int bandHeight = std::clamp(p.trackingHeight, 1, height);
            const int bandY0 = int(std::lround(std::clamp(p.trackingPosition, 0.0, 1.0) * (height - bandHeight)));
            const bool tracking = p.tracking && p.trackingShift != 0;

            parallelRows(height, promise, {35, 50}, [&](int y) {
                const QRgb* s = constRow(graded, y);
                QRgb* d = writer.row(y);
                Rng rng(hashCombine(p.seed, 0x1000u, uint32_t(y)));

                int shift = 0;
                float noiseMix = 0.f;
                if (jitter > 0) {
                    // Smooth "floating" of the lines: noise is interpolated between key points
                    // every 12 lines, so neighboring lines shift together as with uneven tape
                    // transport; once in a while a single line tears
                    constexpr int wavePeriod = 12;
                    const int cell = y / wavePeriod;
                    const float t = float(y - cell * wavePeriod) / float(wavePeriod);
                    const float n0 = float(hashCombine(p.seed, 0x3000u, uint32_t(cell)) & 0xFFFF) / 32767.5f - 1.f;
                    const float n1 = float(hashCombine(p.seed, 0x3000u, uint32_t(cell + 1)) & 0xFFFF) / 32767.5f - 1.f;
                    const float wave = n0 + (n1 - n0) * smoothstep(0.f, 1.f, t);
                    shift += int(std::lround(wave * float(jitter)));
                    if (rng.unit() < 0.02f)
                        shift += rng.range(-jitter * 3, jitter * 3);
                }
                if (tracking && y >= bandY0 && y < bandY0 + bandHeight) {
                    const float t = (y - bandY0 + 0.5f) / float(bandHeight);
                    const float profile = std::sin(t * std::numbers::pi_v<float>);   // smooth band envelope
                    shift += int(std::lround(profile * float(p.trackingShift) * (0.5f + 0.5f * rng.unit())));
                    noiseMix = 0.6f * profile;
                }

                if (shift == 0 && noiseMix <= 0.f) {
                    std::memcpy(d, s, size_t(width) * sizeof(QRgb));
                    return;
                }

                float gray = 0.f;
                for (int x = 0; x < width; ++x) {
                    int sx = (x - shift) % width;
                    if (sx < 0)
                        sx += width;
                    QRgb c = s[sx];
                    if (noiseMix > 0.f) {
                        // Interference: gray streaks in 4 px blocks, color is lost inside the band
                        if ((x & 3) == 0)
                            gray = 40.f + rng.unit() * 215.f;
                        const float keep = 1.f - noiseMix;
                        const float l = colorspace::luma(c);
                        const float r = (qRed(c) * 0.6f + l * 0.4f) * keep + gray * noiseMix;
                        const float g = (qGreen(c) * 0.6f + l * 0.4f) * keep + gray * noiseMix;
                        const float b = (qBlue(c) * 0.6f + l * 0.4f) * keep + gray * noiseMix;
                        c = qRgba(clampByte(r), clampByte(g), clampByte(b), qAlpha(c));
                    }
                    d[x] = c;
                }
            });
        }
        if (promise.isCanceled())
            return {};

        // ---------- Pass 3: chromatic aberration (left and right layers) ----------
        QImage aberrated(in.size(), QImage::Format_ARGB32);
        {
            const RowWriter writer(aberrated);
            const unsigned maskL = channelMask(p.imageLeft);
            const unsigned maskR = channelMask(p.imageRight);
            const int off = std::max(0, p.imageOffset);

            parallelRows(height, promise, {50, 70}, [&](int y) {
                const QRgb* s = constRow(shifted, y);
                QRgb* d = writer.row(y);
                if (off == 0) {
                    std::memcpy(d, s, size_t(width) * sizeof(QRgb));
                    return;
                }
                for (int x = 0; x < width; ++x) {
                    const QRgb base = s[x];
                    // The left layer is shifted left, so the pixel is taken further right; the right one the opposite way.
                    // Beyond the image edge the layer is empty (black).
                    const QRgb leftLayer = (x + off < width) ? s[x + off] : 0u;
                    const QRgb rightLayer = (x - off >= 0) ? s[x - off] : 0u;
                    const int r = (maskL & 1u) ? qRed(leftLayer) : (maskR & 1u) ? qRed(rightLayer) : qRed(base);
                    const int g = (maskL & 2u) ? qGreen(leftLayer) : (maskR & 2u) ? qGreen(rightLayer) : qGreen(base);
                    const int b = (maskL & 4u) ? qBlue(leftLayer) : (maskR & 4u) ? qBlue(rightLayer) : qBlue(base);
                    d[x] = qRgba(r, g, b, qAlpha(base));
                }
            });
        }
        if (promise.isCanceled())
            return {};

        // ---------- Pass 4: scanlines + grain + vignette + overlays ----------
        QImage out(in.size(), QImage::Format_ARGB32);
        {
            const RowWriter writer(out);
            const QImage osd = (!osdLayer.isNull() && osdLayer.size() == in.size()) ? toArgb32(osdLayer) : QImage();
            const bool hasOsd = !osd.isNull();
            const unsigned tMaskL = channelMask(p.textLeft);
            const unsigned tMaskR = channelMask(p.textRight);
            const int tOff = std::max(0, p.textOffset);

            const bool scan = p.scanlines && p.scanlineIntensity > 0.0;
            const int period = std::max(1, p.scanlinePeriod);
            const int thickness = std::clamp(p.scanlineThickness, 1, period);
            const float scanMul = 1.f - float(std::clamp(p.scanlineIntensity, 0.0, 1.0));
            const float noiseAmp = float(std::clamp(p.noise, 0.0, 1.0)) * 128.f;
            const float vignette = float(std::clamp(p.vignette, 0.0, 1.0));
            const float cx = std::max(0.5f, (width - 1) / 2.f);
            const float cy = std::max(0.5f, (height - 1) / 2.f);
            const float invDiag = 1.f / std::sqrt(2.f);

            parallelRows(height, promise, {70, 100}, [&](int y) {
                const QRgb* s = constRow(aberrated, y);
                const QRgb* o = hasOsd ? constRow(osd, y) : nullptr;
                QRgb* d = writer.row(y);
                Rng rng(hashCombine(p.seed, 0x2000u, uint32_t(y)));

                const float lineMul = (scan && (y % period) < thickness) ? scanMul : 1.f;
                const float dy = (y - cy) / cy;

                for (int x = 0; x < width; ++x) {
                    float r = float(qRed(s[x]));
                    float g = float(qGreen(s[x]));
                    float b = float(qBlue(s[x]));

                    if (noiseAmp > 0.f) {
                        const float n = rng.signedUnit() * noiseAmp;
                        r += n;
                        g += n;
                        b += n;
                    }

                    float mul = lineMul;
                    if (vignette > 0.f) {
                        const float dx = (x - cx) / cx;
                        const float dist = std::sqrt(dx * dx + dy * dy) * invDiag;
                        mul *= 1.f - vignette * smoothstep(0.35f, 1.f, dist);
                    }
                    r *= mul;
                    g *= mul;
                    b *= mul;

                    if (o) {
                        // Overlay with its own aberration, blended additively (the black background disappears)
                        const QRgb tb = o[x];
                        const QRgb tl = (x + tOff < width) ? o[x + tOff] : 0u;
                        const QRgb tr = (x - tOff >= 0) ? o[x - tOff] : 0u;
                        const int textR = (tMaskL & 1u) ? qRed(tl) : (tMaskR & 1u) ? qRed(tr) : qRed(tb);
                        const int textG = (tMaskL & 2u) ? qGreen(tl) : (tMaskR & 2u) ? qGreen(tr) : qGreen(tb);
                        const int textB = (tMaskL & 4u) ? qBlue(tl) : (tMaskR & 4u) ? qBlue(tr) : qBlue(tb);
                        r += textR * lineMul;
                        g += textG * lineMul;
                        b += textB * lineMul;
                    }

                    d[x] = qRgba(clampByte(r), clampByte(g), clampByte(b), qAlpha(s[x]));
                }
            });
        }

        return promise.isCanceled() ? QImage() : out;
    };
}

} // namespace vhs
} // namespace kolorit
