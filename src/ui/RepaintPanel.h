#pragma once

#include "processing/PaletteMapper.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;

namespace kolorit {

/// Repaint panel: color model, dithering and the primary action button.
/// Gathers everything the application's main operation needs in one place, with explanations.
class RepaintPanel : public QWidget {
    Q_OBJECT
public:
    explicit RepaintPanel(QWidget* parent = nullptr);

    RepaintParams params() const;
    void setParams(const RepaintParams& params);

    /// Number of colors in the palette: the label under the button and whether it is enabled
    void setPaletteSize(int count);
    /// Whether an image is loaded
    void setImageLoaded(bool loaded);
    /// Processing or effect setup is in progress: the button is disabled
    void setBusy(bool busy);

    void refreshIcons();

signals:
    void repaintRequested();
    void paramsChanged();

private:
    void updateState();

    QComboBox* m_model;
    QComboBox* m_dither;
    QSlider* m_strengthSlider;
    QSpinBox* m_strength;
    QLabel* m_strengthLabel;
    QPushButton* m_button;
    QLabel* m_status;
    int m_paletteSize = 0;
    bool m_imageLoaded = false;
    bool m_busy = false;
};

} // namespace kolorit
