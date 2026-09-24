#include "ui/EffectParamsDialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace kolorit {

EffectParamsDialog::EffectParamsDialog(const QString& title, const QImage& source, QList<ParamSpec> specs,
                                       OpFactory factory, QWidget* parent)
    : PreviewDialog(source, parent)
    , m_specs(std::move(specs))
    , m_factory(std::move(factory))
{
    setWindowTitle(title);
    setMinimumWidth(460);

    m_description = new QLabel(this);
    m_description->setWordWrap(true);
    m_description->setVisible(false);

    auto* grid = new QGridLayout;
    grid->setColumnStretch(1, 1);

    for (int i = 0; i < m_specs.size(); ++i) {
        const ParamSpec& spec = m_specs[i];
        Control control;

        if (spec.isBool) {
            control.check = new QCheckBox(spec.label, this);
            control.check->setChecked(spec.value != 0);
            connect(control.check, &QCheckBox::toggled, this, [this] {
                emit valuesChanged();
                schedulePreview();
            });
            grid->addWidget(control.check, i, 0, 1, 3);
            m_controls.append(control);
            continue;
        }
        if (spec.isColor) {
            control.color = spec.color;
            control.colorButton = new QPushButton(this);
            control.colorButton->setCursor(Qt::PointingHandCursor);
            control.label = new QLabel(spec.label, this);
            grid->addWidget(control.label, i, 0);
            grid->addWidget(control.colorButton, i, 1, 1, 2);
            m_controls.append(control);
            updateColorButton(i);
            connect(control.colorButton, &QPushButton::clicked, this, [this, i] {
                const QColor chosen = QColorDialog::getColor(m_controls[i].color, this, m_specs[i].label);
                if (!chosen.isValid() || chosen == m_controls[i].color)
                    return;
                m_controls[i].color = chosen;
                updateColorButton(i);
                emit valuesChanged();
                schedulePreview();
            });
            continue;
        }
        if (spec.isChoice) {
            control.combo = new QComboBox(this);
            control.combo->addItems(spec.options);
            control.combo->setCurrentIndex(std::clamp(int(spec.value), 0, int(spec.options.size()) - 1));
            connect(control.combo, &QComboBox::currentIndexChanged, this, [this] {
                emit valuesChanged();
                schedulePreview();
            });
            control.label = new QLabel(spec.label, this);
            grid->addWidget(control.label, i, 0);
            grid->addWidget(control.combo, i, 1, 1, 2);
            m_controls.append(control);
            continue;
        }

        auto* label = new QLabel(spec.label, this);
        auto* slider = new QSlider(Qt::Horizontal, this);
        control.label = label;
        control.slider = slider;
        control.spin = new QDoubleSpinBox(this);
        QDoubleSpinBox* spin = control.spin;

        // The slider works in whole steps, the spin box in real units
        const int steps = int(std::lround((spec.max - spec.min) / spec.step));
        slider->setRange(0, steps);
        spin->setRange(spec.min, spec.max);
        spin->setSingleStep(spec.step);
        spin->setDecimals(spec.decimals);
        spin->setSuffix(spec.suffix);
        spin->setValue(spec.value);
        slider->setValue(int(std::lround((spec.value - spec.min) / spec.step)));

        connect(slider, &QSlider::valueChanged, spin, [spin, spec](int v) {
            const double value = spec.min + v * spec.step;
            if (std::fabs(spin->value() - value) > spec.step / 2)
                spin->setValue(value);
        });
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, slider, spec](double v) {
            const int pos = int(std::lround((v - spec.min) / spec.step));
            if (slider->value() != pos)
                slider->setValue(pos);
            emit valuesChanged();
            schedulePreview();
        });

        grid->addWidget(label, i, 0);
        grid->addWidget(slider, i, 1);
        grid->addWidget(spin, i, 2);
        m_controls.append(control);
    }

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_description);
    layout->addLayout(grid);
    layout->addStretch(1);
    layout->addWidget(buildFooter(tr("Apply")));
}

void EffectParamsDialog::setValues(const QVariantMap& values)
{
    m_applyingPreset = true;
    for (int i = 0; i < m_specs.size(); ++i) {
        const QVariant value = values.value(m_specs[i].key);
        if (!value.isValid())
            continue;
        Control& c = m_controls[i];
        if (c.check)
            c.check->setChecked(value.toBool());
        else if (c.combo)
            c.combo->setCurrentIndex(std::clamp(value.toInt(), 0, c.combo->count() - 1));
        else if (c.colorButton) {
            const QColor color = value.value<QColor>();
            if (color.isValid()) {
                c.color = color;
                updateColorButton(i);
            }
        } else if (c.spin)
            c.spin->setValue(value.toDouble());
    }
    m_applyingPreset = false;
    emit valuesChanged();
    schedulePreview();
}

