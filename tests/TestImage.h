#pragma once

// Synthetic test image: a gradient sky, a sun, houses, a checkerboard patch,
// thin lines and slight noise: enough detail for repainting, dithering, artifact
// removal, pixel art, the disc and VHS. The tests do not depend on external files.

#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>

namespace kolorit::testing {

inline QImage makeTestImage(int width = 1200, int height = 900)
{
    QImage image(width, height, QImage::Format_ARGB32);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Sky: a sunset gradient
    QLinearGradient sky(0, 0, 0, height * 0.6);
    sky.setColorAt(0.0, QColor(58, 64, 140));
    sky.setColorAt(0.45, QColor(226, 102, 120));
    sky.setColorAt(1.0, QColor(255, 196, 92));
    p.fillRect(image.rect(), sky);

    // Sun with a halo
    QRadialGradient sun(width * 0.72, height * 0.42, width * 0.12);
    sun.setColorAt(0.0, QColor(255, 250, 220));
    sun.setColorAt(0.35, QColor(255, 220, 120));
    sun.setColorAt(1.0, QColor(255, 196, 92, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(sun);
    p.drawEllipse(QPointF(width * 0.72, height * 0.42), width * 0.12, width * 0.12);

    // Ground and houses of different colors
    p.setBrush(QColor(40, 44, 52));
    p.drawRect(0, int(height * 0.6), width, height);
    QRandomGenerator rng(7);
    int x = 0;
    while (x < width) {
        const int w = 40 + int(rng.bounded(90));
        const int h = 60 + int(rng.bounded(int(height * 0.35)));
        const QColor wall = QColor::fromHsv(int(rng.bounded(360)), 60 + int(rng.bounded(120)), 90 + int(rng.bounded(140)));
        p.setBrush(wall);
        p.drawRect(x, int(height * 0.6) - h, w, h);
        // windows
        p.setBrush(QColor(255, 236, 150));
        for (int wy = int(height * 0.6) - h + 12; wy < int(height * 0.6) - 14; wy += 22)
            for (int wx = x + 8; wx < x + w - 12; wx += 18)
                if (rng.bounded(3) != 0)
                    p.drawRect(wx, wy, 8, 12);
        x += w + 6;
    }

    // Checkerboard patch and thin lines, for dithering and outlines
    for (int cy = 0; cy < 6; ++cy)
        for (int cx = 0; cx < 6; ++cx)
            p.fillRect(int(width * 0.05) + cx * 16, int(height * 0.68) + cy * 16, 16, 16,
                       (cx + cy) % 2 ? QColor(240, 240, 240) : QColor(30, 30, 30));
    p.setPen(QPen(QColor(255, 255, 255), 1));
    for (int i = 0; i < 8; ++i)
        p.drawLine(int(width * 0.55) + i * 9, int(height * 0.7), int(width * 0.55) + i * 9, int(height * 0.95));

    // A circle with a gradient: smooth transitions
    QRadialGradient ball(width * 0.3, height * 0.82, width * 0.08);
    ball.setColorAt(0.0, QColor(120, 220, 255));
    ball.setColorAt(1.0, QColor(10, 60, 120));
    p.setPen(Qt::NoPen);
    p.setBrush(ball);
    p.drawEllipse(QPointF(width * 0.3, height * 0.82), width * 0.08, width * 0.08);
    p.end();

    // Slight noise
    for (int y = 0; y < height; ++y) {
        auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int xx = 0; xx < width; ++xx) {
            const int n = int(rng.bounded(13)) - 6;
            row[xx] = qRgb(qBound(0, qRed(row[xx]) + n, 255), qBound(0, qGreen(row[xx]) + n, 255),
                           qBound(0, qBlue(row[xx]) + n, 255));
        }
    }
    return image;
}

/// Load an image from a file or, if the path is empty or "-", generate a test one
inline QImage loadOrGenerate(const QString& path)
{
    if (!path.isEmpty() && path != QStringLiteral("-")) {
        const QImage loaded(path);
        if (!loaded.isNull())
            return loaded.convertToFormat(QImage::Format_ARGB32);
    }
    return makeTestImage();
}

} // namespace kolorit::testing
