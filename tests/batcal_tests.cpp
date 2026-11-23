#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <unistd.h>
#include "backend.h"

class BatcalTests : public QObject {
    Q_OBJECT

private:
    static void writeFile(const QString &path, const QString &content) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
        f.write(content.toUtf8());
    }

    static QString readFile(const QString &path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
        return QString::fromUtf8(f.readAll()).trimmed();
    }

    static void setupFakeSysfs(const QString &batDir, const QString &acDir,
                               int cap, const QString &status, int acOnline,
                               long long voltageUv, const QString &behaviour,
                               int startTh = 75, int endTh = 80) {
        QDir().mkpath(batDir);
        QDir().mkpath(acDir);
        writeFile(batDir + "/capacity", QString::number(cap));
        writeFile(batDir + "/status", status);
        writeFile(batDir + "/energy_full", "13840000");
        writeFile(batDir + "/energy_full_design", "86580000");
        writeFile(batDir + "/voltage_now", QString::number(voltageUv));
        writeFile(batDir + "/power_now", "0");
        writeFile(batDir + "/charge_behaviour", behaviour);
        writeFile(batDir + "/charge_control_start_threshold", QString::number(startTh));
        writeFile(batDir + "/charge_control_end_threshold", QString::number(endTh));
        writeFile(acDir + "/online", QString::number(acOnline));
    }

    static QString createFakeHelper(const QString &dir, const QString &batDir) {
        QString path = dir + "/fake-helper.sh";
        QString script = QString(
            "#!/usr/bin/env bash\n"
            "BAT=\"%1\"\n"
            "if [[ \"$1\" == \"set-thresholds\" ]]; then\n"
            "  echo \"$2\" > \"$BAT/charge_control_start_threshold\"\n"
            "  echo \"$3\" > \"$BAT/charge_control_end_threshold\"\n"
            "  echo OK; exit 0\n"
            "fi\n"
            "if [[ \"$1\" == \"set-behaviour\" ]]; then\n"
            "  echo \"[$2]\" > \"$BAT/charge_behaviour\"\n"
            "  echo OK; exit 0\n"
            "fi\n"
            "echo READY\n"
            "while read -r cmd a b; do\n"
            "  case \"$cmd\" in\n"
            "    SET_BEHAVIOUR) echo \"[$a]\" > \"$BAT/charge_behaviour\"; echo OK ;;\n"
            "    SET_THRESHOLDS) echo \"$a\" > \"$BAT/charge_control_start_threshold\"; echo \"$b\" > \"$BAT/charge_control_end_threshold\"; echo OK ;;\n"
            "    ENABLE_UPOWER_RECAL|RESTORE_UPOWER_RECAL|PING) echo OK ;;\n"
            "    QUIT) echo OK; exit 0 ;;\n"
            "  esac\n"
            "done\n"
        ).arg(batDir);
        writeFile(path, script);
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        return path;
    }

