#include "backend.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <csignal>
#include <cmath>

Backend::Backend(QObject *parent) : QObject(parent) {
    const QString xdgRuntime = qEnvironmentVariable("XDG_RUNTIME_DIR", "/tmp");
    m_runtimeDir = xdgRuntime + "/batcal";
    m_stateDir = QDir::homePath() + "/.local/state/batcal";

    // Fallback to local repo helper when running from build/
    if (!QFile::exists(m_helperPath)) {
        const QString devHelper = QCoreApplication::applicationDirPath() + "/../scripts/batcal-helper";
        if (QFile::exists(devHelper)) m_helperPath = devHelper;
    }

    m_pollTimer.setInterval(2000);
    connect(&m_pollTimer, &QTimer::timeout, this, &Backend::pollOnce);
}

Backend::~Backend() {
    if (calibrating()) {
        abortCalibration(tr("Application closed during calibration"));
    } else {
        stopHelperSession();
        releaseSleepInhibit();
    }
}

void Backend::setPaths(const QString &batDir, const QString &acDir,
                       const QString &runtimeDir, const QString &stateDir,
                       const QString &helperPath, bool usePkexec) {
    m_batDir = batDir;
    m_acDir = acDir;
    m_runtimeDir = runtimeDir;
    m_stateDir = stateDir;
    m_helperPath = helperPath;
    m_usePkexec = usePkexec;
}

void Backend::startPolling() {
    recoverOrphanedSession();
    pollOnce();
    m_pollTimer.start();
}

