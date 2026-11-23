#include "theme.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>

Theme::Theme(QObject *parent) : QObject(parent) {
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this]() {
        reload();
        armWatcher();
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        reload();
        armWatcher();
    });
    reload();
    armWatcher();
}

QColor Theme::accentForeground() const {
    const double lum = 0.299 * m_accent.redF() + 0.587 * m_accent.greenF() + 0.114 * m_accent.blueF();
    return lum > 0.55 ? QColor("#141416") : QColor("#ffffff");
}

void Theme::armWatcher() {
    const QString path = QDir::homePath() + "/.local/state/omarchy/current/theme/colors.toml";
    const QString dir = QDir::homePath() + "/.local/state/omarchy/current";
    if (QFile::exists(path) && !m_watcher.files().contains(path))
        m_watcher.addPath(path);
    if (QDir(dir).exists() && !m_watcher.directories().contains(dir))
        m_watcher.addPath(dir);
}

void Theme::reload() {
    QColor nextAccent("#FFD60A");
    QFile f(QDir::homePath() + "/.local/state/omarchy/current/theme/colors.toml");
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        static const QRegularExpression re(R"(^\s*accent\s*=\s*["'](#[0-9A-Fa-f]{6})["'])");
        while (!in.atEnd()) {
            const auto m = re.match(in.readLine());
            if (m.hasMatch()) {
                QColor c(m.captured(1));
                if (c.isValid()) nextAccent = c;
                break;
            }
        }
    }

    int nextRadius = 0;
    QProcess proc;
    proc.start("hyprctl", {"-j", "getoption", "decoration:rounding"});
    if (proc.waitForFinished(500) && proc.exitCode() == 0) {
        const auto doc = QJsonDocument::fromJson(proc.readAllStandardOutput());
        if (doc.isObject())
            nextRadius = qMax(0, doc.object().value("int").toInt(0));
    }

    if (nextAccent != m_accent || nextRadius != m_radius) {
        m_accent = nextAccent;
        m_radius = nextRadius;
        emit changed();
    }
}