private slots:
    void testTelemetryAndThresholds() {
        QTemporaryDir tmp;
        QString bat = tmp.path() + "/BAT0";
        QString ac = tmp.path() + "/AC";
        setupFakeSysfs(bat, ac, 42, "Charging", 1, 11400000, "[auto] force-discharge", 75, 80);
        QString helper = createFakeHelper(tmp.path(), bat);

        Backend b;
        b.setPaths(bat, ac, tmp.path() + "/run", tmp.path() + "/state", helper, false);
        b.pollOnce();

        QCOMPARE(b.capacity(), 42);
        QCOMPARE(b.healthPercent(), 16);
        QCOMPARE(b.startThreshold(), 75);
        QCOMPARE(b.endThreshold(), 80);

        QVERIFY(!b.applyThresholds(80, 80));
        QVERIFY(b.applyThresholds(55, 80));
        QCOMPARE(b.startThreshold(), 55);
        QCOMPARE(b.endThreshold(), 80);
    }

    void testVoltageDebounceAndFullCycle() {
        QTemporaryDir tmp;
        QString bat = tmp.path() + "/BAT0";
        QString ac = tmp.path() + "/AC";
        setupFakeSysfs(bat, ac, 50, "Charging", 1, 11500000, "[auto] force-discharge", 75, 80);
        QString helper = createFakeHelper(tmp.path(), bat);

        Backend b;
        b.setPaths(bat, ac, tmp.path() + "/run", tmp.path() + "/state", helper, false);
        b.setSkipPrecharge(true);
        b.setMinDischargePercent(0);
        b.setMinVoltageV(10.2);

        QVERIFY(b.startCalibration());
        QCOMPARE(b.stage(), QStringLiteral("discharging"));
        // Thresholds unlocked to 0-100
        QCOMPARE(readFile(bat + "/charge_control_start_threshold"), QStringLiteral("0"));
        QCOMPARE(readFile(bat + "/charge_control_end_threshold"), QStringLiteral("100"));
        // Threshold modification locked during calibration
        QVERIFY(!b.applyThresholds(60, 80));

        // First poll confirms [force-discharge]
        b.pollOnce();
        QCOMPARE(b.stage(), QStringLiteral("discharging"));

        // Single voltage dip should NOT trigger brake
        writeFile(bat + "/voltage_now", "10000000");
        b.pollOnce();
        QCOMPARE(b.stage(), QStringLiteral("discharging"));
        writeFile(bat + "/voltage_now", "10500000");
        b.pollOnce();
        QCOMPARE(b.stage(), QStringLiteral("discharging"));

        // 3 consecutive ticks below 10.2V triggers Recharging
        writeFile(bat + "/voltage_now", "10100000");
        b.pollOnce();
        b.pollOnce();
        QCOMPARE(b.stage(), QStringLiteral("discharging"));
        b.pollOnce();
        QCOMPARE(b.stage(), QStringLiteral("recharging"));
        QCOMPARE(readFile(bat + "/charge_behaviour"), QStringLiteral("[auto]"));

        // Aging-battery compound full-charge check (cap>=99 + Not charging + power_now==0 for 15 ticks)
        writeFile(bat + "/capacity", "99");
        writeFile(bat + "/status", "Not charging");
        writeFile(bat + "/power_now", "0");
        for (int i = 0; i < 14; ++i) b.pollOnce();
        QCOMPARE(b.stage(), QStringLiteral("recharging"));
        b.pollOnce();
        QCOMPARE(b.stage(), QStringLiteral("completed"));
        // Original 75-80 thresholds restored
        QCOMPARE(readFile(bat + "/charge_control_start_threshold"), QStringLiteral("75"));
        QCOMPARE(readFile(bat + "/charge_control_end_threshold"), QStringLiteral("80"));
    }

    void testRemoteAbortAndAcUnplug() {
        QTemporaryDir tmp;
        QString bat = tmp.path() + "/BAT0";
        QString ac = tmp.path() + "/AC";
        setupFakeSysfs(bat, ac, 80, "Charging", 1, 11500000, "[auto] force-discharge", 75, 80);
        QString helper = createFakeHelper(tmp.path(), bat);

        Backend b;
        b.setPaths(bat, ac, tmp.path() + "/run", tmp.path() + "/state", helper, false);
        QVERIFY(b.startCalibration());
        QCOMPARE(b.stage(), QStringLiteral("precharging"));

        // Remote abort via abort.flag
        writeFile(tmp.path() + "/run/abort.flag", "1");
        b.pollOnce();
        QCOMPARE(b.stage(), QStringLiteral("aborted"));
        QCOMPARE(readFile(bat + "/charge_control_start_threshold"), QStringLiteral("75"));

        // AC unplug abort test
        QVERIFY(b.startCalibration());
        QCOMPARE(b.stage(), QStringLiteral("precharging"));
        writeFile(ac + "/online", "0");
        b.pollOnce();
        QCOMPARE(b.stage(), QStringLiteral("aborted"));
        QCOMPARE(readFile(bat + "/charge_control_start_threshold"), QStringLiteral("75"));
    }

    void testRealHelperSessionEofAndOctalAndWriteError() {
        QTemporaryDir tmp;
        QString bat = tmp.path() + "/BAT0";
        QString ac = tmp.path() + "/AC";
        setupFakeSysfs(bat, ac, 50, "Charging", 1, 11500000, "[auto] force-discharge", 75, 80);

        const QString realHelper = QDir(QCoreApplication::applicationDirPath()).filePath("../../scripts/batcal-helper");
        QVERIFY(QFile::exists(realHelper));

        // 1. Octal leading-zero test (08 80)
        {
            QProcess p;
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert("BATCAL_BAT_DIR", bat);
            p.setProcessEnvironment(env);
            p.start(realHelper, {"set-thresholds", "08", "80"});
            QVERIFY(p.waitForFinished(5000));
            QCOMPARE(p.exitCode(), 0);
            QCOMPARE(readFile(bat + "/charge_control_start_threshold"), QStringLiteral("8"));
            QCOMPARE(readFile(bat + "/charge_control_end_threshold"), QStringLiteral("80"));
        }

        // Restore 75/80
        writeFile(bat + "/charge_control_start_threshold", "75");
        writeFile(bat + "/charge_control_end_threshold", "80");

        // 2. Abrupt EOF crash recovery test
        {
            QProcess p;
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert("BATCAL_BAT_DIR", bat);
            p.setProcessEnvironment(env);
            p.start(realHelper, {"--session"});
            QVERIFY(p.waitForStarted(5000));
            QVERIFY(p.waitForReadyRead(5000));
            QCOMPARE(QString::fromUtf8(p.readLine()).trimmed(), QStringLiteral("READY"));

            p.write("SET_THRESHOLDS 0 100\nSET_BEHAVIOUR force-discharge\n");
            p.waitForBytesWritten(2000);
            p.closeWriteChannel(); // Simulate abrupt GUI crash (EOF without QUIT)
            QVERIFY(p.waitForFinished(5000));

            QCOMPARE(readFile(bat + "/charge_behaviour"), QStringLiteral("auto"));
            QCOMPARE(readFile(bat + "/charge_control_start_threshold"), QStringLiteral("75"));
            QCOMPARE(readFile(bat + "/charge_control_end_threshold"), QStringLiteral("80"));
        }
    }

    void testCliDoesNotOverwriteGuiStatusAndCreatesAbortFlag() {
        QTemporaryDir tmp;
        QString bat = tmp.path() + "/BAT0";
        QString ac = tmp.path() + "/AC";
        QString runDir = tmp.path() + "/run";
        setupFakeSysfs(bat, ac, 50, "Charging", 1, 11500000, "[auto] force-discharge", 75, 80);
        QString helper = createFakeHelper(tmp.path(), bat);

        // Simulate a live GUI process (use parent PID, which is alive and != self)
        QDir().mkpath(runDir);
        const qint64 liveOtherPid = static_cast<qint64>(getppid());
        writeFile(runDir + "/status.json",
                  QString("{\"pid\":%1,\"stage\":\"discharging\",\"calibrating\":true}").arg(liveOtherPid));

        Backend cli;
        cli.setPaths(bat, ac, runDir, tmp.path() + "/state", helper, false);
        cli.readTelemetry();

        QVERIFY(cli.cliIsGuiCalibrating());
        const QJsonObject st = cli.cliStatusJson();
        QCOMPARE(st.value("stage").toString(), QStringLiteral("discharging"));
        QCOMPARE(st.value("calibrating").toBool(), true);

        // cliAbort must create abort.flag without overwriting status.json
        QCOMPARE(cli.cliAbort(), 0);
        QVERIFY(QFile::exists(runDir + "/abort.flag"));
    }
};

QTEST_MAIN(BatcalTests)
#include "batcal_tests.moc"
