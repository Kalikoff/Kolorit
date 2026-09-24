#pragma once

#include "core/ParallelFor.h"

#include <QImage>

namespace kolorit {

/// Convert the image to ARGB32, the common working format of all operations.
/// Nothing is copied if the format already fits (QImage shares data implicitly).
inline QImage toArgb32(const QImage& image)
{
    return image.format() == QImage::Format_ARGB32 ? image : image.convertToFormat(QImage::Format_ARGB32);
}

/// Direct access to the rows of the output image from several threads.
///
/// QImage::scanLine() calls detach() and is not meant to be called concurrently, so
/// the data pointer and the stride are taken once before the threads start and rows
/// are found with pointer arithmetic.
class RowWriter {
public:
    explicit RowWriter(QImage& image)
        : m_bits(image.bits())
        , m_stride(image.bytesPerLine())
    {
    }

    QRgb* row(int y) const { return reinterpret_cast<QRgb*>(m_bits + m_stride * y); }

private:
    uchar* m_bits;
    qsizetype m_stride;
};

/// Reading rows of the source image (const, no detach, so it is thread-safe)
inline const QRgb* constRow(const QImage& image, int y)
{
    return reinterpret_cast<const QRgb*>(image.constScanLine(y));
}

/// Apply a per-pixel function fn(QRgb) -> QRgb to the whole image, rows in parallel.
/// Returns a null image if the operation was cancelled.
template <typename Progress, typename Fn>
QImage mapPixels(const QImage& source, Progress& progress, ProgressSpan span, Fn&& fn)
{
    const QImage in = toArgb32(source);
    QImage out(in.size(), QImage::Format_ARGB32);
    const RowWriter writer(out);
    const int width = in.width();

    parallelRows(in.height(), progress, span, [&](int y) {
        const QRgb* s = constRow(in, y);
        QRgb* d = writer.row(y);
        for (int x = 0; x < width; ++x)
            d[x] = fn(s[x]);
    });

    return progress.isCanceled() ? QImage() : out;
}

} // namespace kolorit