void EffectParamsDialog::enablePresets(const QString& presetKey)
{
    if (m_presetRow)
        return;
    m_presetKey = presetKey;

    m_presetBox = new QComboBox(this);
    m_presetBox->setToolTip(tr("Saved settings of this effect"));
    m_presetSave = new QPushButton(tr("Save…"), this);
    m_presetSave->setToolTip(tr("Remember the current settings under a name"));
    m_presetDelete = new QPushButton(tr("Delete"), this);
    m_presetDelete->setToolTip(tr("Remove the selected preset"));

    m_presetRow = new QWidget(this);
    auto* row = new QHBoxLayout(m_presetRow);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(new QLabel(tr("Preset:"), m_presetRow));
    row->addWidget(m_presetBox, 1);
    row->addWidget(m_presetSave);
    row->addWidget(m_presetDelete);

    // The row goes first, above the description and the parameters
    if (auto* box = qobject_cast<QVBoxLayout*>(layout()))
        box->insertWidget(0, m_presetRow);

    connect(m_presetBox, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_applyingPreset || index <= 0)
            return;
        setValues(m_presetBox->itemData(index).toMap());
    });
    connect(m_presetSave, &QPushButton::clicked, this, &EffectParamsDialog::savePreset);
    connect(m_presetDelete, &QPushButton::clicked, this, &EffectParamsDialog::deletePreset);

    reloadPresets();
}

void EffectParamsDialog::reloadPresets(const QString& select)
{
    if (!m_presetBox)
        return;
    m_applyingPreset = true;
    m_presetBox->clear();
    m_presetBox->addItem(tr("— custom settings —"));

    QSettings settings;
    settings.beginGroup(QStringLiteral("presets/") + m_presetKey);
    const QStringList names = settings.childKeys();
    for (const QString& name : names)
        m_presetBox->addItem(name, settings.value(name).toMap());
    settings.endGroup();

    const int index = select.isEmpty() ? 0 : std::max(0, m_presetBox->findText(select));
    m_presetBox->setCurrentIndex(index);
    m_presetDelete->setEnabled(index > 0);
    m_applyingPreset = false;
}

void EffectParamsDialog::savePreset()
{
    bool ok = false;
    const QString suggested = m_presetBox->currentIndex() > 0 ? m_presetBox->currentText() : QString();
    const QString name =
        QInputDialog::getText(this, tr("Save preset"), tr("Preset name:"), QLineEdit::Normal, suggested, &ok)
            .trimmed();
    if (!ok || name.isEmpty())
        return;

    QSettings settings;
    settings.beginGroup(QStringLiteral("presets/") + m_presetKey);
    settings.setValue(name, values());
    settings.endGroup();
    reloadPresets(name);
    m_presetDelete->setEnabled(true);
}

void EffectParamsDialog::deletePreset()
{
    if (m_presetBox->currentIndex() <= 0)
        return;
    const QString name = m_presetBox->currentText();
    QSettings settings;
    settings.beginGroup(QStringLiteral("presets/") + m_presetKey);
    settings.remove(name);
    settings.endGroup();
    reloadPresets();
}

int EffectParamsDialog::indexOf(const QString& key) const
{
    for (int i = 0; i < m_specs.size(); ++i)
        if (m_specs[i].key == key)
            return i;
    return -1;
}

void EffectParamsDialog::setLabel(const QString& key, const QString& text)
{
    const int i = indexOf(key);
    if (i < 0)
        return;
    const Control& c = m_controls[i];
    if (c.check)
        c.check->setText(text);
    else if (c.label)
        c.label->setText(text);
}

void EffectParamsDialog::setControlEnabled(const QString& key, bool enabled)
{
    const int i = indexOf(key);
    if (i < 0)
        return;
    const Control& c = m_controls[i];
    for (QWidget* w : std::initializer_list<QWidget*>{c.label, c.slider, c.spin, c.check, c.combo, c.colorButton})
        if (w)
            w->setEnabled(enabled);
}

void EffectParamsDialog::updateColorButton(int index)
{
    const Control& c = m_controls[index];
    if (!c.colorButton)
        return;
    // The button is filled with the chosen color; the text is the hex of a contrasting color
    const bool dark = c.color.lightness() < 128;
    c.colorButton->setText(c.color.name().toUpper());
    c.colorButton->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid %3; }")
                                     .arg(c.color.name(), dark ? QStringLiteral("#ffffff") : QStringLiteral("#000000"),
                                          dark ? QStringLiteral("#666666") : QStringLiteral("#999999")));
}

void EffectParamsDialog::setDescription(const QString& text)
{
    m_description->setText(text);
    m_description->setVisible(!text.isEmpty());
}

QVariantMap EffectParamsDialog::values() const
{
    QVariantMap map;
    for (int i = 0; i < m_specs.size(); ++i) {
        const Control& c = m_controls[i];
        if (c.check)
            map.insert(m_specs[i].key, c.check->isChecked());
        else if (c.combo)
            map.insert(m_specs[i].key, c.combo->currentIndex());
        else if (c.colorButton)
            map.insert(m_specs[i].key, c.color);
        else
            map.insert(m_specs[i].key, c.spin->value());
    }
    return map;
}

ImageOp EffectParamsDialog::makeOp(double scale) const
{
    QVariantMap map;
    for (int i = 0; i < m_specs.size(); ++i) {
        const Control& c = m_controls[i];
        if (c.check) {
            map.insert(m_specs[i].key, c.check->isChecked());
            continue;
        }
        if (c.combo) {
            map.insert(m_specs[i].key, c.combo->currentIndex());
            continue;
        }
        if (c.colorButton) {
            map.insert(m_specs[i].key, c.color);
            continue;
        }
        double v = c.spin->value();
        if (m_specs[i].scalesWithImage)
            v = std::max(m_specs[i].min, std::round(v * scale));
        map.insert(m_specs[i].key, v);
    }
    return m_factory(map);
}

} // namespace kolorit
