#pragma once

#include <QImage>
#include <QRgb>
#include <QString>
#include <QVector>

namespace kolorit {

/// A palette is an ordered list of opaque colors.
/// On disk it is stored as an N×1 PNG image where every pixel is one palette color.
class Palette {
public:
    static constexpr int kMaxColors = 4096;

    Palette() = default;
    explicit Palette(QVector<QRgb> colors);

    bool isEmpty() const { return m_colors.isEmpty(); }
    int size() const { return int(m_colors.size()); }
    const QVector<QRgb>& colors() const { return m_colors; }
    QRgb at(int index) const { return m_colors.at(index); }

    void add(QRgb color);
    /// Add the color unless it is already there. Returns true if it was added.
    bool addUnique(QRgb color);
    void set(int index, QRgb color);
    void removeAt(int index);
    void move(int from, int to);
    void clear();

    void sortByLuminance();
    void sortByHue();

    /// Order the colors as a gradient: neighbors are the colors closest to each other.
    ///
    /// Sorting by luminance or by hue breaks the row wherever the other coordinate
    /// changes: dark blue and dark red sit side by side by luminance even though they
    /// are far apart. Here a path through all colors with a small sum of steps is built:
    /// start from the darkest and always take the nearest of the remaining ones (distance
    /// in CIE Lab). This is what a gradient map and a duotone need, since there the order
    /// of the palette is the shape of the gradient.
    void sortAsGradient();

    /// Append the colors of another palette, skipping those already present.
    /// Returns the number of colors added.
    int merge(const Palette& other);

    /// The palette as an N×1 image
    QImage toImage() const;

    /// Build a palette from all unique opaque colors of an image, in scan order.
    /// For an N×1 image that is exactly its row of colors; for an arbitrary picture it
    /// is the set of its colors.
    static Palette fromImage(const QImage& image, int maxColors = kMaxColors);

    static Palette load(const QString& path, QString* errorMessage = nullptr);
    bool save(const QString& path, QString* errorMessage = nullptr) const;

    bool operator==(const Palette& other) const { return m_colors == other.m_colors; }
    bool operator!=(const Palette& other) const { return !(*this == other); }

private:
    QVector<QRgb> m_colors;
};

} // namespace kolorit
