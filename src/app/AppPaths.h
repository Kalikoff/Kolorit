#pragma once

#include <QString>

namespace kolorit::AppPaths {

/// Working folders next to the executable:
/// Images for source pictures, Results for output, Palette for user palettes.
QString imagesDir();
QString resultsDir();
QString palettesDir();

/// Create the folders if they do not exist yet
void ensureDirectories();

} // namespace kolorit::AppPaths
