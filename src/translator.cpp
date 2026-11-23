#include "translator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>

Translator::Translator(QObject *parent) : QObject(parent) {
    QCoreApplication::installTranslator(&m_translator);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this]() { reload(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this]() { reload(); });
    reload();
}

void Translator::reload() {
    const QString langDir = QDir::homePath() + "/.config/omarchy";
    const QString langFile = langDir + "/language";
    if (QDir(langDir).exists() && !m_watcher.directories().contains(langDir))
        m_watcher.addPath(langDir);
    if (QFile::exists(langFile) && !m_watcher.files().contains(langFile))
        m_watcher.addPath(langFile);

    QString lang = QLocale::system().name().replace('_', '-');
    QFile f(langFile);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString v = QString::fromUtf8(f.readAll()).trimmed();
        if (!v.isEmpty()) lang = v;
    }

    QHash<QString, QString> map;
    const QStringList candidates = {
        QString(":/i18n/%1.json").arg(lang),
        QString(":/i18n/%1.json").arg(lang.section('-', 0, 0))
    };
    for (const QString &res : candidates) {
        QFile rf(res);
        if (rf.open(QIODevice::ReadOnly)) {
            const auto obj = QJsonDocument::fromJson(rf.readAll()).object();
            for (auto it = obj.begin(); it != obj.end(); ++it)
                map.insert(it.key(), it.value().toString());
            break;
        }
    }

    m_translator.loadMap(map);
    if (m_language != lang) {
        m_language = lang;
        emit languageChanged();
    }
}
