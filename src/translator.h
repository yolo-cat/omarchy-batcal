#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QTranslator>

class JsonTranslator : public QTranslator {
public:
    void loadMap(const QHash<QString, QString> &map) { m_map = map; }
    bool isEmpty() const override { return m_map.isEmpty(); }
    QString translate(const char *, const char *sourceText, const char *, int) const override {
        return m_map.value(QString::fromUtf8(sourceText));
    }

private:
    QHash<QString, QString> m_map;
};

class Translator : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString language READ language NOTIFY languageChanged)

public:
    explicit Translator(QObject *parent = nullptr);
    QString language() const { return m_language; }

signals:
    void languageChanged();

private:
    void reload();

    QString m_language{"en"};
    JsonTranslator m_translator;
    QFileSystemWatcher m_watcher;
};
