#pragma once

#include "core/ImageTask.h"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QThreadPool>

#include <optional>

namespace kolorit {

/// Runs an ImageOp asynchronously with progress, cancellation and timing.
///
/// Threading:
///  - the operation starts in a private pool of one thread (the orchestrator thread),
///    so the interface never blocks;
///  - inside the operation parallelRows() spreads rows over the global QThreadPool,
///    that is, over all CPU cores.
///
/// If start() is called while a task is running, the current task is cancelled and the
/// new one is queued to start right after it ends; this is how the live preview in the
/// dialogs works.
class ImageTaskRunner : public QObject {
    Q_OBJECT
public:
    explicit ImageTaskRunner(QObject* parent = nullptr);
    ~ImageTaskRunner() override;

    bool isRunning() const { return m_running; }

    void start(const QImage& source, ImageOp op);
    void cancel();

signals:
    void started();
    void progressChanged(int percent);
    void finished(const QImage& result, qint64 elapsedMs);
    void canceled();

private:
    struct Pending {
        QImage source;
        ImageOp op;
    };

    void launch(Pending pending);
    void onWatcherFinished();

    QThreadPool m_pool;
    QFutureWatcher<QImage> m_watcher;
    QElapsedTimer m_timer;
    std::optional<Pending> m_pending;
    bool m_running = false;
};

} // namespace kolorit
