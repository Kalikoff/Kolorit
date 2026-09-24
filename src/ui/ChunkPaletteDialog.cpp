#include "ui/ChunkPaletteDialog.h"

#include "processing/PaletteBuilder.h"
#include "ui/PaletteStrip.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace kolorit {

ChunkPaletteDialog::ChunkPaletteDialog(const QImage& source, QWidget* parent)
    : QDialog(parent)
    , m_source(source)
    , m_columns(new QSpinBox(this))
    , m_rows(new QSpinBox(this))
    , m_strip(new PaletteStrip(this))
{
    setWindowTitle(tr("Palette from image"));
    setMinimumWidth(380);

    m_columns->setRange(1, 256);
    m_rows->setRange(1, 256);
    m_columns->setValue(8);
    m_rows->setValue(8);

    auto* group = new QGroupBox(tr("Number of chunks"), this);
    auto* form = new QFormLayout(group);
    form->addRow(tr("Across (X):"), m_columns);
    form->addRow(tr("Down (Y):"), m_rows);

    auto* hint = new QLabel(tr("The image is split into a grid, the color of every cell is averaged, "
                               "duplicate colors are dropped."),
                            this);
    hint->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create palette"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(group);
    layout->addWidget(hint);
    layout->addWidget(new QLabel(tr("Palette preview:"), this));
    layout->addWidget(m_strip);
    layout->addStretch(1);
    layout->addWidget(buttons);

    connect(m_columns, &QSpinBox::valueChanged, this, &ChunkPaletteDialog::recompute);
    connect(m_rows, &QSpinBox::valueChanged, this, &ChunkPaletteDialog::recompute);

    recompute();
}

int ChunkPaletteDialog::columns() const
{
    return m_columns->value();
}

int ChunkPaletteDialog::rows() const
{
    return m_rows->value();
}

void ChunkPaletteDialog::setGrid(int columns, int rows)
{
    m_columns->setValue(columns);
    m_rows->setValue(rows);
}

void ChunkPaletteDialog::recompute()
{
    // Averaging chunks is one fast parallel pass, so it is computed synchronously
    m_palette = palette_builder::fromChunks(m_source, m_columns->value(), m_rows->value());
    m_strip->setColors(m_palette);
    setWindowTitle(tr("Palette from image: %1 colors").arg(m_palette.size()));
}

} // namespace kolorit
