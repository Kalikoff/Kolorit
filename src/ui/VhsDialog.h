#pragma once

#include "processing/VhsEffect.h"
#include "ui/PreviewDialog.h"

class QCheckBox;
class QComboBox;
class QDateTimeEdit;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace kolorit {

/// VHS camcorder effect dialog: aberration of the image and text layers, plus interference,
/// scanlines, grain, color bleed, vignette and on-screen text.
class VhsDialog : public PreviewDialog {
    Q_OBJECT
public:
    VhsDialog(const QImage& source, const VhsParams& initial, QWidget* parent = nullptr);

    VhsParams params() const;

protected:
    ImageOp makeOp(double scale) const override;

private:
    void applyParams(const VhsParams& p);
    void chooseTextColor();
    void randomizeSeed();
    void updateColorButton();

    QWidget* buildAberrationGroup();
    QWidget* buildNoiseGroup();
    QWidget* buildGradeGroup();
    QWidget* buildOsdGroup();
    QWidget* buildSeedGroup();

    // Image aberration
    QComboBox* m_imageLeft;
    QComboBox* m_imageRight;
    QSpinBox* m_imageOffset;
    QSpinBox* m_chromaBleed;
    // Interference
    QSpinBox* m_lineJitter;
    QCheckBox* m_tracking;
    QSpinBox* m_trackingPosition;
    QSpinBox* m_trackingHeight;
    QSpinBox* m_trackingShift;
    QCheckBox* m_scanlines;
    QSpinBox* m_scanlinePeriod;
    QSpinBox* m_scanlineThickness;
    QSpinBox* m_scanlineIntensity;
    QSpinBox* m_noise;
    // Color correction
    QSpinBox* m_brightness;
    QSpinBox* m_contrast;
    QSpinBox* m_saturation;
    QSpinBox* m_vignette;
    // Overlays
    QCheckBox* m_showTime;
    QCheckBox* m_showDate;
    QLineEdit* m_customText;
    QComboBox* m_label;
    QComboBox* m_corner;
    QDoubleSpinBox* m_fontSize;
    QPushButton* m_textColorButton;
    QComboBox* m_textLeft;
    QComboBox* m_textRight;
    QSpinBox* m_textOffset;
    QCheckBox* m_useCurrentTime;
    QDateTimeEdit* m_dateTime;
    // Randomness
    QSpinBox* m_seed;

    QColor m_textColor;
    bool m_updating = false;
};

} // namespace kolorit
