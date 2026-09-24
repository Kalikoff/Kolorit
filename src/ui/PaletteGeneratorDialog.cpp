#include "ui/PaletteGeneratorDialog.h"

#include "core/BuiltinPalettes.h"
#include "ui/PaletteStrip.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace kolorit {

namespace {

QString kindHint(int kind)
{
    switch (kind) {
    case PaletteGeneratorDialog::Grayscale:
        return QObject::tr("An even scale from black to white. 2 levels give a black-and-white image, "
                           "256 the full scale.");
    case PaletteGeneratorDialog::Tint:
        return QObject::tr("A ramp of one hue through the whole tonal range: lightness changes, saturation "
                           "falls off towards black and white. This is how you make your own \"Game Boy look\" in any color, "
                           "and unlike the hardware palette such a ramp does not collapse into two colors.");
    case PaletteGeneratorDialog::Ramp:
        return QObject::tr("A transition between colors through CIE Lab, smoother than mixing in RGB. "
                           "A ready ramp for a gradient map or a duotone.");
    case PaletteGeneratorDialog::Levels:
        return QObject::tr("All combinations of even per-channel levels. 3 levels give the Amstrad CPC palette, "
                           "4 give EGA, 6 give web-safe.");
    case PaletteGeneratorDialog::HueWheel:
        return QObject::tr("Hues around the circle at equal lightness and saturation, so none of them "
                           "sticks out. An even replacement for Rainbow, whose lightness wanders by 19 units.");
    }
    return {};
}

} // namespace

PaletteGeneratorDialog::PaletteGeneratorDialog(QWidget* parent)
    : QDialog(parent)
    , m_kind(new QComboBox(this))
    , m_pages(new QStackedWidget(this))
    , m_hint(new QLabel(this))
    , m_strip(new PaletteStrip(this))
    , m_countLabel(new QLabel(this))
{
    setWindowTitle(tr("Create palette"));
    setMinimumWidth(460);

    m_kind->addItem(tr("Grayscale"));
    m_kind->addItem(tr("Tint of one color"));
    m_kind->addItem(tr("Ramp between colors"));
    m_kind->addItem(tr("Levels per channel"));
    m_kind->addItem(tr("Hue wheel"));
    m_kind->setToolTip(tr("What to build"));

    m_hint->setWordWrap(true);
    m_hint->setObjectName(QStringLiteral("hintLabel"));

    // --- Grayscale ---
    {
        auto* page = new QWidget(this);
        auto* form = new QFormLayout(page);
        form->setContentsMargins(0, 0, 0, 0);
        m_gray = addCountRow(form, tr("Number of shades:"), 2, 256, 8,
                             tr("Number of levels from black to white"));
        m_grayPerceptual = new QCheckBox(tr("Visually even steps"), page);
        m_grayPerceptual->setToolTip(tr("An even step in lightness L* instead of an even step in bytes: "
                                        "the most visible step becomes 12–17 % shorter"));
        form->addRow(QString(), m_grayPerceptual);
        m_pages->addWidget(page);
    }

    // --- Tint of one color ---
    {
        auto* page = new QWidget(this);
        auto* form = new QFormLayout(page);
        form->setContentsMargins(0, 0, 0, 0);
        m_tintButton = makeColorButton(&m_tintColor);
        form->addRow(tr("Color:"), m_tintButton);
        m_tint = addCountRow(form, tr("Steps:"), 2, 256, 8, tr("Number of steps in the ramp"));
        m_pages->addWidget(page);
    }

    // --- Ramp ---
    {
        auto* page = new QWidget(this);
        auto* form = new QFormLayout(page);
        form->setContentsMargins(0, 0, 0, 0);
        m_rampFromButton = makeColorButton(&m_rampFrom);
        m_rampMidButton = makeColorButton(&m_rampMid);
        m_rampToButton = makeColorButton(&m_rampTo);
        form->addRow(tr("Shadows:"), m_rampFromButton);
        m_rampUseMid = new QCheckBox(tr("Through a middle color"), page);
        form->addRow(QString(), m_rampUseMid);
        m_rampMidLabel = new QLabel(tr("Midtones:"), page);
        form->addRow(m_rampMidLabel, m_rampMidButton);
        form->addRow(tr("Highlights:"), m_rampToButton);
        m_ramp = addCountRow(form, tr("Steps:"), 2, 256, 12, tr("Number of steps in the ramp"));
        m_pages->addWidget(page);
    }

    // --- Levels per channel ---
    {
        auto* page = new QWidget(this);
        auto* form = new QFormLayout(page);
        form->setContentsMargins(0, 0, 0, 0);
        m_levels = new QSpinBox(page);
        m_levels->setRange(2, 16);
        m_levels->setValue(4);
        m_levels->setToolTip(tr("How many values each channel takes; the number of colors is its cube"));
        form->addRow(tr("Levels per channel:"), m_levels);
        m_pages->addWidget(page);
    }

    // --- Hue wheel ---
    {
        auto* page = new QWidget(this);
        auto* form = new QFormLayout(page);
        form->setContentsMargins(0, 0, 0, 0);
        m_wheel = addCountRow(form, tr("Hues:"), 2, 256, 12, tr("How many hues to take around the circle"));
        m_wheelLightness = new QDoubleSpinBox(page);
        m_wheelLightness->setRange(0.0, 100.0);
        m_wheelLightness->setDecimals(0);
        m_wheelLightness->setValue(60.0);
        m_wheelLightness->setToolTip(tr("Lightness L*: 0 is black, 100 is white"));
        m_wheelChroma = new QDoubleSpinBox(page);
        m_wheelChroma->setRange(0.0, 128.0);
        m_wheelChroma->setDecimals(0);
        m_wheelChroma->setValue(45.0);
        m_wheelChroma->setToolTip(tr("Saturation: above 60 many hues no longer fit into sRGB"));
        auto* row = new QHBoxLayout;
        row->addWidget(m_wheelLightness, 1);
        row->addWidget(new QLabel(tr("saturation"), page));
        row->addWidget(m_wheelChroma, 1);
        form->addRow(tr("Lightness:"), row);
        m_pages->addWidget(page);
    }

    m_countLabel->setObjectName(QStringLiteral("hintLabel"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create palette"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Type:"), this));
    top->addWidget(m_kind, 1);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(m_pages);
    layout->addWidget(m_hint);
    layout->addWidget(m_strip);
    layout->addWidget(m_countLabel);
    layout->addStretch(1);
    layout->addWidget(buttons);

    connect(m_kind, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_pages->setCurrentIndex(index);
        updateAll();
    });
    connect(m_grayPerceptual, &QCheckBox::toggled, this, &PaletteGeneratorDialog::updateAll);
    connect(m_rampUseMid, &QCheckBox::toggled, this, &PaletteGeneratorDialog::updateAll);
    connect(m_levels, &QSpinBox::valueChanged, this, &PaletteGeneratorDialog::updateAll);
    connect(m_wheelLightness, &QDoubleSpinBox::valueChanged, this, &PaletteGeneratorDialog::updateAll);
    connect(m_wheelChroma, &QDoubleSpinBox::valueChanged, this, &PaletteGeneratorDialog::updateAll);

    updateAll();
}

