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

    ShareMeTheme { id: theme }

    HomePage {
        anchors.fill: parent
        visible: window.appController.page === "home"
        appController: window.appController
        onOpenSettings: settingsDialog.open()
        onOpenHelp: helpDialog.open()
    }

    PreflightPage {
        anchors.fill: parent
        visible: window.appController.page === "preflight"
        appController: window.appController
    }

    Loader {
        anchors.fill: parent
        active: window.appController.page === "calling"
                && window.appController.activeController !== null
        sourceComponent: CallPage {
            appController: window.appController
            controller: window.appController.activeController
        }
    }

    RecoveryDialog {
        anchors.fill: parent
        visible: window.appController.page === "result"
        appController: window.appController
    }

    SettingsDialog {
        id: settingsDialog
        appController: window.appController
    }

    HelpDialog { id: helpDialog }
}
