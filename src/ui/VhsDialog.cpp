#include "ui/VhsDialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

namespace kolorit {

namespace {

QComboBox* channelCombo(QWidget* parent)
{
    auto* combo = new QComboBox(parent);
    for (Channel c : allChannels())
        combo->addItem(channelName(c), int(c));
    return combo;
}

QSpinBox* spin(QWidget* parent, int min, int max, const QString& suffix = QString())
{
    auto* s = new QSpinBox(parent);
    s->setRange(min, max);
    s->setSuffix(suffix);
    return s;
}

Channel channelOf(const QComboBox* combo)
{
    return Channel(combo->currentData().toInt());
}

void selectChannel(QComboBox* combo, Channel c)
{
    combo->setCurrentIndex(combo->findData(int(c)));
}

} // namespace

VhsDialog::VhsDialog(const QImage& source, const VhsParams& initial, QWidget* parent)
    : PreviewDialog(source, parent)
{
    setWindowTitle(tr("VHS camcorder effect"));

    // Two columns of groups, each aligned to the top
    auto* leftColumn = new QVBoxLayout;
    leftColumn->addWidget(buildAberrationGroup());
    leftColumn->addWidget(buildGradeGroup());
    leftColumn->addWidget(buildSeedGroup());
    leftColumn->addStretch(1);

    auto* rightColumn = new QVBoxLayout;
    rightColumn->addWidget(buildNoiseGroup());
    rightColumn->addWidget(buildOsdGroup());
    rightColumn->addStretch(1);

    auto* grid = new QHBoxLayout;
    grid->addLayout(leftColumn, 1);
    grid->addLayout(rightColumn, 1);

    auto* resetButton = new QPushButton(tr("Reset to defaults"), this);
    connect(resetButton, &QPushButton::clicked, this, [this] { applyParams(VhsParams()); });

    auto* footerRow = new QHBoxLayout;
    footerRow->addWidget(resetButton);
    footerRow->addWidget(buildFooter(tr("Apply")), 1);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(grid);
    layout->addLayout(footerRow);

    applyParams(initial);

    // Any parameter change restarts the preview
    const auto watch = [this](auto* widget, auto signal) {
        connect(widget, signal, this, [this] {
            if (!m_updating)
                schedulePreview();
        });
    };
    for (QSpinBox* s : {m_imageOffset, m_chromaBleed, m_lineJitter, m_trackingPosition, m_trackingHeight,
                        m_trackingShift, m_scanlinePeriod, m_scanlineThickness, m_scanlineIntensity, m_noise,
                        m_brightness, m_contrast, m_saturation, m_vignette, m_textOffset, m_seed})
        watch(s, &QSpinBox::valueChanged);
    for (QComboBox* c : {m_imageLeft, m_imageRight, m_label, m_corner, m_textLeft, m_textRight})
        watch(c, &QComboBox::currentIndexChanged);
    for (QCheckBox* c : {m_tracking, m_scanlines, m_showTime, m_showDate, m_useCurrentTime})
        watch(c, &QCheckBox::toggled);
    watch(m_customText, &QLineEdit::textChanged);
    watch(m_fontSize, &QDoubleSpinBox::valueChanged);
    watch(m_dateTime, &QDateTimeEdit::dateTimeChanged);

    connect(m_useCurrentTime, &QCheckBox::toggled, m_dateTime, &QWidget::setDisabled);
    connect(m_tracking, &QCheckBox::toggled, this, [this](bool on) {
        m_trackingPosition->setEnabled(on);
        m_trackingHeight->setEnabled(on);
        m_trackingShift->setEnabled(on);
    });
    connect(m_scanlines, &QCheckBox::toggled, this, [this](bool on) {
        m_scanlinePeriod->setEnabled(on);
        m_scanlineThickness->setEnabled(on);
        m_scanlineIntensity->setEnabled(on);
    });
    m_dateTime->setDisabled(m_useCurrentTime->isChecked());
    emit m_tracking->toggled(m_tracking->isChecked());
    emit m_scanlines->toggled(m_scanlines->isChecked());
}

QWidget* VhsDialog::buildAberrationGroup()
{
    auto* group = new QGroupBox(tr("Image: chromatic aberration"), this);
    m_imageLeft = channelCombo(group);
    m_imageRight = channelCombo(group);
    m_imageOffset = spin(group, 0, 200, tr(" px"));
    m_chromaBleed = spin(group, 0, 64, tr(" px"));

    auto* form = new QFormLayout(group);
    form->addRow(tr("Left layer (filter):"), m_imageLeft);
    form->addRow(tr("Right layer (filter):"), m_imageRight);
    form->addRow(tr("Layer offset:"), m_imageOffset);
    form->addRow(tr("Color bleed:"), m_chromaBleed);
    return group;
}

QWidget* VhsDialog::buildNoiseGroup()
{
    auto* group = new QGroupBox(tr("Interference"), this);
    m_lineJitter = spin(group, 0, 50, tr(" px"));
    m_tracking = new QCheckBox(tr("Tracking band"), group);
    m_trackingPosition = spin(group, 0, 100, tr(" %"));
    m_trackingHeight = spin(group, 1, 500, tr(" px"));
    m_trackingShift = spin(group, 0, 300, tr(" px"));
    m_scanlines = new QCheckBox(tr("Scanlines"), group);
    m_scanlinePeriod = spin(group, 1, 32, tr(" px"));
    m_scanlineThickness = spin(group, 1, 32, tr(" px"));
    m_scanlineIntensity = spin(group, 0, 100, tr(" %"));
    m_noise = spin(group, 0, 100, tr(" %"));

    auto* form = new QFormLayout(group);
    form->addRow(tr("Line jitter:"), m_lineJitter);
    form->addRow(tr("Grain (noise):"), m_noise);
    form->addRow(m_tracking);
    form->addRow(tr("   Position (from top):"), m_trackingPosition);
    form->addRow(tr("   Band height:"), m_trackingHeight);
    form->addRow(tr("   Shift in band:"), m_trackingShift);
    form->addRow(m_scanlines);
    form->addRow(tr("   Period:"), m_scanlinePeriod);
    form->addRow(tr("   Thickness:"), m_scanlineThickness);
    form->addRow(tr("   Darkening:"), m_scanlineIntensity);
    return group;
}

QWidget* VhsDialog::buildGradeGroup()
{
    auto* group = new QGroupBox(tr("Color correction"), this);
    m_brightness = spin(group, -100, 100);
    m_contrast = spin(group, -100, 100);
    m_saturation = spin(group, -100, 100);
    m_vignette = spin(group, 0, 100, tr(" %"));

    auto* form = new QFormLayout(group);
    form->addRow(tr("Brightness:"), m_brightness);
    form->addRow(tr("Contrast:"), m_contrast);
    form->addRow(tr("Saturation:"), m_saturation);
    form->addRow(tr("Vignette:"), m_vignette);
    return group;
}

QWidget* VhsDialog::buildOsdGroup()
{
    auto* group = new QGroupBox(tr("Text (on-screen display)"), this);
    m_showTime = new QCheckBox(tr("Time"), group);
    m_showDate = new QCheckBox(tr("Date"), group);
    m_customText = new QLineEdit(group);
    m_customText->setPlaceholderText(tr("Custom text (optional)"));
    m_label = new QComboBox(group);
    for (OsdLabel l : {OsdLabel::None, OsdLabel::Play, OsdLabel::Rec, OsdLabel::Pause})
        m_label->addItem(osdLabelName(l), int(l));
    m_corner = new QComboBox(group);
    for (OsdCorner c : {OsdCorner::TopLeft, OsdCorner::TopRight, OsdCorner::BottomLeft, OsdCorner::BottomRight})
        m_corner->addItem(osdCornerName(c), int(c));
    m_fontSize = new QDoubleSpinBox(group);
    m_fontSize->setRange(1.0, 25.0);
    m_fontSize->setDecimals(1);
    m_fontSize->setSingleStep(0.5);
    m_fontSize->setSuffix(tr(" % of height"));
    m_textColorButton = new QPushButton(group);
    connect(m_textColorButton, &QPushButton::clicked, this, &VhsDialog::chooseTextColor);
    m_textLeft = channelCombo(group);
    m_textRight = channelCombo(group);
    m_textOffset = spin(group, 0, 50, tr(" px"));
    m_useCurrentTime = new QCheckBox(tr("Current date and time"), group);
    m_dateTime = new QDateTimeEdit(group);
    m_dateTime->setDisplayFormat(QStringLiteral("dd.MM.yyyy HH:mm"));
    m_dateTime->setCalendarPopup(true);

    auto* checks = new QHBoxLayout;
    checks->addWidget(m_showTime);
    checks->addWidget(m_showDate);
    checks->addStretch(1);

    auto* form = new QFormLayout(group);
    form->addRow(tr("Show:"), checks);
    form->addRow(tr("Text:"), m_customText);
    form->addRow(tr("Indicator:"), m_label);
    form->addRow(tr("Position:"), m_corner);
    form->addRow(tr("Font size:"), m_fontSize);
    form->addRow(tr("Text color:"), m_textColorButton);
    form->addRow(tr("Left layer (filter):"), m_textLeft);
    form->addRow(tr("Right layer (filter):"), m_textRight);
    form->addRow(tr("Layer offset:"), m_textOffset);
    form->addRow(m_useCurrentTime);
    form->addRow(tr("Date and time:"), m_dateTime);
    return group;
}

QWidget* VhsDialog::buildSeedGroup()
{
    auto* group = new QGroupBox(tr("Randomness"), this);
    m_seed = spin(group, 0, 999999);
    auto* randomButton = new QPushButton(tr("Randomize"), group);
    connect(randomButton, &QPushButton::clicked, this, &VhsDialog::randomizeSeed);

    auto* row = new QHBoxLayout(group);
    row->addWidget(m_seed, 1);
    row->addWidget(randomButton);
    return group;
}

void VhsDialog::applyParams(const VhsParams& p)
{
    m_updating = true;
    selectChannel(m_imageLeft, p.imageLeft);
    selectChannel(m_imageRight, p.imageRight);
    m_imageOffset->setValue(p.imageOffset);
    m_chromaBleed->setValue(p.chromaBleed);
    m_lineJitter->setValue(p.lineJitter);
    m_tracking->setChecked(p.tracking);
    m_trackingPosition->setValue(int(std::lround(p.trackingPosition * 100.0)));
    m_trackingHeight->setValue(p.trackingHeight);
    m_trackingShift->setValue(p.trackingShift);
    m_scanlines->setChecked(p.scanlines);
    m_scanlinePeriod->setValue(p.scanlinePeriod);
    m_scanlineThickness->setValue(p.scanlineThickness);
    m_scanlineIntensity->setValue(int(std::lround(p.scanlineIntensity * 100.0)));
    m_noise->setValue(int(std::lround(p.noise * 100.0)));
    m_brightness->setValue(int(std::lround(p.brightness)));
    m_contrast->setValue(int(std::lround(p.contrast)));
    m_saturation->setValue(int(std::lround(p.saturation)));
    m_vignette->setValue(int(std::lround(p.vignette * 100.0)));
    m_showTime->setChecked(p.showTime);
    m_showDate->setChecked(p.showDate);
    m_customText->setText(p.customText);
    m_label->setCurrentIndex(m_label->findData(int(p.label)));
    m_corner->setCurrentIndex(m_corner->findData(int(p.corner)));
    m_fontSize->setValue(p.fontSizePercent);
    m_textColor = p.textColor;
    updateColorButton();
    selectChannel(m_textLeft, p.textLeft);
    selectChannel(m_textRight, p.textRight);
    m_textOffset->setValue(p.textOffset);
    m_useCurrentTime->setChecked(p.useCurrentTime);
    m_dateTime->setDateTime(p.dateTime);
    m_seed->setValue(int(p.seed % 1000000u));
    m_updating = false;
    schedulePreview();
}

VhsParams VhsDialog::params() const
{
    VhsParams p;
    p.imageLeft = channelOf(m_imageLeft);
    p.imageRight = channelOf(m_imageRight);
    p.imageOffset = m_imageOffset->value();
    p.chromaBleed = m_chromaBleed->value();
    p.lineJitter = m_lineJitter->value();
    p.tracking = m_tracking->isChecked();
    p.trackingPosition = m_trackingPosition->value() / 100.0;
    p.trackingHeight = m_trackingHeight->value();
    p.trackingShift = m_trackingShift->value();
    p.scanlines = m_scanlines->isChecked();
    p.scanlinePeriod = m_scanlinePeriod->value();
    p.scanlineThickness = m_scanlineThickness->value();
    p.scanlineIntensity = m_scanlineIntensity->value() / 100.0;
    p.noise = m_noise->value() / 100.0;
    p.brightness = m_brightness->value();
    p.contrast = m_contrast->value();
    p.saturation = m_saturation->value();
    p.vignette = m_vignette->value() / 100.0;
    p.showTime = m_showTime->isChecked();
    p.showDate = m_showDate->isChecked();
    p.customText = m_customText->text();
    p.label = OsdLabel(m_label->currentData().toInt());
    p.corner = OsdCorner(m_corner->currentData().toInt());
    p.fontSizePercent = m_fontSize->value();
    p.textColor = m_textColor;
    p.textLeft = channelOf(m_textLeft);
    p.textRight = channelOf(m_textRight);
    p.textOffset = m_textOffset->value();
    p.useCurrentTime = m_useCurrentTime->isChecked();
    p.dateTime = m_dateTime->dateTime();
    p.seed = uint32_t(m_seed->value());
    return p;
}

ImageOp VhsDialog::makeOp(double scale) const
{
    const VhsParams scaled = params().scaled(scale);
    // The overlay layer is drawn on the main thread and must match the size of the image
    // the operation is applied to (the preview or the full size)
    const QSize size = scale == 1.0 ? sourceSize() : previewSource().size();
    return vhs::makeOp(scaled, vhs::renderOsd(scaled, size));
}

void VhsDialog::chooseTextColor()
{
    const QColor color = QColorDialog::getColor(m_textColor, this, tr("Text color"));
    if (!color.isValid())
        return;
    m_textColor = color;
    updateColorButton();
    schedulePreview();
}

void VhsDialog::updateColorButton()
{
    m_textColorButton->setText(m_textColor.name().toUpper());
    const bool dark = m_textColor.lightness() < 128;
    m_textColorButton->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: %2; }")
                                         .arg(m_textColor.name(), dark ? QStringLiteral("#ffffff") : QStringLiteral("#000000")));
}

void VhsDialog::randomizeSeed()
{
    m_seed->setValue(int(QRandomGenerator::global()->bounded(1, 1000000)));
}

} // namespace kolorit
