#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)

public:
    explicit Theme(QObject *parent = nullptr);
    QColor accent() const { return m_accent; }
    QColor accentForeground() const;
    int radius() const { return m_radius; }

signals:
    void changed();

private:
    void reload();
    void armWatcher();

    QColor m_accent{"#FFD60A"};
    int m_radius = 0;
    QFileSystemWatcher m_watcher;
};