QString Backend::readText(const QString &path) const {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

long long Backend::readLong(const QString &path, long long fallback) const {
    bool ok = false;
    const long long v = readText(path).toLongLong(&ok);
    return ok ? v : fallback;
}

QString Backend::parseActiveBehaviour(const QString &raw) const {
    static const QRegularExpression re(R"(\[([a-z-]+)\])");
    const auto m = re.match(raw);
    return m.hasMatch() ? m.captured(1) : QStringLiteral("auto");
}

QString Backend::stage() const {
    switch (m_stage) {
        case Stage::Idle:        return QStringLiteral("idle");
        case Stage::Precharging: return QStringLiteral("precharging");
        case Stage::Discharging: return QStringLiteral("discharging");
        case Stage::Recharging:  return QStringLiteral("recharging");
        case Stage::Completed:   return QStringLiteral("completed");
        case Stage::Aborted:     return QStringLiteral("aborted");
    }
    return QStringLiteral("idle");
}

bool Backend::calibrating() const {
    return m_stage == Stage::Precharging ||
           m_stage == Stage::Discharging ||
           m_stage == Stage::Recharging;
}

void Backend::setSkipPrecharge(bool v) {
    if (m_skipPrecharge != v) { m_skipPrecharge = v; emit optionsChanged(); }
}

void Backend::setMinDischargePercent(int v) {
    v = qBound(0, v, 20);
    if (m_minDischargePercent != v) { m_minDischargePercent = v; emit optionsChanged(); }
}

void Backend::setMinVoltageV(double v) {
    v = qBound(9.6, v, 10.8);
    if (std::abs(m_minVoltageV - v) > 0.01) { m_minVoltageV = v; emit optionsChanged(); }
}

void Backend::readTelemetry() {
    m_capacity = static_cast<int>(readLong(m_batDir + "/capacity", 0));
    m_status = readText(m_batDir + "/status");
    if (m_status.isEmpty()) m_status = QStringLiteral("Unknown");
    m_onAc = (readLong(m_acDir + "/online", 0) == 1);

    const long long fullUwh = readLong(m_batDir + "/energy_full", 0);
    const long long designUwh = readLong(m_batDir + "/energy_full_design", 0);
    m_energyFullWh = fullUwh / 1e6;
    m_energyDesignWh = designUwh / 1e6;
    m_healthPercent = (designUwh > 0) ? static_cast<int>(std::round(fullUwh * 100.0 / designUwh)) : 0;

    m_voltageV = readLong(m_batDir + "/voltage_now", 0) / 1e6;
    m_powerW = std::abs(readLong(m_batDir + "/power_now", 0)) / 1e6;
    m_behaviour = parseActiveBehaviour(readText(m_batDir + "/charge_behaviour"));
    m_startThreshold = static_cast<int>(readLong(m_batDir + "/charge_control_start_threshold", 0));
    m_endThreshold = static_cast<int>(readLong(m_batDir + "/charge_control_end_threshold", 100));

    emit telemetryChanged();
}

void Backend::pollOnce() {
    readTelemetry();

    // Check remote SSH abort flag
    const QString abortFlag = m_runtimeDir + "/abort.flag";
    if (QFile::exists(abortFlag)) {
        QFile::remove(abortFlag);
        if (calibrating()) {
            abortCalibration(tr("Aborted remotely via CLI"));
            writeRuntimeStatus();
            return;
        }
    }

    if (calibrating()) {
        advanceStateMachine();
    }
    writeRuntimeStatus();
}

bool Backend::isFullChargeReached() {
    if (m_status == QStringLiteral("Full")) {
        m_fullHoldTicks = 0;
        return true;
    }
    if (m_capacity >= 99 && m_status == QStringLiteral("Not charging") && m_powerW < 0.05) {
        m_fullHoldTicks++;
        if (m_fullHoldTicks >= 15) { // 15 * 2s = 30s
            m_fullHoldTicks = 0;
            return true;
        }
    } else {
        m_fullHoldTicks = 0;
    }
    return false;
}

void Backend::advanceStateMachine() {
    if (!m_onAc) {
        abortCalibration(tr("AC adapter disconnected — calibration aborted for safety"));
        return;
    }

    if (m_stage == Stage::Precharging) {
        if (isFullChargeReached()) {
            m_stage = Stage::Discharging;
            m_dischargeConfirmed = false;
            m_lowVoltageStreak = 0;
            m_message = tr("Force-discharging battery safely...");
            setPowerProfile(QStringLiteral("power-saver"));
            if (!sendHelperCommand(QStringLiteral("SET_BEHAVIOUR force-discharge"))) {
                abortCalibration(tr("Failed to enable force-discharge"));
                return;
            }
            emit stateChanged();
        }
        return;
    }

    if (m_stage == Stage::Discharging) {
        if (!m_dischargeConfirmed) {
            if (m_behaviour == QStringLiteral("force-discharge")) {
                m_dischargeConfirmed = true;
            }
            return;
        }

        // Voltage debounce brake (3 consecutive readings below minVoltageV)
        if (m_voltageV > 1.0 && m_voltageV < m_minVoltageV) {
            m_lowVoltageStreak++;
        } else {
            m_lowVoltageStreak = 0;
        }

        bool ecReturnedAuto = (m_behaviour != QStringLiteral("force-discharge"));
        bool hitPercentFloor = (m_minDischargePercent > 0 && m_capacity <= m_minDischargePercent);
        bool hitVoltageFloor = (m_lowVoltageStreak >= 3);

        if (ecReturnedAuto || hitPercentFloor || hitVoltageFloor) {
            if (!ecReturnedAuto) {
                sendHelperCommand(QStringLiteral("SET_BEHAVIOUR auto"));
            }
            m_stage = Stage::Recharging;
            m_fullHoldTicks = 0;
            if (hitVoltageFloor) {
                m_message = tr("Safety voltage floor (%1V) reached — recharging to 100%").arg(m_minVoltageV, 0, 'f', 1);
            } else if (hitPercentFloor) {
                m_message = tr("Safety charge floor (%1%) reached — recharging to 100%").arg(m_minDischargePercent);
            } else {
                m_message = tr("EC completed discharge — recharging to 100%");
            }
            emit stateChanged();
        }
        return;
    }

    if (m_stage == Stage::Recharging) {
        if (isFullChargeReached()) {
            finishCalibration(Stage::Completed,
                tr("Calibration complete: %1 Wh (%2%) → %3 Wh (%4%)")
                    .arg(m_beforeFullWh, 0, 'f', 2).arg(m_beforeHealth)
                    .arg(m_energyFullWh, 0, 'f', 2).arg(m_healthPercent));
        }
    }
}

bool Backend::startCalibration() {
    if (calibrating()) return false;
    pollOnce();
    if (!m_onAc) {
        m_message = tr("Connect AC adapter before starting calibration");
        emit stateChanged();
        return false;
    }

    m_origStart = m_startThreshold;
    m_origEnd = m_endThreshold;
    m_origProfile = currentPowerProfile();
    m_beforeFullWh = m_energyFullWh;
    m_beforeHealth = m_healthPercent;

    if (!startHelperSession()) {
        m_message = tr("Authorization cancelled or helper failed to start");
        emit stateChanged();
        return false;
    }

    saveSessionFile();
    acquireSleepInhibit();
    sendHelperCommand(QStringLiteral("ENABLE_UPOWER_RECAL"));
    if (!sendHelperCommand(QStringLiteral("SET_THRESHOLDS 0 100"))) {
        abortCalibration(tr("Failed to unlock charge thresholds to 0–100%"));
        return false;
    }

    m_fullHoldTicks = 0;
    m_lowVoltageStreak = 0;
    m_dischargeConfirmed = false;

    if (m_skipPrecharge || m_status == QStringLiteral("Full") || m_capacity >= 100) {
        m_stage = Stage::Discharging;
        m_message = tr("Force-discharging battery safely...");
        setPowerProfile(QStringLiteral("power-saver"));
        if (!sendHelperCommand(QStringLiteral("SET_BEHAVIOUR force-discharge"))) {
            abortCalibration(tr("Failed to enable force-discharge"));
            return false;
        }
    } else {
        m_stage = Stage::Precharging;
        m_message = tr("Precharging battery to 100% before discharge...");
    }

    emit stateChanged();
    writeRuntimeStatus();
    return true;
}

void Backend::abortCalibration(const QString &reason) {
    if (!calibrating() && m_helperProc == nullptr) return;
    finishCalibration(Stage::Aborted, reason.isEmpty() ? tr("Calibration aborted") : reason);
}

void Backend::finishCalibration(Stage finalStage, const QString &msg) {
    sendHelperCommand(QStringLiteral("SET_BEHAVIOUR auto"));
    sendHelperCommand(QStringLiteral("SET_THRESHOLDS %1 %2").arg(m_origStart).arg(m_origEnd));
    sendHelperCommand(QStringLiteral("RESTORE_UPOWER_RECAL"));
    stopHelperSession();
    releaseSleepInhibit();
    if (!m_origProfile.isEmpty()) setPowerProfile(m_origProfile);
    removeSessionFile();

    m_stage = finalStage;
    m_message = msg;
    emit stateChanged();
    writeRuntimeStatus();
}

void Backend::resetResult() {
    if (!calibrating()) {
        m_stage = Stage::Idle;
        m_message.clear();
        emit stateChanged();
    }
}

bool Backend::applyThresholds(int start, int stop) {
    if (calibrating()) {
        m_message = tr("Cannot change thresholds while calibration is active");
        emit stateChanged();
        return false;
    }
    if (start < 0 || stop > 100 || start >= stop) {
        m_message = tr("Invalid threshold range");
        emit stateChanged();
        return false;
    }

    QProcess proc;
    if (m_usePkexec) {
        proc.start(QStringLiteral("pkexec"), {m_helperPath, QStringLiteral("set-thresholds"),
                                              QString::number(start), QString::number(stop)});
    } else {
        proc.start(m_helperPath, {QStringLiteral("set-thresholds"),
                                  QString::number(start), QString::number(stop)});
    }
    if (!proc.waitForFinished(30000) || proc.exitCode() != 0) {
        m_message = tr("Failed to apply charge thresholds");
        emit stateChanged();
        return false;
    }
    pollOnce();
    m_message = tr("Charge range updated to %1% – %2%").arg(start).arg(stop);
    emit stateChanged();
    return true;
}

bool Backend::startHelperSession() {
    stopHelperSession();
    m_helperProc = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("BATCAL_BAT_DIR"), m_batDir);
    m_helperProc->setProcessEnvironment(env);

    if (m_usePkexec) {
        m_helperProc->start(QStringLiteral("pkexec"), {m_helperPath, QStringLiteral("--session")});
    } else {
        m_helperProc->start(m_helperPath, {QStringLiteral("--session")});
    }

    if (!m_helperProc->waitForStarted(30000)) {
        stopHelperSession();
        return false;
    }
    if (!m_helperProc->waitForReadyRead(30000)) {
        stopHelperSession();
        return false;
    }
    const QString line = QString::fromUtf8(m_helperProc->readLine()).trimmed();
    if (line != QStringLiteral("READY")) {
        stopHelperSession();
        return false;
    }
    return true;
}

