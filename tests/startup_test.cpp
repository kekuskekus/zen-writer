#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QtTest>

class StartupTest final : public QObject
{
    Q_OBJECT

private slots:
    void startsWithSupportedArguments_data()
    {
        QTest::addColumn<QStringList>("arguments");
        QTest::newRow("default-fullscreen") << QStringList{};
        QTest::newRow("explicit-fullscreen") << QStringList{"--fullscreen"};
        QTest::newRow("windowed") << QStringList{"--windowed"};
        QTest::newRow("windowed-short") << QStringList{"-w"};
        QTest::newRow("windowed-overrides-fullscreen")
            << QStringList{"--fullscreen", "--windowed"};

        // Exercise the arguments actually shipped in the menu/autostart entry.
        QFile desktopFile(QStringLiteral(ZEN_WRITER_DESKTOP_FILE));
        QVERIFY(desktopFile.open(QIODevice::ReadOnly));
        QStringList desktopArguments;
        bool foundExec = false;
        while (!desktopFile.atEnd()) {
            const QString line = QString::fromUtf8(desktopFile.readLine()).trimmed();
            if (line.startsWith(QStringLiteral("Exec="))) {
                desktopArguments = QProcess::splitCommand(line.mid(5));
                QVERIFY(!desktopArguments.isEmpty());
                desktopArguments.removeFirst();
                foundExec = true;
                break;
            }
        }
        QVERIFY(foundExec);
        QTest::newRow("installed-desktop-entry") << desktopArguments;
    }

    void startsWithSupportedArguments()
    {
        QFETCH(QStringList, arguments);
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        QProcess process;
        configureProcess(process, profile.path());
        process.start(QStringLiteral(ZEN_WRITER_EXECUTABLE), arguments);
        QVERIFY2(process.waitForStarted(5000), qPrintable(process.errorString()));

        // An unsupported option exits immediately. A successful GUI launch
        // stays in the event loop; offscreen makes this usable in headless CI.
        const bool exitedEarly = process.waitForFinished(2000);
        const int earlyExitCode = exitedEarly ? process.exitCode() : -1;
        if (!exitedEarly) {
            process.terminate();
            if (!process.waitForFinished(3000)) {
                process.kill();
                QVERIFY(process.waitForFinished(3000));
            }
        }
        const QString diagnostic = QStringLiteral("Application exited during startup (code %1):\n%2")
                                       .arg(earlyExitCode)
                                       .arg(QString::fromUtf8(process.readAll()));
        QVERIFY2(!exitedEarly, qPrintable(diagnostic));
    }

    void rejectsUnknownArguments()
    {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        QProcess process;
        configureProcess(process, profile.path());
        process.start(QStringLiteral(ZEN_WRITER_EXECUTABLE), {"--not-a-zen-writer-option"});
        QVERIFY2(process.waitForStarted(5000), qPrintable(process.errorString()));
        QVERIFY(process.waitForFinished(5000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 1);
        QVERIFY(process.readAll().contains("not-a-zen-writer-option"));
    }

private:
    static void configureProcess(QProcess& process, const QString& profile)
    {
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        environment.insert(QStringLiteral("HOME"), profile);
        environment.insert(QStringLiteral("XDG_CONFIG_HOME"), profile + "/config");
        environment.insert(QStringLiteral("XDG_DATA_HOME"), profile + "/data");
        environment.insert(QStringLiteral("XDG_CACHE_HOME"), profile + "/cache");
        const QString runtime = profile + "/runtime";
        QDir().mkpath(runtime);
        QFile::setPermissions(runtime, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        environment.insert(QStringLiteral("XDG_RUNTIME_DIR"), runtime);
        process.setProcessEnvironment(environment);
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.setWorkingDirectory(profile);
    }
};

QTEST_GUILESS_MAIN(StartupTest)
#include "startup_test.moc"
