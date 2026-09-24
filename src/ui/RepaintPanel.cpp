#include "ui/RepaintPanel.h"

#include "core/ColorSpace.h"
#include "ui/Theme.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace kolorit {

namespace {

QString modelHint(ColorModel model)
{
    switch (model) {
    case ColorModel::RGB:
        return QObject::tr("Distance between channels. Holds luminance most evenly. The best choice for palettes of "
                           "pure colors (ZX Spectrum) and for tinted monochrome palettes such as the Game Boy.");
    case ColorModel::HSL:
        return QObject::tr("Hue weighs twice as much as lightness: color is kept more precisely, tones are simplified more. "
                           "Good for colorful palettes, unsuitable for grayscale palettes.");
    case ColorModel::HSV:
        return QObject::tr("Like HSL, but \"brightness\" is the largest channel, so saturated blue counts as "
                           "light as white and shadows get lifted. Not for gray palettes.");
    case ColorModel::XYZ:
        return QObject::tr("Linear light: highlights are told apart in detail, shadows collapse, since a difference "
                           "in the shadows weighs about eight times less there. More an artistic device than accuracy.");
    case ColorModel::Lab:
        return QObject::tr("Perceptually uniform: distance matches the visible difference between colors. Usually the "
                           "best choice for photos, but loses the light tones on tinted monochrome palettes.");
    }
    return {};
}

QString ditherHint(Dithering d)
{
    switch (d) {
    case Dithering::None:
        return QObject::tr("Every pixel is simply replaced with the nearest color.");
    case Dithering::Bayer2:
    case Dithering::Bayer4:
    case Dithering::Bayer8:
        return QObject::tr("An ordered pattern: a retro look, as on old screens. The larger the matrix, "
                           "the more halftones and the finer the grid.");
    case Dithering::BlueNoise:
        return QObject::tr("Ordered, but without a grid: the thresholds are placed so the pattern dissolves "
                           "into an even grain. The best-looking ordered method, and just as fast.");
    case Dithering::FloydSteinberg:
        return QObject::tr("Error diffusion: smooth transitions with few colors.");
    case Dithering::Atkinson:
        return QObject::tr("Spreads only 6/8 of the error: a more contrasty, \"crisp\" picture, fine "
                           "transitions are not smeared. The look of the classic Macintosh.");
    case Dithering::Sierra:
        return QObject::tr("A wide diffusion kernel: softer than Floyd–Steinberg, fewer visible trails.");
    case Dithering::Stucki:
        return QObject::tr("An even wider kernel: the smoothest transitions, but a slower computation.");
    case Dithering::JarvisJudiceNinke:
        return QObject::tr("The widest diffusion kernel: the softest and the slowest.");
    }
    return {};
}

} // namespace

RepaintPanel::RepaintPanel(QWidget* parent)
    : QWidget(parent)
    , m_model(new QComboBox(this))
    , m_dither(new QComboBox(this))
    , m_strengthSlider(new QSlider(Qt::Horizontal, this))
    , m_strength(new QSpinBox(this))
    , m_strengthLabel(new QLabel(tr("Pattern strength:"), this))
    , m_button(new QPushButton(tr("Repaint"), this))
    , m_status(new QLabel(this))
{
    for (ColorModel model : colorspace::allModels()) {
        m_model->addItem(colorspace::modelName(model), int(model));
        m_model->setItemData(m_model->count() - 1, modelHint(model), Qt::ToolTipRole);
    }
    m_model->setToolTip(tr("The space in which the nearest palette color is searched for"));

    for (Dithering d : allDitherings()) {
        m_dither->addItem(ditheringName(d), int(d));
        m_dither->setItemData(m_dither->count() - 1, ditherHint(d), Qt::ToolTipRole);
    }
    m_dither->setToolTip(tr("How to mix palette colors to render halftones"));

    m_strengthSlider->setRange(0, 128);
    m_strength->setRange(0, 128);
    m_strengthSlider->setValue(32);
    m_strength->setValue(32);
    m_strength->setToolTip(tr("Amplitude of ordered dithering in RGB units"));

    auto* strengthRow = new QHBoxLayout;
    strengthRow->addWidget(m_strengthSlider, 1);
    strengthRow->addWidget(m_strength);

    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->addRow(tr("Color model:"), m_model);
    form->addRow(tr("Dithering:"), m_dither);
    form->addRow(m_strengthLabel, strengthRow);

    m_button->setObjectName(QStringLiteral("primaryButton"));
    m_button->setToolTip(tr("Replace every pixel with the nearest palette color (Ctrl+R)"));
    m_button->setCursor(Qt::PointingHandCursor);

    m_status->setObjectName(QStringLiteral("hintLabel"));
    m_status->setWordWrap(true);
    m_status->setAlignment(Qt::AlignHCenter);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);
    layout->addLayout(form);
    layout->addWidget(m_button);
    layout->addWidget(m_status);
    layout->addStretch(1);

    connect(m_strengthSlider, &QSlider::valueChanged, m_strength, &QSpinBox::setValue);
    connect(m_strength, &QSpinBox::valueChanged, m_strengthSlider, &QSlider::setValue);
    connect(m_strength, &QSpinBox::valueChanged, this, &RepaintPanel::paramsChanged);
    connect(m_model, &QComboBox::currentIndexChanged, this, &RepaintPanel::paramsChanged);
    connect(m_dither, &QComboBox::currentIndexChanged, this, [this] {
        updateState();
        emit paramsChanged();
    });
    connect(m_button, &QPushButton::clicked, this, &RepaintPanel::repaintRequested);

    refreshIcons();
    updateState();
}

RepaintParams RepaintPanel::params() const
{
    RepaintParams p;
    p.model = ColorModel(m_model->currentData().toInt());
    p.dithering = Dithering(m_dither->currentData().toInt());
    p.ditherStrength = m_strength->value();
    return p;
}

void RepaintPanel::setParams(const RepaintParams& params)
{
    m_model->setCurrentIndex(std::max(0, m_model->findData(int(params.model))));
    m_dither->setCurrentIndex(std::max(0, m_dither->findData(int(params.dithering))));
    m_strength->setValue(params.ditherStrength);
    updateState();
}

void RepaintPanel::setPaletteSize(int count)
{
    m_paletteSize = count;
    updateState();
}

void RepaintPanel::setImageLoaded(bool loaded)
{
    m_imageLoaded = loaded;
    updateState();
}

void RepaintPanel::setBusy(bool busy)
{
    m_busy = busy;
    updateState();
}

void RepaintPanel::refreshIcons()
{
    m_button->setIcon(theme::icon(QStringLiteral("repaint"), Qt::white));
    m_button->setIconSize(QSize(18, 18));
}

void RepaintPanel::updateState()
{
    const auto d = Dithering(m_dither->currentData().toInt());
    const bool ordered = isOrderedDithering(d);
    m_strengthLabel->setVisible(ordered);
    m_strengthSlider->setVisible(ordered);
    m_strength->setVisible(ordered);

    const bool ready = m_imageLoaded && m_paletteSize > 0 && !m_busy;
    m_button->setEnabled(ready);

    if (!m_imageLoaded)
        m_status->setText(tr("Open an image to start"));
    else if (m_paletteSize == 0)
        m_status->setText(tr("The palette is empty: load or create one below"));
    else if (m_busy)
        m_status->setText(tr("Wait for the current operation to finish"));
    else
        m_status->setText(tr("Every pixel will be replaced with the nearest of %1 palette colors").arg(m_paletteSize));
}

} // namespace kolorit
