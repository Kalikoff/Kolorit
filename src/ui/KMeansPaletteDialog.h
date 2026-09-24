#pragma once

#include "core/ImageTaskRunner.h"
#include "core/Palette.h"

#include <QDialog>
#include <QImage>

class QProgressBar;
class QPushButton;
class QSpinBox;
class QLabel;

namespace kolorit {

class PaletteStrip;

/// k-means palette: the k characteristic colors of an image.
/// Clustering runs in the background (rows in parallel) with progress and cancellation.
class KMeansPaletteDialog : public QDialog {
    Q_OBJECT
public:
    explicit KMeansPaletteDialog(const QImage& source, QWidget* parent = nullptr);

    const Palette& palette() const { return m_palette; }

protected:
    void reject() override;

private:
    void compute();
    void onFinished(const QImage& paletteImage, qint64 ms);

    QImage m_source;
    Palette m_palette;
    ImageTaskRunner m_runner;
    QSpinBox* m_count;
    QSpinBox* m_iterations;
    QSpinBox* m_seed;
    QPushButton* m_computeButton;
    QPushButton* m_okButton;
    QProgressBar* m_progress;
    QLabel* m_status;
    PaletteStrip* m_strip;
};

} // namespace kolorit
