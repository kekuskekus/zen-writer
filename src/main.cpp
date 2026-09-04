#include "main_window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QIcon>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Zen Writer"));
    QCoreApplication::setApplicationVersion(QStringLiteral(ZEN_WRITER_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("ZenWriter"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("local.zenwriter"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Distraction-free writing for Raspberry Pi"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption fullscreen(QStringLiteral("fullscreen"),
                                 QStringLiteral("Start in full screen (the default)."));
    parser.addOption(fullscreen);
    QCommandLineOption windowed(QStringList {QStringLiteral("w"), QStringLiteral("windowed")},
                                QStringLiteral("Start in a normal window."));
    parser.addOption(windowed);
    parser.addPositionalArgument(QStringLiteral("file"),
                                 QStringLiteral("Optional .txt or .md file to open."),
                                 QStringLiteral("[file]"));
    parser.process(application);

    MainWindow window;
    const QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty()) {
        window.openInitialFile(positional.constFirst());
    } else {
        window.restoreSession();
    }

    if (parser.isSet(windowed)) {
        window.resize(1200, 800);
        window.show();
    } else {
        window.showFullScreen();
    }

    return application.exec();
}
