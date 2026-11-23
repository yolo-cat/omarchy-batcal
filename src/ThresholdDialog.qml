import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: dlg
    modal: true
    anchors.centerIn: parent
    width: 440
    title: qsTr("Battery Gliding (Charge Range)")

    property int startVal: 75
    property int stopVal: 80

    signal acceptedRange(int start, int stop)

    function openWith(s, e) {
        startVal = Math.max(0, Math.min(95, s))
        stopVal = Math.max(startVal + 5, Math.min(100, e))
        open()
    }

    function applyAndClose() {
        dlg.acceptedRange(dlg.startVal, dlg.stopVal)
        dlg.close()
    }

    Shortcut {
        sequences: [StandardKey.InsertParagraphSeparator, "Return", "Enter"]
        enabled: dlg.visible
        onActivated: dlg.applyAndClose()
    }

    ColumnLayout {
        width: parent.width
        spacing: 14

        // Visual gliding range bar
        Rectangle {
            Layout.fillWidth: true
            height: 24
            radius: Math.min(4, theme.radius)
            color: "#2a2a30"

            Rectangle {
                x: parent.width * (dlg.startVal / 100.0)
                width: Math.max(4, parent.width * ((dlg.stopVal - dlg.startVal) / 100.0))
                height: parent.height
                radius: parent.radius
                color: theme.accent
            }

            Rectangle {
                anchors.centerIn: parent
                width: rangeLabel.implicitWidth + 12
                height: rangeLabel.implicitHeight + 2
                radius: Math.min(4, theme.radius)
                color: Qt.rgba(0.08, 0.08, 0.09, 0.72)

                Label {
                    id: rangeLabel
                    anchors.centerIn: parent
                    textFormat: Text.PlainText
                    text: dlg.startVal + "% – " + dlg.stopVal + "%"
                    font.pixelSize: 12
                    font.bold: true
                    color: "#ffffff"
                }
            }
        }

        // Quick presets
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Button {
                Layout.fillWidth: true
                text: qsTr("Long-term AC (75–80%)")
                font.pixelSize: 11
                onClicked: { dlg.startVal = 75; dlg.stopVal = 80 }
            }
            Button {
                Layout.fillWidth: true
                text: qsTr("Balanced (55–80%)")
                font.pixelSize: 11
                onClicked: { dlg.startVal = 55; dlg.stopVal = 80 }
            }
            Button {
                Layout.fillWidth: true
                text: qsTr("Full Travel (0–100%)")
                font.pixelSize: 11
                onClicked: { dlg.startVal = 0; dlg.stopVal = 100 }
            }
        }

        // Start threshold
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                Label { textFormat: Text.PlainText; text: qsTr("Start charging below:"); Layout.fillWidth: true }
                Button {
                    text: "−"
                    implicitWidth: 32
                    enabled: dlg.startVal > 0
                    onClicked: dlg.startVal = Math.max(0, dlg.startVal - 5)
                }
                Label { textFormat: Text.PlainText; text: dlg.startVal + "%"; font.bold: true; Layout.preferredWidth: 38; horizontalAlignment: Text.AlignHCenter }
                Button {
                    text: "+"
                    implicitWidth: 32
                    enabled: dlg.startVal < 95
                    onClicked: {
                        dlg.startVal = Math.min(95, dlg.startVal + 5)
                        if (dlg.startVal >= dlg.stopVal) dlg.stopVal = Math.min(100, dlg.startVal + 5)
                    }
                }
            }
            Slider {
                Layout.fillWidth: true
                from: 0; to: 95; stepSize: 5
                value: dlg.startVal
                onMoved: {
                    dlg.startVal = Math.round(value)
                    if (dlg.startVal >= dlg.stopVal) dlg.stopVal = Math.min(100, dlg.startVal + 5)
                }
            }
        }

        // Stop threshold
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                Label { textFormat: Text.PlainText; text: qsTr("Stop charging at:"); Layout.fillWidth: true }
                Button {
                    text: "−"
                    implicitWidth: 32
                    enabled: dlg.stopVal > 10
                    onClicked: {
                        dlg.stopVal = Math.max(10, dlg.stopVal - 5)
                        if (dlg.stopVal <= dlg.startVal) dlg.startVal = Math.max(0, dlg.stopVal - 5)
                    }
                }
                Label { textFormat: Text.PlainText; text: dlg.stopVal + "%"; font.bold: true; Layout.preferredWidth: 38; horizontalAlignment: Text.AlignHCenter }
                Button {
                    text: "+"
                    implicitWidth: 32
                    enabled: dlg.stopVal < 100
                    onClicked: dlg.stopVal = Math.min(100, dlg.stopVal + 5)
                }
            }
            Slider {
                Layout.fillWidth: true
                from: 10; to: 100; stepSize: 5
                value: dlg.stopVal
                onMoved: {
                    dlg.stopVal = Math.round(value)
                    if (dlg.stopVal <= dlg.startVal) dlg.startVal = Math.max(0, dlg.stopVal - 5)
                }
            }
        }
    }

    footer: DialogButtonBox {
        Button { text: qsTr("Cancel"); onClicked: dlg.close() }
        Button {
            text: qsTr("Apply")
            highlighted: true
            onClicked: dlg.applyAndClose()
        }
    }
}
