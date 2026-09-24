#include "core/Palette.h"
#include "core/ColorSpace.h"

#include <QImageReader>
#include <QObject>
#include <QSet>

#include <algorithm>
#include <limits>
#include <vector>

namespace kolorit {

Palette::Palette(QVector<QRgb> colors)
    : m_colors(std::move(colors))
{
    for (QRgb& c : m_colors)
        c |= 0xFF000000u;
}

void Palette::add(QRgb color)
{
    if (m_colors.size() < kMaxColors)
        m_colors.append(color | 0xFF000000u);
}

bool Palette::addUnique(QRgb color)
{
    color |= 0xFF000000u;
    if (m_colors.size() >= kMaxColors || m_colors.contains(color))
        return false;
    m_colors.append(color);
    return true;
}

void Palette::set(int index, QRgb color)
{
    if (index >= 0 && index < m_colors.size())
        m_colors[index] = color | 0xFF000000u;
}

void Palette::removeAt(int index)
{
    if (index >= 0 && index < m_colors.size())
        m_colors.removeAt(index);
}

void Palette::move(int from, int to)
{
    if (from < 0 || from >= m_colors.size() || to < 0 || to >= m_colors.size() || from == to)
        return;
    m_colors.move(from, to);
}

void Palette::clear()
{
    m_colors.clear();
}

void Palette::sortByLuminance()
{
    std::stable_sort(m_colors.begin(), m_colors.end(), [](QRgb a, QRgb b) {
        return colorspace::luma(a) < colorspace::luma(b);
    });
}

void Palette::sortByHue()
{
    // Grays (no saturation) come first by lightness, then chromatic colors by hue
    auto key = [](QRgb c) {
        float h, s, l;
        colorspace::rgbToHsl(qRed(c), qGreen(c), qBlue(c), h, s, l);
        return s < 0.08f ? std::pair{-1.f, l} : std::pair{h, l};
    };
    std::stable_sort(m_colors.begin(), m_colors.end(), [&](QRgb a, QRgb b) { return key(a) < key(b); });
}

void Palette::sortAsGradient()
{
    if (m_colors.size() < 3)
        return;

    std::vector<Vec3> lab;
    lab.reserve(size_t(m_colors.size()));
    for (QRgb c : m_colors)
        lab.push_back(colorspace::toModel(c, ColorModel::Lab));

    std::vector<bool> used(lab.size(), false);
    QVector<QRgb> ordered;
    ordered.reserve(m_colors.size());

    // Start from the darkest one so the gradient runs from shadows to highlights
    int current = 0;
    for (int i = 1; i < m_colors.size(); ++i)
        if (lab[size_t(i)].x < lab[size_t(current)].x)
            current = i;

    used[size_t(current)] = true;
    ordered.append(m_colors.at(current));
    for (int step = 1; step < m_colors.size(); ++step) {
        int best = -1;
        float bestDistance = std::numeric_limits<float>::max();
        for (int i = 0; i < m_colors.size(); ++i) {
            if (used[size_t(i)])
                continue;
            const float d = colorspace::distanceSq(lab[size_t(current)], lab[size_t(i)]);
            if (d < bestDistance) {
                bestDistance = d;
                best = i;
            }
        }
        if (best < 0)
            break;
        used[size_t(best)] = true;
        ordered.append(m_colors.at(best));
        current = best;
    }
    m_colors = ordered;
}

int Palette::merge(const Palette& other)
{
    int added = 0;
    for (QRgb c : other.colors())
        if (addUnique(c))
            ++added;
    return added;
}

QImage Palette::toImage() const
{
    if (m_colors.isEmpty())
        return {};
    QImage image(m_colors.size(), 1, QImage::Format_ARGB32);
    auto* row = reinterpret_cast<QRgb*>(image.bits());
    std::copy(m_colors.begin(), m_colors.end(), row);
    return image;
}

Palette Palette::fromImage(const QImage& source, int maxColors)
{
    Palette palette;
    if (source.isNull())
        return palette;

    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    QSet<QRgb> seen;
    for (int y = 0; y < image.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(row[x]) == 0)
                continue;
            const QRgb c = row[x] | 0xFF000000u;
            if (seen.contains(c))
                continue;
            seen.insert(c);
            palette.m_colors.append(c);
            if (palette.m_colors.size() >= maxColors)
                return palette;
        }
    }
    return palette;
}

Palette Palette::load(const QString& path, QString* errorMessage)
{
    QImageReader reader(path);
    const QImage image = reader.read();
    if (image.isNull()) {
        if (errorMessage)
            *errorMessage = reader.errorString();
        return {};
    }
    Palette palette = fromImage(image);
    if (palette.isEmpty() && errorMessage)
        *errorMessage = QObject::tr("The palette file contains no opaque colors");
    return palette;
}

bool Palette::save(const QString& path, QString* errorMessage) const
{
    if (m_colors.isEmpty()) {
        if (errorMessage)
            *errorMessage = QObject::tr("The palette is empty");
        return false;
    }
    if (!toImage().save(path, "PNG")) {
        if (errorMessage)
            *errorMessage = QObject::tr("Could not write the file %1").arg(path);
        return false;
    }
    return true;
}

} // namespace kolorit