bool Backend::sendHelperCommand(const QString &cmd) {
    if (!m_helperProc || m_helperProc->state() != QProcess::Running) return false;
    m_helperProc->write((cmd + "\n").toUtf8());
    m_helperProc->waitForBytesWritten(3000);
    while (!m_helperProc->canReadLine()) {
        if (!m_helperProc->waitForReadyRead(10000)) return false;
    }
    const QString resp = QString::fromUtf8(m_helperProc->readLine()).trimmed();
    return resp == QStringLiteral("OK");
}

void Backend::stopHelperSession() {
    if (!m_helperProc) return;
    if (m_helperProc->state() == QProcess::Running) {
        m_helperProc->write("QUIT\n");
        m_helperProc->waitForBytesWritten(1000);
        if (!m_helperProc->waitForFinished(3000)) {
            m_helperProc->terminate();
            m_helperProc->waitForFinished(1000);
        }
    }
    m_helperProc->deleteLater();
    m_helperProc = nullptr;
}

void Backend::acquireSleepInhibit() {
    QDBusInterface iface(QStringLiteral("org.freedesktop.login1"),
                         QStringLiteral("/org/freedesktop/login1"),
                         QStringLiteral("org.freedesktop.login1.Manager"),
                         QDBusConnection::systemBus());
    if (!iface.isValid()) return;
    QDBusReply<QDBusUnixFileDescriptor> reply = iface.call(
        QStringLiteral("Inhibit"), QStringLiteral("sleep:idle"),
        QStringLiteral("batcal"), QStringLiteral("Battery calibration in progress"),
        QStringLiteral("block"));
    if (reply.isValid()) m_inhibitFd = reply.value();
}

