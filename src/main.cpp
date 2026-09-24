#include "app/AppPaths.h"
#include "app/BatchRunner.h"
#include "app/Language.h"
#include "processing/VhsEffect.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QIcon>
#include <QSettings>
#include <QStringList>

#include <algorithm>
#include <cstdio>

int main(int argc, char* argv[])
{
    QStringList rawArgs;
    rawArgs.reserve(argc);
    for (int i = 0; i < argc; ++i)
        rawArgs << QString::fromLocal8Bit(argv[i]);
    // Batch mode lives in the separate console kolorit-cli: the GUI build has nowhere
    // to print to, and the shell does not wait for it
    if (kolorit::batch::isRequested(rawArgs)) {
        std::fputs("Batch mode is provided by kolorit-cli:\n"
                   "  kolorit-cli --palette <file|name> --out <folder> <input...>\n",
                   stderr);
        return 2;
    }

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Kalikoff"));
    QCoreApplication::setApplicationName(QStringLiteral("Kolorit"));
    QCoreApplication::setApplicationVersion(QStringLiteral(KOLORIT_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/kolorit.svg")));

    kolorit::language::install();

    // The style is applied before any window is created; the dark theme is the default
    kolorit::theme::captureSystemDefaults();
    const int themeMode = QSettings().value(QStringLiteral("ui/theme"), int(kolorit::theme::Mode::Dark)).toInt();
    kolorit::theme::apply(kolorit::theme::Mode(std::clamp(themeMode, 0, 2)));

    kolorit::vhs::registerFonts();
    kolorit::AppPaths::ensureDirectories();

    kolorit::MainWindow window;
    window.show();

    // A file passed on the command line is opened right away
    const QStringList args = QCoreApplication::arguments();
    if (args.size() > 1)
        window.openImageFile(args.at(1));

    return app.exec();
}
