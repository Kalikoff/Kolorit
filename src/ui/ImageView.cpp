#include "ui/ImageView.h"

#include "ui/Theme.h"

#include <QEvent>
#include <QFontMetrics>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace kolorit {

namespace {

constexpr double kZoomStep = 1.2;
constexpr double kMinZoom = 0.02;
constexpr double kMaxZoom = 64.0;

/// Checkerboard background matching the theme, so transparency is visible
QBrush checkerBrush(const QColor& window)
{
    const bool dark = window.lightness() < 128;
    const QColor a = dark ? QColor(44, 45, 49) : QColor(214, 216, 220);
    const QColor b = dark ? QColor(52, 53, 58) : QColor(232, 234, 238);
    QPixmap tile(16, 16);
    tile.fill(a);
    QPainter p(&tile);
    p.fillRect(0, 0, 8, 8, b);
    p.fillRect(8, 8, 8, 8, b);
    return QBrush(tile);
}

} // namespace

ImageView::ImageView(QWidget* parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
    , m_item(m_scene->addPixmap(QPixmap()))
{
    setScene(m_scene);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setFrameShape(QFrame::NoFrame);
    // Dropped files are handled by the main window
    setAcceptDrops(false);
    updateBackground();
}

void ImageView::setImage(const QImage& image, const QSize& logicalSize)
{
    QSize logical = logicalSize.isValid() ? logicalSize : image.size();
    // A frame of the same logical size (a preview, say) keeps the zoom and position;
    // a difference of a couple of pixels from rounding a downscaled copy counts as the same size
    const bool sameFraming = hasImage() && std::abs(m_logicalSize.width() - logical.width()) <= 2 &&
                             std::abs(m_logicalSize.height() - logical.height()) <= 2;
    if (sameFraming)
        logical = m_logicalSize;

    m_image = image;
    m_logicalSize = logical;
    m_item->setPixmap(QPixmap::fromImage(image));
    m_item->setScale(image.width() > 0 ? double(logical.width()) / image.width() : 1.0);
    m_scene->setSceneRect(QRectF(QPointF(0, 0), QSizeF(logical)));

    if (m_fit || !sameFraming)
        fitToWindow();
    else
        updateSmoothing();
    viewport()->update();
}

void ImageView::clear()
{
    m_image = QImage();
    m_logicalSize = QSize();
    m_item->setPixmap(QPixmap());
    m_item->setScale(1.0);
    m_scene->setSceneRect(QRectF());
    m_fit = true;
    resetTransform();
    viewport()->update();
    emit zoomChanged(1.0);
}

double ImageView::zoom() const
{
    return transform().m11();
}

void ImageView::setOverlayText(const QString& text)
{
    if (m_overlayText == text)
        return;
    m_overlayText = text;
    viewport()->update();
}

void ImageView::fitToWindow()
{
    m_fit = true;
    if (!hasImage())
        return;
    fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
    updateSmoothing();
    emit zoomChanged(zoom());
}

void ImageView::zoomActualSize()
{
    m_fit = false;
    resetTransform();
    updateSmoothing();
    emit zoomChanged(1.0);
}

void ImageView::zoomIn()
{
    applyZoom(kZoomStep);
}

void ImageView::zoomOut()
{
    applyZoom(1.0 / kZoomStep);
}

void ImageView::setZoom(double factor)
{
    if (!hasImage())
        return;
    m_fit = false;
    resetTransform();
    const double target = std::clamp(factor, kMinZoom, kMaxZoom);
    scale(target, target);
    updateSmoothing();
    emit zoomChanged(zoom());
}

void ImageView::applyZoom(double factor)
{
    if (!hasImage())
        return;
    const double target = std::clamp(zoom() * factor, kMinZoom, kMaxZoom);
    factor = target / zoom();
    if (std::fabs(factor - 1.0) < 1e-6)
        return;
    m_fit = false;
    scale(factor, factor);
    updateSmoothing();
    emit zoomChanged(zoom());
}

void ImageView::updateSmoothing()
{
    // Smooth when zooming out, crisp pixels when zooming in (matters for palettes and pixel art).
    // A stretched preview frame is always smoothed: blur looks more natural than jaggies.
    const double itemScale = m_item->scale();
    const double effective = zoom() * itemScale;
    m_item->setTransformationMode(effective < 1.0 || itemScale > 1.0 ? Qt::SmoothTransformation
                                                                     : Qt::FastTransformation);
}

void ImageView::updateBackground()
{
    setBackgroundBrush(checkerBrush(palette().color(QPalette::Window)));
}

void ImageView::wheelEvent(QWheelEvent* event)
{
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        QGraphicsView::wheelEvent(event);
        return;
    }
    applyZoom(delta > 0 ? kZoomStep : 1.0 / kZoomStep);
    event->accept();
}

void ImageView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    if (m_fit && hasImage())
        fitToWindow();
}

void ImageView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && hasImage()) {
        if (m_fit)
            zoomActualSize();
        else
            fitToWindow();
        event->accept();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void ImageView::changeEvent(QEvent* event)
{
    QGraphicsView::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        updateBackground();
}

void ImageView::paintEvent(QPaintEvent* event)
{
    QGraphicsView::paintEvent(event);

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    if (!hasImage())
        drawPlaceholder(painter);
    if (!m_overlayText.isEmpty())
        drawOverlay(painter);
}

void ImageView::drawPlaceholder(QPainter& painter)
{
    const QRect area = viewport()->rect();
    QColor text = palette().color(QPalette::WindowText);
    text.setAlpha(150);

    const int iconSize = 72;
    QFont titleFont = font();
    titleFont.setPointSizeF(font().pointSizeF() * 1.5);
    titleFont.setBold(true);
    QFont hintFont = font();
    hintFont.setPointSizeF(font().pointSizeF() * 1.05);

    const QString title = tr("Drop an image here");
    const QString hint = tr("or open a file (Ctrl+O), paste from the clipboard (Ctrl+V)");
    const int titleHeight = QFontMetrics(titleFont).height();
    const int hintHeight = QFontMetrics(hintFont).height();
    const int total = iconSize + 18 + titleHeight + 6 + hintHeight;
    int y = area.center().y() - total / 2;

    theme::icon(QStringLiteral("image"), text).paint(&painter, QRect(area.center().x() - iconSize / 2, y, iconSize, iconSize));
    y += iconSize + 18;

    painter.setPen(text);
    painter.setFont(titleFont);
    painter.drawText(QRect(area.left(), y, area.width(), titleHeight), Qt::AlignHCenter | Qt::AlignVCenter, title);
    y += titleHeight + 6;
    painter.setFont(hintFont);
    painter.drawText(QRect(area.left() + 16, y, area.width() - 32, hintHeight * 2), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, hint);
}

void ImageView::drawOverlay(QPainter& painter)
{
    QFont f = font();
    f.setBold(true);
    painter.setFont(f);
    const QFontMetrics fm(f);
    const QSize textSize(fm.horizontalAdvance(m_overlayText), fm.height());
    const QRect viewportRect = viewport()->rect();
    const QRect pill(viewportRect.center().x() - textSize.width() / 2 - 14, viewportRect.bottom() - textSize.height() - 34,
                     textSize.width() + 28, textSize.height() + 14);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(20, 20, 24, 190));
    painter.drawRoundedRect(pill, 9, 9);
    painter.setPen(Qt::white);
    painter.drawText(pill, Qt::AlignCenter, m_overlayText);
}

} // namespace kolorit
