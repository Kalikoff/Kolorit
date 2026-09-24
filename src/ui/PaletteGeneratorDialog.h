#pragma once

#include "core/Palette.h"

#include <QColor>
#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;

namespace kolorit {

class PaletteStrip;

/// Palette generator: grayscale, tint of one color, ramp between colors,
/// levels per channel and a hue wheel.
///
/// All generators live in one window with a type switch, so in the menu of the
/// Palette panel they take one line instead of five. The set of fields follows the
/// chosen type, and the strip at the bottom shows the result right away.
class PaletteGeneratorDialog : public QDialog {
    Q_OBJECT
public:
    /// Generator type, remembered between runs
    enum Kind {
        Grayscale = 0,
        Tint,
        Ramp,
        Levels,
        HueWheel,
    };

    explicit PaletteGeneratorDialog(QWidget* parent = nullptr);

    /// Restore the state of the previous call
    void setState(int kind, int grayCount, bool grayPerceptual, const QColor& tintColor, int tintCount,
                  const QColor& rampFrom, const QColor& rampMid, const QColor& rampTo, bool rampUseMid,
                  int rampCount, int levels, int wheelCount, double wheelLightness, double wheelChroma);

    int kind() const;
    int grayCount() const;
    bool grayPerceptual() const;
    QColor tintColor() const { return m_tintColor; }
    int tintCount() const;
    QColor rampFrom() const { return m_rampFrom; }
    QColor rampMid() const { return m_rampMid; }
    QColor rampTo() const { return m_rampTo; }
    bool rampUseMid() const;
    int rampCount() const;
    int levels() const;
    int wheelCount() const;
    double wheelLightness() const;
    double wheelChroma() const;

    /// Palette for the current fields
    Palette palette() const;

private:
    /// A "slider + spin box" pair on one line, to save space
    struct CountRow {
        QSlider* slider = nullptr;
        QSpinBox* spin = nullptr;
    };
    CountRow addCountRow(QFormLayout* form, const QString& label, int min, int max, int value,
                         const QString& tip);
    QPushButton* makeColorButton(QColor* target);
    void updateColorButton(QPushButton* button, const QColor& color);
    void updateAll();

    QComboBox* m_kind;
    QStackedWidget* m_pages;
    QLabel* m_hint;
    PaletteStrip* m_strip;
    QLabel* m_countLabel;

    CountRow m_gray;
    QCheckBox* m_grayPerceptual;

    CountRow m_tint;
    QColor m_tintColor = QColor(0x8B, 0xAC, 0x0F);
    QPushButton* m_tintButton;

    CountRow m_ramp;
    QColor m_rampFrom = QColor(0x1B, 0x1F, 0x3B);
    QColor m_rampMid = QColor(0xC0, 0x39, 0x2B);
    QColor m_rampTo = QColor(0xF5, 0xE6, 0xC8);
    QPushButton* m_rampFromButton;
    QPushButton* m_rampMidButton;
    QPushButton* m_rampToButton;
    QCheckBox* m_rampUseMid;
    QLabel* m_rampMidLabel;

    QSpinBox* m_levels;

    CountRow m_wheel;
    QDoubleSpinBox* m_wheelLightness;
    QDoubleSpinBox* m_wheelChroma;
};

} // namespace kolorit
