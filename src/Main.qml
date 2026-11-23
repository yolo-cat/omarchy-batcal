import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    visible: true
    width: 520
    height: 540
    minimumWidth: 460
    minimumHeight: 480
    title: "Batcal"
    color: "#141416"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool showShortcuts: false

    Shortcut {
        sequence: "Space"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (thresholdDialog.visible || quitDialog.visible || shortcutsDialog.visible) return
            if (backend.calibrating) backend.abortCalibration()
            else backend.startCalibration()
        }
    }

    Shortcut {
        sequence: "T"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (!backend.calibrating) thresholdDialog.openWith(backend.startThreshold, backend.endThreshold)
        }
    }

    Shortcut {
        sequence: "?"
        context: Qt.ApplicationShortcut
        onActivated: win.showShortcuts = !win.showShortcuts
    }

    Shortcut {
        sequence: "Q"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (backend.calibrating) quitDialog.open()
            else Qt.quit()
        }
    }

    onClosing: function(close) {
        if (backend.calibrating) {
            close.accepted = false
            quitDialog.open()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        // ---------- Header & Live Telemetry ----------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: telemetryCol.implicitHeight + 24
            radius: theme.radius
            color: "#1c1c20"
            border.color: "#2c2c32"
            border.width: 1

            ColumnLayout {
                id: telemetryCol
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        textFormat: Text.PlainText
                        text: qsTr("Battery Calibration")
                        font.pixelSize: 18
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: backend.capacity + "% · " + backend.status + (backend.onAc ? " (AC)" : "")
                        font.pixelSize: 14
                        font.bold: true
                        color: theme.accent
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 4
                    columnSpacing: 12
                    rowSpacing: 4

                    Label { textFormat: Text.PlainText; text: qsTr("Health"); opacity: 0.65; font.pixelSize: 12 }
                    Label { textFormat: Text.PlainText; text: qsTr("Voltage"); opacity: 0.65; font.pixelSize: 12 }
                    Label { textFormat: Text.PlainText; text: qsTr("Power"); opacity: 0.65; font.pixelSize: 12 }
                    Label { textFormat: Text.PlainText; text: qsTr("Mode"); opacity: 0.65; font.pixelSize: 12 }

                    Label {
                        textFormat: Text.PlainText
                        text: backend.healthPercent + "% (" + backend.energyFullWh.toFixed(1) + "/" + backend.energyDesignWh.toFixed(0) + " Wh)"
                        font.bold: true
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: backend.voltageV.toFixed(2) + " V"
                        font.bold: true
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: backend.powerW.toFixed(1) + " W"
                        font.bold: true
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: backend.behaviour
                        font.bold: true
                    }
                }
            }
        }

        // ---------- Battery Gliding Summary Row ----------
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 54
            radius: theme.radius
            color: "#1c1c20"
            border.color: "#2c2c32"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 10

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        textFormat: Text.PlainText
                        text: qsTr("Battery Gliding (Charge Range)")
                        font.pixelSize: 12
                        opacity: 0.7
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: backend.startThreshold + "% – " + backend.endThreshold + "%"
                        font.pixelSize: 15
                        font.bold: true
                    }
                }

                Button {
                    text: qsTr("Adjust…")
                    enabled: !backend.calibrating
                    onClicked: thresholdDialog.openWith(backend.startThreshold, backend.endThreshold)
                }
            }
        }

        // ---------- Calibration Stage & Controls ----------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: theme.radius
            color: "#1c1c20"
            border.color: "#2c2c32"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12

                Label {
                    textFormat: Text.PlainText
                    text: qsTr("Calibration")
                    font.pixelSize: 15
                    font.bold: true
                }

                // 4-stage indicator
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Repeater {
                        model: [
                            { key: "precharging", label: qsTr("Precharge") },
                            { key: "discharging", label: qsTr("Discharge") },
                            { key: "recharging",  label: qsTr("Recharge") },
                            { key: "completed",   label: qsTr("Done") }
                        ]
                        Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            height: 28
                            radius: Math.min(4, theme.radius)
                            color: backend.stage === modelData.key ? theme.accent : "#2a2a30"
                            Label {
                                anchors.centerIn: parent
                                textFormat: Text.PlainText
                                text: modelData.label
                                font.pixelSize: 12
                                font.bold: backend.stage === modelData.key
                                color: backend.stage === modelData.key ? theme.accentForeground : "#d0d0d5"
                            }
                        }
                    }
                }

                CheckBox {
                    text: qsTr("Skip precharge (start discharging directly)")
                    checked: backend.skipPrecharge
                    enabled: !backend.calibrating
                    onToggled: backend.skipPrecharge = checked
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Label { textFormat: Text.PlainText; text: qsTr("Safety floor:"); font.pixelSize: 13 }
                    SpinBox {
                        from: 0; to: 20; value: backend.minDischargePercent
                        enabled: !backend.calibrating
                        onValueModified: backend.minDischargePercent = value
                        textFromValue: function(v) { return v === 0 ? "EC (0%)" : v + "%" }
                    }
                    Item { Layout.fillWidth: true }
                    Label { textFormat: Text.PlainText; text: qsTr("Min voltage:"); font.pixelSize: 13 }
                    Button {
                        text: "−"
                        enabled: !backend.calibrating && backend.minVoltageV > 9.61
                        onClicked: backend.minVoltageV = Math.round((backend.minVoltageV - 0.1) * 10) / 10
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: backend.minVoltageV.toFixed(1) + "V"
                        font.bold: true
                    }
                    Button {
                        text: "+"
                        enabled: !backend.calibrating && backend.minVoltageV < 10.79
                        onClicked: backend.minVoltageV = Math.round((backend.minVoltageV + 0.1) * 10) / 10
                    }
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    textFormat: Text.PlainText
                    font.pixelSize: 12
                    opacity: 0.75
                    text: backend.message.length > 0
                          ? backend.message
                          : qsTr("Please save your work before starting. Keep AC adapter connected.")
                }

                Item { Layout.fillHeight: true }

                Button {
                    Layout.fillWidth: true
                    highlighted: true
                    text: backend.calibrating ? qsTr("Safe Abort") : qsTr("Start Calibration")
                    enabled: backend.calibrating || backend.onAc
                    onClicked: {
                        if (backend.calibrating) backend.abortCalibration()
                        else backend.startCalibration()
                    }
                }
            }
        }
    }

    ThresholdDialog {
        id: thresholdDialog
        onAcceptedRange: function(s, e) {
            backend.applyThresholds(s, e)
        }
    }

    Dialog {
        id: quitDialog
        modal: true
        anchors.centerIn: parent
        title: qsTr("Quit while calibrating?")
        Label {
            textFormat: Text.PlainText
            text: qsTr("Leaving now will safely abort calibration and restore auto mode.")
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Cancel"); onClicked: quitDialog.close() }
            Button {
                text: qsTr("Abort & Quit")
                highlighted: true
                onClicked: {
                    backend.abortCalibration()
                    Qt.quit()
                }
            }
        }
    }

    Dialog {
        id: shortcutsDialog
        visible: win.showShortcuts
        modal: true
        anchors.centerIn: parent
        title: qsTr("Keyboard Shortcuts")
        onClosed: win.showShortcuts = false
        ColumnLayout {
            spacing: 6
            Label { textFormat: Text.PlainText; text: qsTr("Space  —  Start / Abort calibration") }
            Label { textFormat: Text.PlainText; text: qsTr("T      —  Battery gliding (charge range)") }
            Label { textFormat: Text.PlainText; text: qsTr("?      —  Toggle shortcuts") }
            Label { textFormat: Text.PlainText; text: qsTr("Q      —  Quit") }
        }
    }
}
