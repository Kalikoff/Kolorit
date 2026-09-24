#pragma once

#include "core/Palette.h"

#include <QDialog>
#include <QImage>

class QSpinBox;

namespace kolorit {

class PaletteStrip;

/// Palette from an image: a grid of columns×rows chunks, the average color of each
/// (with a live preview)
class ChunkPaletteDialog : public QDialog {
    Q_OBJECT
public:
    explicit ChunkPaletteDialog(const QImage& source, QWidget* parent = nullptr);

    const Palette& palette() const { return m_palette; }
    int columns() const;
    int rows() const;
    void setGrid(int columns, int rows);

private:
    void recompute();

    QImage m_source;
    Palette m_palette;
    QSpinBox* m_columns;
    QSpinBox* m_rows;
    PaletteStrip* m_strip;
};

} // namespace kolorit
