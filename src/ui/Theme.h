#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace kolorit::theme {

/// Application look
enum class Mode {
    System = 0,   ///< style and colors of the operating system
    Dark = 1,     ///< dark Fusion theme (the default: it does not distract from the image)
    Light = 2,    ///< light Fusion theme
};

/// Remember the system style and palette so they can be restored.
/// Called automatically on the first apply().
void captureSystemDefaults();

/// Apply the look to the whole application (style, palette, style sheet)
void apply(Mode mode);

Mode current();
QString modeName(Mode mode);

/// Accent color (primary action buttons, selection)
QColor accent();

/// Icon from the resources :/icons/<name>.svg, recolored to the text color of the current palette.
/// Drawn as vectors at any size and DPI; semi-transparent when disabled.
QIcon icon(const QString& name);

/// The same icon in a given color (for example, white for the primary action button)
QIcon icon(const QString& name, const QColor& color);

} // namespace kolorit::theme
