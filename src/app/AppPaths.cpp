#include "app/AppPaths.h"

#include <QCoreApplication>
#include <QDir>

namespace kolorit::AppPaths {

namespace {

QString appSubdir(const QString& name)
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(name);
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
