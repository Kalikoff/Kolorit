#pragma once

#include <QGraphicsView>
#include <QImage>

class QGraphicsPixmapItem;
class QGraphicsScene;

namespace kolorit {

/// Image view with mouse-wheel zoom, panning
/// and a "fit to window" mode.
///
/// The scene is always measured in "logical" pixels, the size of the full image.
/// A downscaled preview frame can be shown with the logical size of the original:
/// it stretches over the same place and the zoom and position do not change.
///
/// Without an image it shows a hint on how to open one; during processing,
/// a progress label over the picture.
class ImageView : public QGraphicsView {
    Q_OBJECT
public:
    explicit ImageView(QWidget* parent = nullptr);

    /// Show an image. logicalSize is the size it represents
    /// (by default the image's own size).
    void setImage(const QImage& image, const QSize& logicalSize = QSize());
    void clear();
    bool hasImage() const { return !m_image.isNull(); }
    bool fitMode() const { return m_fit; }
    /// Zoom relative to the logical size (1.0 is pixel for pixel)
    double zoom() const;

    /// Label over the image (an empty string hides it)
    void setOverlayText(const QString& text);

public slots:
    void fitToWindow();
    void zoomActualSize();
    void zoomIn();
    void zoomOut();
    /// Set an exact zoom relative to the logical size
    void setZoom(double factor);

signals:
    void zoomChanged(double factor);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void applyZoom(double factor);
    void updateSmoothing();
    void updateBackground();
    void drawPlaceholder(QPainter& painter);
    void drawOverlay(QPainter& painter);

    QGraphicsScene* m_scene;
    QGraphicsPixmapItem* m_item;
    QImage m_image;
    QSize m_logicalSize;
    QString m_overlayText;
    bool m_fit = true;
};

} // namespace kolorit
