#pragma once

#include <QImage>
#include <QPromise>

#include <functional>

namespace kolorit {

/// An operation on an image: takes the source and a promise for progress and
/// cancellation, returns the result (a null QImage if the operation was cancelled).
///
/// Every operation of the application (repainting, effects, VHS, palette building)
/// has this type, so they are all run by the same ImageTaskRunner.
using ImageOp = std::function<QImage(const QImage& source, QPromise<QImage>& promise)>;

} // namespace kolorit