void Backend::releaseSleepInhibit() {
    m_inhibitFd = QDBusUnixFileDescriptor();
}

QString Backend::currentPowerProfile() const {
    QProcess p;
    p.start(QStringLiteral("powerprofilesctl"), {QStringLiteral("get")});
    if (p.waitForFinished(1000) && p.exitCode() == 0)
        return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    return {};
}

void Backend::setPowerProfile(const QString &profile) {
    if (profile.isEmpty()) return;
    QProcess::startDetached(QStringLiteral("powerprofilesctl"), {QStringLiteral("set"), profile});
}

void Backend::saveSessionFile() const {
    QDir().mkpath(m_stateDir);
    QJsonObject obj;
    obj[QStringLiteral("origStart")] = m_origStart;
    obj[QStringLiteral("origEnd")] = m_origEnd;
    obj[QStringLiteral("origProfile")] = m_origProfile;
    QFile f(m_stateDir + "/session.json");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void Backend::removeSessionFile() const {
    QFile::remove(m_stateDir + "/session.json");
}

void Backend::recoverOrphanedSession() {
    QFile f(m_stateDir + "/session.json");
    if (!f.open(QIODevice::ReadOnly)) return;
    const auto obj = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    const QString prof = obj.value(QStringLiteral("origProfile")).toString();
    if (!prof.isEmpty()) setPowerProfile(prof);
    const int origS = obj.value(QStringLiteral("origStart")).toInt(0);
    const int origE = obj.value(QStringLiteral("origEnd")).toInt(100);
    readTelemetry();
    if ((origS != m_startThreshold || origE != m_endThreshold) && origS < origE) {
        m_message = tr("Previous session ended unexpectedly; original range was %1%–%2%").arg(origS).arg(origE);
        emit stateChanged();
    }
    removeSessionFile();
}

QJsonObject Backend::statusJson() const {
    QJsonObject obj;
    obj[QStringLiteral("pid")] = QCoreApplication::applicationPid();
    obj[QStringLiteral("stage")] = stage();
    obj[QStringLiteral("calibrating")] = calibrating();
    obj[QStringLiteral("capacity")] = m_capacity;
    obj[QStringLiteral("status")] = m_status;
    obj[QStringLiteral("onAc")] = m_onAc;
    obj[QStringLiteral("energyFullWh")] = m_energyFullWh;
    obj[QStringLiteral("energyDesignWh")] = m_energyDesignWh;
    obj[QStringLiteral("healthPercent")] = m_healthPercent;
    obj[QStringLiteral("voltageV")] = m_voltageV;
    obj[QStringLiteral("powerW")] = m_powerW;
    obj[QStringLiteral("behaviour")] = m_behaviour;
    obj[QStringLiteral("startThreshold")] = m_startThreshold;
    obj[QStringLiteral("endThreshold")] = m_endThreshold;
    return obj;
}

void Backend::writeRuntimeStatus() const {
    QDir().mkpath(m_runtimeDir);
    QFile f(m_runtimeDir + "/status.json");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(statusJson()).toJson(QJsonDocument::Compact));
}

