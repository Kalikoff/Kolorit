#pragma once

#include <QList>
#include <QtConcurrent/QtConcurrentMap>

#include <atomic>
#include <numeric>

namespace kolorit {

/// The progress range (in percent) that one processing pass occupies.
/// Lets multi-pass operations share a single 0..100 scale.
struct ProgressSpan {
    int from = 0;
    int to = 100;
};

/// A no-op progress sink for synchronous calls without cancellation
struct NoProgress {
    bool isCanceled() const { return false; }
    void setProgressValue(int) {}
};

/// Runs fn(y) in parallel for every row y ∈ [0, rows).
///
/// Work is spread over the global QThreadPool (all CPU cores); blocks of rows are
/// handed out adaptively, so the overhead stays small even for cheap rows. Progress is
/// reported through progress.setProgressValue(), cancellation is checked before every
/// row through progress.isCanceled().
///
/// Progress is a QPromise<T> or NoProgress. The function blocks the calling thread
/// until all rows are done (the calling thread takes part in the work too).
template <typename Progress, typename Fn>
void parallelRows(int rows, Progress& progress, ProgressSpan span, Fn&& fn)
{
    if (rows <= 0)
        return;

    QList<int> indices(rows);
    std::iota(indices.begin(), indices.end(), 0);

    std::atomic<int> done{0};
    const int spanLength = span.to - span.from;

    QtConcurrent::blockingMap(indices, [&](int y) {
        if (progress.isCanceled())
            return;
        fn(y);
        const int finished = done.fetch_add(1, std::memory_order_relaxed) + 1;
        progress.setProgressValue(span.from + int(qint64(spanLength) * finished / rows));
    });
}

} // namespace kolorit
