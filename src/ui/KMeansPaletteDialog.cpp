#include "ui/KMeansPaletteDialog.h"

#include "processing/PaletteBuilder.h"
#include "ui/PaletteStrip.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace kolorit {

KMeansPaletteDialog::KMeansPaletteDialog(const QImage& source, QWidget* parent)
    : QDialog(parent)
    , m_source(source)
    , m_count(new QSpinBox(this))
    , m_iterations(new QSpinBox(this))
    , m_seed(new QSpinBox(this))
    , m_computeButton(new QPushButton(tr("Compute"), this))
    , m_progress(new QProgressBar(this))
    , m_status(new QLabel(this))
    , m_strip(new PaletteStrip(this))
{
    setWindowTitle(tr("k-means palette"));
    setMinimumWidth(400);

    m_count->setRange(2, 256);
    m_count->setValue(16);
    m_iterations->setRange(1, 100);
    m_iterations->setValue(12);
    m_seed->setRange(0, 999999);
    m_seed->setValue(1);

    auto* group = new QGroupBox(tr("Parameters"), this);
    auto* form = new QFormLayout(group);
    form->addRow(tr("Number of colors:"), m_count);
    form->addRow(tr("Iterations (maximum):"), m_iterations);
    form->addRow(tr("Seed:"), m_seed);

    auto* hint = new QLabel(tr("The algorithm groups pixels by color similarity and takes the center of each group. "
                               "It gives the palette that renders the source image most faithfully."),
                            this);
    hint->setWordWrap(true);

    m_progress->setRange(0, 100);
    m_progress->setVisible(false);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_okButton = buttons->button(QDialogButtonBox::Ok);
    m_okButton->setText(tr("Use palette"));
    m_okButton->setEnabled(false);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(group);
    layout->addWidget(hint);
    layout->addWidget(m_computeButton);
    layout->addWidget(m_progress);
    layout->addWidget(m_status);
    layout->addWidget(m_strip);
    layout->addStretch(1);
    layout->addWidget(buttons);

    connect(m_computeButton, &QPushButton::clicked, this, &KMeansPaletteDialog::compute);
    connect(&m_runner, &ImageTaskRunner::progressChanged, m_progress, &QProgressBar::setValue);
    connect(&m_runner, &ImageTaskRunner::finished, this, &KMeansPaletteDialog::onFinished);
    connect(&m_runner, &ImageTaskRunner::canceled, this, [this] {
        m_progress->setVisible(false);
        m_computeButton->setEnabled(true);
        m_status->setText(tr("Cancelled"));
    });

    // The parameters changed, so the previous result no longer matches them
    auto invalidate = [this] { m_okButton->setEnabled(false); };
    connect(m_count, &QSpinBox::valueChanged, this, invalidate);
    connect(m_iterations, &QSpinBox::valueChanged, this, invalidate);
    connect(m_seed, &QSpinBox::valueChanged, this, invalidate);
}

void KMeansPaletteDialog::compute()
{
    m_computeButton->setEnabled(false);
    m_okButton->setEnabled(false);
    m_progress->setValue(0);
    m_progress->setVisible(true);
    m_status->setText(tr("Clustering…"));
    m_runner.start(m_source, palette_builder::kMeansOp(m_count->value(), m_iterations->value(),
                                                       uint32_t(m_seed->value())));
}

void KMeansPaletteDialog::onFinished(const QImage& paletteImage, qint64 ms)
{
    m_palette = Palette::fromImage(paletteImage);
    m_strip->setColors(m_palette);
    m_progress->setVisible(false);
    m_computeButton->setEnabled(true);
    m_okButton->setEnabled(!m_palette.isEmpty());
    m_status->setText(tr("Colors found: %1 (in %2 ms)").arg(m_palette.size()).arg(ms));
}

void KMeansPaletteDialog::reject()
{
    m_runner.cancel();
    QDialog::reject();
}

} // namespace kolorit