bool Backend::cliIsGuiCalibrating() const {
    QFile sf(m_runtimeDir + "/status.json");
    if (!sf.open(QIODevice::ReadOnly)) return false;
    const auto obj = QJsonDocument::fromJson(sf.readAll()).object();
    const qint64 pid = obj.value(QStringLiteral("pid")).toVariant().toLongLong();
    if (pid <= 0 || pid == QCoreApplication::applicationPid()) return false;
    if (::kill(static_cast<pid_t>(pid), 0) != 0) return false;
    return obj.value(QStringLiteral("calibrating")).toBool(false);
}

QJsonObject Backend::cliStatusJson() const {
    QJsonObject obj = statusJson();
    QFile sf(m_runtimeDir + "/status.json");
    if (sf.open(QIODevice::ReadOnly)) {
        const auto guiObj = QJsonDocument::fromJson(sf.readAll()).object();
        const qint64 pid = guiObj.value(QStringLiteral("pid")).toVariant().toLongLong();
        if (pid > 0 && pid != QCoreApplication::applicationPid() && ::kill(static_cast<pid_t>(pid), 0) == 0) {
            obj[QStringLiteral("pid")] = pid;
            obj[QStringLiteral("stage")] = guiObj.value(QStringLiteral("stage")).toString(QStringLiteral("idle"));
            obj[QStringLiteral("calibrating")] = guiObj.value(QStringLiteral("calibrating")).toBool(false);
        }
    }
    return obj;
}

int Backend::cliAbort() {
    readTelemetry();
    if (cliIsGuiCalibrating()) {
        QDir().mkpath(m_runtimeDir);
        QFile flag(m_runtimeDir + "/abort.flag");
        if (flag.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            flag.write("1\n");
            return 0;
        }
        return 1;
    }

    // Fallback: GUI not running, rescue stuck force-discharge directly
    if (m_behaviour == QStringLiteral("force-discharge")) {
        QProcess proc;
        if (m_usePkexec)
            proc.start(QStringLiteral("pkexec"), {m_helperPath, QStringLiteral("set-behaviour"), QStringLiteral("auto")});
        else
            proc.start(m_helperPath, {QStringLiteral("set-behaviour"), QStringLiteral("auto")});
        return (proc.waitForFinished(30000) && proc.exitCode() == 0) ? 0 : 1;
    }
    return 0;
}
