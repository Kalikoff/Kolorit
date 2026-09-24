#include "core/ImageTaskRunner.h"

#include <QtConcurrent/QtConcurrentRun>

namespace kolorit {

ImageTaskRunner::ImageTaskRunner(QObject* parent)
    : QObject(parent)
{
    // A single orchestrator thread: operations run strictly one after another,
    // and row-level parallelism comes from the global pool inside the operation.
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(-1);

    connect(&m_watcher, &QFutureWatcher<QImage>::progressValueChanged, this, &ImageTaskRunner::progressChanged);
    connect(&m_watcher, &QFutureWatcher<QImage>::finished, this, &ImageTaskRunner::onWatcherFinished);
}

ImageTaskRunner::~ImageTaskRunner()
{
    m_pending.reset();
    if (m_running) {
        m_watcher.cancel();
        m_watcher.waitForFinished();
    }
    m_pool.waitForDone();
}

void ImageTaskRunner::start(const QImage& source, ImageOp op)
{
    Pending pending{source, std::move(op)};
    if (m_running) {
        // The new task replaces the queued one; the current one is cancelled and ends by itself
        m_pending = std::move(pending);
        m_watcher.cancel();
        return;
    }
    launch(std::move(pending));
}

void ImageTaskRunner::cancel()
{
    m_pending.reset();
    if (m_running)
        m_watcher.cancel();
}

void ImageTaskRunner::launch(Pending pending)
{
    m_running = true;
    m_timer.start();
    emit started();

    auto task = [source = std::move(pending.source), op = std::move(pending.op)](QPromise<QImage>& promise) {
        promise.setProgressRange(0, 100);
        const QImage result = op(source, promise);
        if (!promise.isCanceled() && !result.isNull())
            promise.addResult(result);
    };
    m_watcher.setFuture(QtConcurrent::run(&m_pool, std::move(task)));
}

void ImageTaskRunner::onWatcherFinished()
{
    m_running = false;
    const qint64 elapsed = m_timer.elapsed();

    const bool wasCanceled = m_watcher.isCanceled() || m_watcher.future().resultCount() == 0;
    if (wasCanceled)
        emit canceled();
    else
        emit finished(m_watcher.result(), elapsed);

    if (m_pending) {
        Pending next = std::move(*m_pending);
        m_pending.reset();
        launch(std::move(next));
    }
}

} // namespace kolorit
