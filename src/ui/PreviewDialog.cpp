#include "ui/PreviewDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace kolorit {

PreviewDialog::PreviewDialog(const QImage& source, QWidget* parent)
    : QDialog(parent)
    , m_source(source)
{
    // Modeless: the main window stays alive, the picture can be zoomed and dragged
    setModal(false);

    if (!source.isNull()) {
        const int longest = std::max(source.width(), source.height());
        if (longest > kPreviewSize) {
            m_preview = source.scaled(kPreviewSize, kPreviewSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            m_scale = double(m_preview.width()) / double(source.width());
        } else {
            m_preview = source;
        }
    }

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(120);
    connect(&m_debounce, &QTimer::timeout, this, &PreviewDialog::runPreview);

    m_fullDelay.setSingleShot(true);
    m_fullDelay.setInterval(kFullPreviewDelayMs);
    connect(&m_fullDelay, &QTimer::timeout, this, &PreviewDialog::runFullPreview);

    connect(&m_runner, &ImageTaskRunner::started, this, [this] {
        if (m_progress) {
            m_progress->setValue(0);
            m_progress->setVisible(true);
        }
        if (m_status)
            m_status->setText(m_runningFull ? tr("Full size…") : tr("Preview…"));
    });
    connect(&m_runner, &ImageTaskRunner::progressChanged, this, [this](int percent) {
        if (m_progress)
            m_progress->setValue(percent);
    });
    connect(&m_runner, &ImageTaskRunner::finished, this, &PreviewDialog::onFinished);
    connect(&m_runner, &ImageTaskRunner::canceled, this, [this] {
        if (m_progress && !m_runner.isRunning())
            m_progress->setVisible(false);
    });
}

QWidget* PreviewDialog::buildFooter(const QString& acceptText)
{
    auto* footer = new QWidget(this);
    auto* layout = new QHBoxLayout(footer);
    layout->setContentsMargins(0, 0, 0, 0);

    m_status = new QLabel(footer);
    m_progress = new QProgressBar(footer);
    m_progress->setRange(0, 100);
    m_progress->setMaximumWidth(160);
    m_progress->setTextVisible(false);
    m_progress->setVisible(false);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, footer);
    m_buttons->button(QDialogButtonBox::Ok)->setText(acceptText);
    m_buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    layout->addWidget(m_status, 1);
    layout->addWidget(m_progress);
    layout->addWidget(m_buttons);
    return footer;
}

void PreviewDialog::schedulePreview()
{
    ++m_generation;
    m_hasFullResult = false;
    m_fullResult = QImage();
    m_fullDelay.stop();
    m_debounce.start();
}

void PreviewDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    schedulePreview();
}

QImage PreviewDialog::takeFullResult()
{
    if (!m_hasFullResult || m_fullGeneration != m_generation)
        return {};
    m_hasFullResult = false;
    return std::exchange(m_fullResult, QImage());
}

void PreviewDialog::runPreview()
{
    if (m_preview.isNull())
        return;
    // If the source is no larger than the preview, the fast frame is already full size
    m_runningFull = m_scale >= 1.0;
    m_runningGeneration = m_generation;
    m_runner.start(m_preview, makeOp(m_scale));
}

void PreviewDialog::runFullPreview()
{
    if (m_source.isNull() || m_scale >= 1.0)
        return;
    m_runningFull = true;
    m_runningGeneration = m_generation;
    m_runner.start(m_source, makeOp(1.0));
}

void PreviewDialog::onFinished(const QImage& image, qint64 ms)
{
    if (m_progress)
        m_progress->setVisible(false);
    // The computation may have started before the last parameter change: the frame is still
    // shown as an intermediate one, but only an up-to-date one counts as the full result
    const bool current = m_runningGeneration == m_generation;

    if (m_runningFull) {
        if (current) {
            m_fullResult = image;
            m_fullGeneration = m_generation;
            m_hasFullResult = true;
            m_fullElapsedMs = ms;
        }
        if (m_status)
            m_status->setText(tr("Full size %1×%2 in %3 ms").arg(image.width()).arg(image.height()).arg(ms));
        emit previewReady(image, true);
        return;
    }

    if (m_status)
        m_status->setText(tr("Preview %1×%2 in %3 ms").arg(image.width()).arg(image.height()).arg(ms));
    emit previewReady(image, false);

    // The user paused, so compute at full resolution
    if (current)
        m_fullDelay.start();
}

} // namespace kolorit
