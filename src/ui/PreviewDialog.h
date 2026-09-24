#pragma once

#include "core/ImageTask.h"
#include "core/ImageTaskRunner.h"

#include <QDialog>
#include <QImage>
#include <QTimer>

class QLabel;
class QProgressBar;
class QDialogButtonBox;

namespace kolorit {

/// Base modeless parameter dialog with a live preview.
///
/// The preview works in two stages. On every parameter change a frame is quickly computed
/// on a downscaled copy (at most kPreviewSize on the longer side), and when the user
/// pauses, the same effect is computed at full resolution so that zooming in the main
/// window shows real detail. The finished full-size frame is handed over through
/// takeFullResult() and used by the "Apply" button without recomputation.
///
/// An unfinished computation is cancelled on the next parameter change.
/// A subclass implements makeOp(scale), the operation with parameters scaled accordingly.
class PreviewDialog : public QDialog {
    Q_OBJECT
public:
    static constexpr int kPreviewSize = 900;
    static constexpr int kFullPreviewDelayMs = 350;

    explicit PreviewDialog(const QImage& source, QWidget* parent = nullptr);

    /// Operation for the full-size image (after the dialog is accepted)
    ImageOp finalOp() const { return makeOp(1.0); }

    /// The full-size result for the current parameters if it is already computed (otherwise null).
    /// After the call the result counts as taken.
    QImage takeFullResult();
    qint64 fullResultElapsedMs() const { return m_fullElapsedMs; }

    /// How many times the copy for the fast preview is downscaled (1.0 means not at all)
    double previewScale() const { return m_scale; }

signals:
    /// Another preview frame is ready. fullResolution means it was computed on the full image.
    void previewReady(const QImage& image, bool fullResolution);

protected:
    virtual ImageOp makeOp(double scale) const = 0;

    /// Call on any parameter change
    void schedulePreview();

    /// Bottom part of the dialog: preview progress and the OK/Cancel buttons
    QWidget* buildFooter(const QString& acceptText);

    void showEvent(QShowEvent* event) override;

    const QImage& previewSource() const { return m_preview; }
    QSize sourceSize() const { return m_source.size(); }

private:
    void runPreview();
    void runFullPreview();
    void onFinished(const QImage& image, qint64 ms);

    QImage m_source;    ///< the full image (implicitly shared copy)
    QImage m_preview;   ///< downscaled copy for the fast preview
    double m_scale = 1.0;

    QTimer m_debounce;
    QTimer m_fullDelay;
    ImageTaskRunner m_runner;

    quint64 m_generation = 0;          ///< number of the current parameter set
    quint64 m_runningGeneration = 0;   ///< parameter set of the running computation
    bool m_runningFull = false;        ///< the running computation is full size

    QImage m_fullResult;
    quint64 m_fullGeneration = 0;
    bool m_hasFullResult = false;
    qint64 m_fullElapsedMs = 0;

    QProgressBar* m_progress = nullptr;
    QLabel* m_status = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
};

} // namespace kolorit
