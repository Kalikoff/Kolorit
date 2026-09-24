#pragma once

#include "core/ImageTask.h"
#include "core/Palette.h"

#include <QImage>

#include <cstdint>

namespace kolorit::palette_builder {

/// Palette of average chunk colors: the image is split into columns×rows parts, the
/// color of every part is averaged, duplicate colors are dropped.
/// Runs synchronously, but the rows of chunks are processed in parallel.
Palette fromChunks(const QImage& image, int columns, int rows);

/// All unique colors of the image (at most maxColors, in scan order)
Palette uniqueColors(const QImage& image, int maxColors = 256);

/// k-means clustering: finds the k most characteristic colors of the image.
/// Returns an operation for ImageTaskRunner; the result is a k×1 image (a palette in
/// file format) that is read back with Palette::fromImage().
ImageOp kMeansOp(int k, int iterations, uint32_t seed);

/// Reduce a palette to count colors: the same k-means, but over the palette itself
/// rather than over a photo.
///
/// This turns the 213 colors of RAL Classic into a working set of sixteen, and "all
/// unique colors" into a palette that is actually usable. Clustering happens in
/// CIE Lab; it runs synchronously since palettes are small.
Palette reduce(const Palette& palette, int count, uint32_t seed = 1);

} // namespace kolorit::palette_builder
