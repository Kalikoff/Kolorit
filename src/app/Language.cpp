#include "app/Language.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace kolorit::language {

namespace {

const QString kSettingsKey = QStringLiteral("ui/language");
const QString kSource = QStringLiteral("en");

} // namespace

QStringList available()
{
    return {kSource, QStringLiteral("ru")};
}

QString nativeName(const QString& code)
{
    if (code == QLatin1String("ru"))
        return QStringLiteral("Русский");
    return QStringLiteral("English");
}

QString preferred()
{
    const QString code = QSettings().value(kSettingsKey).toString();
    return available().contains(code) ? code : QString();
}

void setPreferred(const QString& code)
{
    if (code.isEmpty())
        QSettings().remove(kSettingsKey);
    else
        QSettings().setValue(kSettingsKey, code);
}

QString resolved()
{
    const QString chosen = preferred();
    if (!chosen.isEmpty())
        return chosen;
    // uiLanguages() is ordered by the user's preference: "ru-RU", "en-US", ...
    for (const QString& tag : QLocale::system().uiLanguages()) {
        const QString code = tag.section(QLatin1Char('-'), 0, 0).toLower();
        if (available().contains(code))
            return code;
    }
    return kSource;
}

void install()
{
    const QString code = resolved();
    if (code == kSource)
        return;

    auto* app = QCoreApplication::instance();
    auto* own = new QTranslator(app);
    if (own->load(QStringLiteral(":/i18n/kolorit_%1.qm").arg(code)))
        QCoreApplication::installTranslator(own);
    else
        delete own;

    // Standard dialog buttons and the file dialog come from Qt's own catalog. The Qt
    // installation has it as qtbase_xx.qm; windeployqt merges the modules into qt_xx.qm
    // and qt.conf points QLibraryInfo at the deployed folder. In the macOS bundle the
    // install step puts qtbase_xx.qm into Contents/Resources/translations.
    const QStringList qtDirs = {
        QLibraryInfo::path(QLibraryInfo::TranslationsPath),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("translations")),
#ifdef Q_OS_MACOS
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../Resources/translations")),
#endif
    };
    for (const QString& dir : qtDirs) {
        for (const QString& name : {QStringLiteral("qtbase_%1"), QStringLiteral("qt_%1")}) {
            auto* qt = new QTranslator(app);
            if (qt->load(name.arg(code), dir)) {
                QCoreApplication::installTranslator(qt);
                return;
            }
            delete qt;
        }
    }
}

} // namespace kolorit::language
