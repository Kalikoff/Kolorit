#pragma once

#include "core/Palette.h"

#include <QWidget>

namespace kolorit {

/// A strip of palette colors: a compact preview for dialogs
class PaletteStrip : public QWidget {
    Q_OBJECT
public:
    explicit PaletteStrip(QWidget* parent = nullptr);

    void setColors(const Palette& palette);
    const Palette& colors() const { return m_palette; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Palette m_palette;
};

} // namespace kolorit
