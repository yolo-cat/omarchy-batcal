#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTextStream>
#include <QUrl>

#include "backend.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    if (argc > 1) {
        QCoreApplication app(argc, argv);
        QTextStream out(stdout);
        QTextStream err(stderr);
        const QStringList args = app.arguments();

        const QString usageText =
            QStringLiteral("Usage: batcal [--status [--json] | --threshold <start> <stop> | --abort]\n");

        if (args.contains("--help") || args.contains("-h")) {
            out << usageText;
            return 0;
        }

        // Reject unknown flags cleanly without crashing QGuiApplication
        const QStringList known = {"--status", "--json", "--threshold", "--abort", "--help", "-h"};
        for (int i = 1; i < args.size(); ++i) {
            if (args[i].startsWith('-') && !known.contains(args[i])) {
                err << "Unknown option: " << args[i] << "\n" << usageText;
                return 1;
            }
        }

        Backend backend;
        backend.readTelemetry();

        if (args.contains("--abort")) {
            int rc = backend.cliAbort();
            if (rc == 0) out << "Abort signal sent.\n";
            else err << "Failed to abort calibration.\n";
            return rc;
        }

        int ti = args.indexOf("--threshold");
        if (ti >= 0) {
            if (ti + 2 >= args.size()) {
                err << "Usage: batcal --threshold <start> <stop>\n";
                return 1;
            }
            if (backend.cliIsGuiCalibrating()) {
                err << "Cannot change thresholds while calibration is active.\n";
                return 1;
            }
            bool ok1 = false, ok2 = false;
            int s = args[ti + 1].toInt(&ok1);
            int e = args[ti + 2].toInt(&ok2);
            if (!ok1 || !ok2 || !backend.applyThresholds(s, e)) {
                err << "Failed to set thresholds.\n";
                return 1;
            }
            out << "Thresholds set to " << s << "% - " << e << "%\n";
            return 0;
        }

        if (args.contains("--status") || args.contains("--json")) {
            const QJsonObject st = backend.cliStatusJson();
            if (args.contains("--json")) {
                out << QString::fromUtf8(QJsonDocument(st).toJson(QJsonDocument::Indented));
                return 0;
            }
            out << "Batcal Status\n"
                << "  Stage:      " << st.value("stage").toString() << "\n"
                << "  Capacity:   " << backend.capacity() << "% (" << backend.status() << ")\n"
                << "  Health:     " << backend.healthPercent() << "% ("
                << QString::number(backend.energyFullWh(), 'f', 2) << " / "
                << QString::number(backend.energyDesignWh(), 'f', 2) << " Wh)\n"
                << "  Voltage:    " << QString::number(backend.voltageV(), 'f', 2) << " V\n"
                << "  AC Online:  " << (backend.onAc() ? "yes" : "no") << "\n"
                << "  Behaviour:  " << backend.behaviour() << "\n"
                << "  Thresholds: " << backend.startThreshold() << "% - " << backend.endThreshold() << "%\n";
            return 0;
        }

        err << usageText;
        return 1;
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("batcal"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setDesktopFileName(QStringLiteral("batcal"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("batcal")));
    QQuickStyle::setStyle(QStringLiteral("Material"));

    Theme theme;
    Translator translator;
    Backend backend;
    backend.startPolling();

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::languageChanged, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    engine.rootContext()->setContextProperty(QStringLiteral("translator"), &translator);
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;

    return app.exec();
}
