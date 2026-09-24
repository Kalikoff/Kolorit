#pragma once

#include "ui/PreviewDialog.h"

#include <QColor>
#include <QList>
#include <QString>
#include <QVariantMap>

#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;

namespace kolorit {

/// Description of one effect parameter: a number (slider + spin box), a checkbox, a list choice or a color
struct ParamSpec {
    QString key;
    QString label;
    double min = 0;
    double max = 100;
    double value = 0;
    double step = 1;
    int decimals = 0;
    bool scalesWithImage = false;   ///< a parameter in pixels: scaled for the preview
    QString suffix;
    bool isBool = false;            ///< a checkbox; value != 0 means checked
    QStringList options;            ///< drop-down options; value is the index of the chosen one
    bool isChoice = false;
    QColor color;                   ///< initial color (isColor)
    bool isColor = false;

    static ParamSpec checkbox(const QString& key, const QString& label, bool checked)
    {
        ParamSpec spec;
        spec.key = key;
        spec.label = label;
        spec.value = checked ? 1 : 0;
        spec.isBool = true;
        return spec;
    }

    static ParamSpec colorPick(const QString& key, const QString& label, const QColor& color)
    {
        ParamSpec spec;
        spec.key = key;
        spec.label = label;
        spec.color = color;
        spec.isColor = true;
        return spec;
    }

    static ParamSpec choice(const QString& key, const QString& label, const QStringList& options, int index)
    {
        ParamSpec spec;
        spec.key = key;
        spec.label = label;
        spec.options = options;
        spec.value = index;
        spec.isChoice = true;
        return spec;
    }
};

/// Factory of the operation from the parameter values (already scaled for the preview)
using OpFactory = std::function<ImageOp(const QVariantMap& values)>;

/// Generic parameter dialog with a live preview:
/// posterize, pixelate, color correction, artifact removal, pixel art and so on.
class EffectParamsDialog : public PreviewDialog {
    Q_OBJECT
public:
    EffectParamsDialog(const QString& title, const QImage& source, QList<ParamSpec> specs, OpFactory factory,
                       QWidget* parent = nullptr);

    /// Enable the preset row. presetKey is the settings section, one per effect.
    ///
    /// A set of settings is saved under a name and restored with a single choice from the list.
    /// The row takes exactly one line above the parameters: the list, "Save", "Delete".
    void enablePresets(const QString& presetKey);

    /// Description shown above the parameters
    void setDescription(const QString& text);

    /// Current values without scaling
    QVariantMap values() const;

    /// Set values (keys that are not among the parameters are ignored)
    void setValues(const QVariantMap& values);

    /// Change the label of a parameter (for a checkbox, its text)
    void setLabel(const QString& key, const QString& text);
    /// Enable or disable a parameter (for example, when another one overrides it)
    void setControlEnabled(const QString& key, bool enabled);

public slots:
    /// Recompute the preview when external data changed (palette, color model)
    void refreshPreview() { schedulePreview(); }

signals:
    /// The user changed any parameter
    void valuesChanged();

protected:
    ImageOp makeOp(double scale) const override;

private:
    struct Control {
        QLabel* label = nullptr;
        QSlider* slider = nullptr;
        QDoubleSpinBox* spin = nullptr;
        QCheckBox* check = nullptr;
        QComboBox* combo = nullptr;
        QPushButton* colorButton = nullptr;
        QColor color;
    };

    void updateColorButton(int index);
    void reloadPresets(const QString& select = QString());
    void savePreset();
    void deletePreset();

    int indexOf(const QString& key) const;

    QList<ParamSpec> m_specs;
    QList<Control> m_controls;   // indices match m_specs
    OpFactory m_factory;
    QLabel* m_description = nullptr;

    QString m_presetKey;
    QWidget* m_presetRow = nullptr;
    QComboBox* m_presetBox = nullptr;
    QPushButton* m_presetSave = nullptr;
    QPushButton* m_presetDelete = nullptr;
    bool m_applyingPreset = false;
};

} // namespace kolorit
