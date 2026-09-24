// Console entry point: batch repainting without a window.
//
// A separate executable is needed because of the Windows subsystem. Kolorit.exe is
// built as a GUI application (WIN32), which has no console of its own: its output goes
// nowhere and the shell returns to the prompt without waiting for it. A console build
// behaves like an ordinary command-line tool: it prints to the terminal and returns
// an exit code.

#include "app/BatchRunner.h"

#include <QGuiApplication>

int main(int argc, char* argv[])
{
    // No display is needed: everything happens on QImage
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Kalikoff"));
    QCoreApplication::setApplicationName(QStringLiteral("Kolorit"));
    QCoreApplication::setApplicationVersion(QStringLiteral(KOLORIT_VERSION));

    return kolorit::batch::run(QCoreApplication::arguments());
}
