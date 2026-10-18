#include "app/AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace kolorit::AppPaths {

namespace {

QString appSubdir(const QString& name)
{
#ifdef Q_OS_MACOS
    // The executable sits inside Kolorit.app, usually in the read-only /Applications,
    // and writing into the bundle would break its signature
    const QString base = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                             .filePath(QStringLiteral("Kolorit"));
#else
    const QString base = QCoreApplication::applicationDirPath();
#endif
    return QDir(base).filePath(name);
}

} // namespace

QString imagesDir()
{
    return appSubdir(QStringLiteral("Images"));
}

QString resultsDir()
{
    return appSubdir(QStringLiteral("Results"));
}

QString palettesDir()
{
    return appSubdir(QStringLiteral("Palette"));
}

void ensureDirectories()
{
    for (const QString& path : {imagesDir(), resultsDir(), palettesDir()})
        QDir().mkpath(path);
}

} // namespace kolorit::AppPaths