PaletteGeneratorDialog::CountRow PaletteGeneratorDialog::addCountRow(QFormLayout* form, const QString& label,
                                                                     int min, int max, int value,
                                                                     const QString& tip)
{
    CountRow row;
    row.slider = new QSlider(Qt::Horizontal, form->parentWidget());
    row.spin = new QSpinBox(form->parentWidget());
    row.slider->setRange(min, max);
    row.spin->setRange(min, max);
    row.slider->setValue(value);
    row.spin->setValue(value);
    row.spin->setToolTip(tip);
    row.slider->setToolTip(tip);

    auto* box = new QHBoxLayout;
    box->addWidget(row.slider, 1);
    box->addWidget(row.spin);
    form->addRow(label, box);

    connect(row.slider, &QSlider::valueChanged, row.spin, &QSpinBox::setValue);
    connect(row.spin, &QSpinBox::valueChanged, row.slider, &QSlider::setValue);
    connect(row.spin, &QSpinBox::valueChanged, this, &PaletteGeneratorDialog::updateAll);
    return row;
}

QPushButton* PaletteGeneratorDialog::makeColorButton(QColor* target)
{
    auto* button = new QPushButton(this);
    button->setCursor(Qt::PointingHandCursor);
    updateColorButton(button, *target);
    connect(button, &QPushButton::clicked, this, [this, button, target] {
        const QColor chosen = QColorDialog::getColor(*target, this, tr("Choose a color"));
        if (!chosen.isValid() || chosen == *target)
            return;
        *target = chosen;
        updateColorButton(button, *target);
        updateAll();
    });
    return button;
}

