import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: window
    required property QtObject appController
    width: 1100
    height: 700
    minimumWidth: 760
    minimumHeight: 520
    visible: true
    color: "#090d14"
    title: "ShareMe"

    Loader {
        anchors.fill: parent
        active: window.appController.activeController !== null
        sourceComponent: LegacyCallView {
            controller: window.appController.activeController
        }
    }

    Item {
        anchors.fill: parent
        visible: window.appController.activeController === null

        Label {
            anchors.centerIn: parent
            text: "ShareMe"
            color: "white"
            font.pixelSize: 32
            font.bold: true
        }
    }
}
