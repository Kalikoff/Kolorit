#include "app/Updates.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSettings>
#include <QUrl>

namespace kolorit::updates {

namespace {

const QString kLatestReleaseUrl = QStringLiteral("https://api.github.com/repos/Kalikoff/Kolorit/releases/latest");
const QString kCheckOnStartupKey = QStringLiteral("updates/checkOnStartup");
const QString kSkippedVersionKey = QStringLiteral("updates/skippedVersion");
constexpr int kTimeoutMs = 15000;

/// The release file meant for this platform, matched by the names the Build workflow gives them
bool isAssetForThisPlatform(const QString& name)
{
#if defined(Q_OS_MACOS)
    return name.endsWith(QStringLiteral(".dmg"), Qt::CaseInsensitive);
#elif defined(Q_OS_WIN)
    return name.contains(QStringLiteral("windows"), Qt::CaseInsensitive)
           && name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive);
#else
    Q_UNUSED(name);
    return false;
#endif
}

} // namespace

std::optional<Release> parseRelease(const QByteArray& json)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    QString tag = root.value(QStringLiteral("tag_name")).toString();
    if (tag.startsWith(u'v', Qt::CaseInsensitive))
        tag.remove(0, 1);
    Release release;
    release.version = QVersionNumber::fromString(tag);
    if (release.version.isNull())
        return std::nullopt;
    release.pageUrl = root.value(QStringLiteral("html_url")).toString();
    release.notes = root.value(QStringLiteral("body")).toString();
    release.downloadUrl = release.pageUrl;
    for (const QJsonValue& asset : root.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject a = asset.toObject();
        if (isAssetForThisPlatform(a.value(QStringLiteral("name")).toString())) {
            release.downloadUrl = a.value(QStringLiteral("browser_download_url")).toString();
            break;
        }
    }
    return release;
}

bool isNewer(const Release& release, const QString& currentVersion)
{
    return release.version > QVersionNumber::fromString(currentVersion);
}

void fetchLatest(QObject* context,
                 std::function<void(const std::optional<Release>& release, const QString& error)> done)
{
    auto* network = new QNetworkAccessManager(context);
    QNetworkRequest request{QUrl(kLatestReleaseUrl)};
    // GitHub rejects API requests without a User-Agent
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Kolorit/%1").arg(QCoreApplication::applicationVersion()));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(kTimeoutMs);

    QNetworkReply* reply = network->get(request);
    QObject::connect(reply, &QNetworkReply::finished, context, [network, reply, done = std::move(done)] {
        reply->deleteLater();
        network->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            done(std::nullopt, reply->errorString());
            return;
        }
        const std::optional<Release> release = parseRelease(reply->readAll());
        done(release, release ? QString() : QCoreApplication::translate("kolorit::updates",
                                                                        "Unexpected answer from GitHub"));
    });
}

bool checkOnStartup()
{
    return QSettings().value(kCheckOnStartupKey, true).toBool();
}

void setCheckOnStartup(bool enabled)
{
    QSettings().setValue(kCheckOnStartupKey, enabled);
}

QString skippedVersion()
{
    return QSettings().value(kSkippedVersionKey).toString();
}

void setSkippedVersion(const QString& version)
{
    QSettings().setValue(kSkippedVersionKey, version);
}

} // namespace kolorit::updates
