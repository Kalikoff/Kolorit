#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVersionNumber>

#include <functional>
#include <optional>

namespace kolorit::updates {

/// The latest published release on GitHub
struct Release {
    QVersionNumber version;
    QString pageUrl;       ///< the release page with notes and every file
    QString downloadUrl;   ///< the build for this platform, or the release page if there is none
    QString notes;         ///< release notes in Markdown
};

/// Parse a GitHub "latest release" response; nothing if it is not a release of a known version
std::optional<Release> parseRelease(const QByteArray& json);

/// Whether the release is newer than the given version ("1.1.0")
bool isNewer(const Release& release, const QString& currentVersion);

/// Ask GitHub for the latest release in the background. `done` is called once, in the
/// thread of `context`, with the release or with an error message; it is not called if
/// `context` is destroyed first.
void fetchLatest(QObject* context,
                 std::function<void(const std::optional<Release>& release, const QString& error)> done);

/// Settings: whether to check at startup, and a version the user chose to skip
bool checkOnStartup();
void setCheckOnStartup(bool enabled);
QString skippedVersion();
void setSkippedVersion(const QString& version);

} // namespace kolorit::updates
