#pragma once

#include <QStringList>

namespace kolorit::batch {

/// Whether the command line asks for batch mode
bool isRequested(const QStringList& arguments);

/// Batch repainting without a window; returns the process exit code.
///
/// The core does not depend on QtWidgets, so the same repaint the window button
/// performs can be run over a folder from the command line:
///
///   kolorit-cli <files or folders> --palette <PNG|builtin name> --out <folder>
///               [--model rgb|hsl|hsv|xyz|lab] [--dither none|bayer2|bayer4|bayer8|bluenoise|
///                floyd|atkinson|sierra|stucki|jarvis] [--strength 0..128] [--format png|jpg|webp|bmp]
///   kolorit-cli --list-palettes
///
/// Parses the arguments and reports errors itself; progress goes to stdout.
int run(const QStringList& arguments);

} // namespace kolorit::batch
