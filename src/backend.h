#pragma once

#include <QDBusUnixFileDescriptor>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTimer>

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(int capacity READ capacity NOTIFY telemetryChanged)
    Q_PROPERTY(QString status READ status NOTIFY telemetryChanged)
    Q_PROPERTY(bool onAc READ onAc NOTIFY telemetryChanged)
    Q_PROPERTY(double energyFullWh READ energyFullWh NOTIFY telemetryChanged)
    Q_PROPERTY(double energyDesignWh READ energyDesignWh NOTIFY telemetryChanged)
    Q_PROPERTY(int healthPercent READ healthPercent NOTIFY telemetryChanged)
    Q_PROPERTY(double voltageV READ voltageV NOTIFY telemetryChanged)
    Q_PROPERTY(double powerW READ powerW NOTIFY telemetryChanged)
    Q_PROPERTY(QString behaviour READ behaviour NOTIFY telemetryChanged)
    Q_PROPERTY(int startThreshold READ startThreshold NOTIFY telemetryChanged)
    Q_PROPERTY(int endThreshold READ endThreshold NOTIFY telemetryChanged)

    Q_PROPERTY(QString stage READ stage NOTIFY stateChanged)
    Q_PROPERTY(bool calibrating READ calibrating NOTIFY stateChanged)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    Q_PROPERTY(double beforeFullWh READ beforeFullWh NOTIFY stateChanged)
    Q_PROPERTY(int beforeHealth READ beforeHealth NOTIFY stateChanged)
    Q_PROPERTY(bool skipPrecharge READ skipPrecharge WRITE setSkipPrecharge NOTIFY optionsChanged)
    Q_PROPERTY(int minDischargePercent READ minDischargePercent WRITE setMinDischargePercent NOTIFY optionsChanged)
    Q_PROPERTY(double minVoltageV READ minVoltageV WRITE setMinVoltageV NOTIFY optionsChanged)

public:
    enum class Stage { Idle, Precharging, Discharging, Recharging, Completed, Aborted };
    Q_ENUM(Stage)

    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    // For unit tests & headless CLI
    void setPaths(const QString &batDir, const QString &acDir,
                  const QString &runtimeDir, const QString &stateDir,
                  const QString &helperPath, bool usePkexec);
    void startPolling();
    void readTelemetry();
    void pollOnce();
    QJsonObject statusJson() const;
    QJsonObject cliStatusJson() const;
    bool cliIsGuiCalibrating() const;

    int capacity() const { return m_capacity; }
    QString status() const { return m_status; }
    bool onAc() const { return m_onAc; }
    double energyFullWh() const { return m_energyFullWh; }
    double energyDesignWh() const { return m_energyDesignWh; }
    int healthPercent() const { return m_healthPercent; }
    double voltageV() const { return m_voltageV; }
    double powerW() const { return m_powerW; }
    QString behaviour() const { return m_behaviour; }
    int startThreshold() const { return m_startThreshold; }
    int endThreshold() const { return m_endThreshold; }

    QString stage() const;
    Stage stageEnum() const { return m_stage; }
    bool calibrating() const;
    QString message() const { return m_message; }
    double beforeFullWh() const { return m_beforeFullWh; }
    int beforeHealth() const { return m_beforeHealth; }

    bool skipPrecharge() const { return m_skipPrecharge; }
    void setSkipPrecharge(bool v);
    int minDischargePercent() const { return m_minDischargePercent; }
    void setMinDischargePercent(int v);
    double minVoltageV() const { return m_minVoltageV; }
    void setMinVoltageV(double v);

    Q_INVOKABLE bool startCalibration();
    Q_INVOKABLE void abortCalibration(const QString &reason = QString());
    Q_INVOKABLE bool applyThresholds(int start, int stop);
    Q_INVOKABLE void resetResult();

    // CLI helpers
    int cliAbort();

signals:
    void telemetryChanged();
    void stateChanged();
    void optionsChanged();

private:
    QString readText(const QString &path) const;
    long long readLong(const QString &path, long long fallback = -1) const;
    QString parseActiveBehaviour(const QString &raw) const;
    bool isFullChargeReached();
    void advanceStateMachine();
    bool startHelperSession();
    bool sendHelperCommand(const QString &cmd);
    void stopHelperSession();
    void acquireSleepInhibit();
    void releaseSleepInhibit();
    QString currentPowerProfile() const;
    void setPowerProfile(const QString &profile);
    void saveSessionFile() const;
    void removeSessionFile() const;
    void recoverOrphanedSession();
    void writeRuntimeStatus() const;
    void finishCalibration(Stage finalStage, const QString &msg);

    QString m_batDir{"/sys/class/power_supply/BAT0"};
    QString m_acDir{"/sys/class/power_supply/AC"};
    QString m_runtimeDir;
    QString m_stateDir;
    QString m_helperPath{"/usr/lib/batcal/batcal-helper"};
    bool m_usePkexec = true;

    int m_capacity = 0;
    QString m_status{"Unknown"};
    bool m_onAc = false;
    double m_energyFullWh = 0.0;
    double m_energyDesignWh = 0.0;
    int m_healthPercent = 0;
    double m_voltageV = 0.0;
    double m_powerW = 0.0;
    QString m_behaviour{"auto"};
    int m_startThreshold = 0;
    int m_endThreshold = 100;

    Stage m_stage = Stage::Idle;
    QString m_message;
    double m_beforeFullWh = 0.0;
    int m_beforeHealth = 0;

    bool m_skipPrecharge = false;
    int m_minDischargePercent = 5;
    double m_minVoltageV = 10.2;

    bool m_dischargeConfirmed = false;
    int m_lowVoltageStreak = 0;
    int m_fullHoldTicks = 0;
    int m_origStart = 0;
    int m_origEnd = 100;
    QString m_origProfile;

    QProcess *m_helperProc = nullptr;
    QDBusUnixFileDescriptor m_inhibitFd;
    QTimer m_pollTimer;
};