void PaletteGeneratorDialog::updateColorButton(QPushButton* button, const QColor& color)
{
    // The button is filled with the chosen color; the label is the hex in a contrasting color
    const bool dark = color.lightness() < 128;
    button->setText(color.name().toUpper());
    button->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid %3; }")
                              .arg(color.name(), dark ? QStringLiteral("#ffffff") : QStringLiteral("#000000"),
                                   dark ? QStringLiteral("#666666") : QStringLiteral("#999999")));
}

void PaletteGeneratorDialog::updateAll()
{
    const bool useMid = m_rampUseMid->isChecked();
    m_rampMidLabel->setVisible(useMid);
    m_rampMidButton->setVisible(useMid);

    m_hint->setText(kindHint(m_kind->currentIndex()));
    const Palette result = palette();
    m_strip->setColors(result);
    m_countLabel->setText(tr("Colors: %1").arg(result.size()));
}

int PaletteGeneratorDialog::kind() const { return m_kind->currentIndex(); }
int PaletteGeneratorDialog::grayCount() const { return m_gray.spin->value(); }
bool PaletteGeneratorDialog::grayPerceptual() const { return m_grayPerceptual->isChecked(); }
int PaletteGeneratorDialog::tintCount() const { return m_tint.spin->value(); }
bool PaletteGeneratorDialog::rampUseMid() const { return m_rampUseMid->isChecked(); }
int PaletteGeneratorDialog::rampCount() const { return m_ramp.spin->value(); }
int PaletteGeneratorDialog::levels() const { return m_levels->value(); }
int PaletteGeneratorDialog::wheelCount() const { return m_wheel.spin->value(); }
double PaletteGeneratorDialog::wheelLightness() const { return m_wheelLightness->value(); }
double PaletteGeneratorDialog::wheelChroma() const { return m_wheelChroma->value(); }

void PaletteGeneratorDialog::setState(int kind, int grayCount, bool grayPerceptual, const QColor& tintColor,
                                      int tintCount, const QColor& rampFrom, const QColor& rampMid,
                                      const QColor& rampTo, bool rampUseMid, int rampCount, int levels,
                                      int wheelCount, double wheelLightness, double wheelChroma)
{
    m_kind->setCurrentIndex(std::clamp(kind, 0, m_kind->count() - 1));
    m_gray.spin->setValue(std::clamp(grayCount, 2, 256));
    m_grayPerceptual->setChecked(grayPerceptual);
    if (tintColor.isValid())
        m_tintColor = tintColor;
    m_tint.spin->setValue(std::clamp(tintCount, 2, 256));
    if (rampFrom.isValid())
        m_rampFrom = rampFrom;
    if (rampMid.isValid())
        m_rampMid = rampMid;
    if (rampTo.isValid())
        m_rampTo = rampTo;
    m_rampUseMid->setChecked(rampUseMid);
    m_ramp.spin->setValue(std::clamp(rampCount, 2, 256));
    m_levels->setValue(std::clamp(levels, 2, 16));
    m_wheel.spin->setValue(std::clamp(wheelCount, 2, 256));
    m_wheelLightness->setValue(std::clamp(wheelLightness, 0.0, 100.0));
    m_wheelChroma->setValue(std::clamp(wheelChroma, 0.0, 128.0));

    updateColorButton(m_tintButton, m_tintColor);
    updateColorButton(m_rampFromButton, m_rampFrom);
    updateColorButton(m_rampMidButton, m_rampMid);
    updateColorButton(m_rampToButton, m_rampTo);
    updateAll();
}

Palette PaletteGeneratorDialog::palette() const
{
    switch (m_kind->currentIndex()) {
    case Grayscale:
        return builtin::grayscale(grayCount(), grayPerceptual());
    case Tint:
        return builtin::tint(m_tintColor.rgb(), tintCount());
    case Ramp:
        return rampUseMid() ? builtin::ramp(m_rampFrom.rgb(), m_rampMid.rgb(), m_rampTo.rgb(), rampCount())
                            : builtin::ramp(m_rampFrom.rgb(), m_rampTo.rgb(), rampCount());
    case Levels:
        return builtin::levels(levels());
    case HueWheel:
        return builtin::hueWheel(wheelCount(), wheelLightness(), wheelChroma());
    }
    return {};
}

} // namespace kolorit
