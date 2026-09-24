#include "ui/PaletteStrip.h"

#include <QPainter>

namespace kolorit {

PaletteStrip::PaletteStrip(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void PaletteStrip::setColors(const Palette& palette)
{
    m_palette = palette;
    setToolTip(tr("Colors: %1").arg(palette.size()));
    update();
}

QSize PaletteStrip::sizeHint() const
{
    return {320, 36};
}

QSize PaletteStrip::minimumSizeHint() const
{
    return {120, 24};
}

void PaletteStrip::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRect r = rect().adjusted(0, 0, -1, -1);
    p.setPen(palette().color(QPalette::Mid));
    p.setBrush(QColor(40, 40, 44));
    p.drawRect(r);

    const int n = m_palette.size();
    if (n == 0) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(r, Qt::AlignCenter, tr("palette is empty"));
        return;
    }

    p.setPen(Qt::NoPen);
    const QRectF inner = QRectF(r).adjusted(1, 1, 0, 0);
    const double cell = inner.width() / n;
    for (int i = 0; i < n; ++i) {
        // Cells overlap by a pixel so there are no gaps between them at fractional widths
        const QRectF cellRect(inner.left() + i * cell, inner.top(), cell + 1.0, inner.height());
        p.fillRect(cellRect, QColor::fromRgb(m_palette.at(i)));
    }
}

} // namespace kolorit
