import QtQuick
import Components

FocusScope {
    id: accountRoot

    property var navParams: ({})
    property bool scrobbleOn: false

    signal navigateTo(string path, var params)
    signal goBack()

    function readScrobble() {
        var v = appCore.get_setting(moduleRoot.moduleId, "enabled")
        scrobbleOn = (v === true || v === "ON")
    }

    function toggleScrobble() {
        scrobbleOn = !scrobbleOn
        appCore.save_setting(moduleRoot.moduleId, "enabled", scrobbleOn)
    }

    focus: true

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            if (malBackend.phase === "waiting" || malBackend.phase === "error")
                malBackend.cancelConnect()
            goBack()
            event.accepted = true
        } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
            if (malBackend.phase !== "waiting")
                toggleScrobble()
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (malBackend.phase === "error")
                malBackend.connectAccount()
            else if (malBackend.phase === "waiting")
                malBackend.submitCodeFile()
            else if (malBackend.phase === "linked")
                malBackend.logout()
            event.accepted = true
        }
    }

    Component.onCompleted: {
        readScrobble()
        // Credentials already in config and no linked account: the QR is the
        // screen. Don't stop on a "press connect" page.
        if (malBackend.phase === "ready")
            malBackend.connectAccount()
    }

    Connections {
        target: appCore
        function onModuleSettingChanged(moduleId, key) {
            if (moduleId === moduleRoot.moduleId && key === "enabled")
                accountRoot.readScrobble()
        }
    }

    AppBar {
        iconSource: moduleRoot.moduleIcon
        title: moduleRoot.moduleName
        subtitle: malBackend.phase === "linked" ? (malBackend.username || "LINKED") : "ACCOUNT"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: root.sh * 0.08
        anchors.leftMargin: root.sw * 0.08
    }

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        spacing: root.sh * 0.02
        width: root.sw * 0.84

        // The bitmap is built in C++ (white field, black modules, quiet zone).
        // A QML Canvas drops its paint under EGLFS when this row is still
        // zero-size, which left the waiting screen blank on a Pi 4.
        Item {
            id: qrFrame
            readonly property bool showQr: malBackend.phase === "waiting" && malBackend.qrImageUrl !== ""
            width: parent.width
            height: showQr ? root.sh * 0.5 : 0
            visible: showQr

            Image {
                anchors.centerIn: parent
                width: root.sh * 0.5
                height: width
                source: malBackend.qrImageUrl
                fillMode: Image.PreserveAspectFit
                smooth: false
                cache: false
            }
        }

        Text {
            visible: malBackend.phase === "waiting"
            width: parent.width
            text: malBackend.shortUrl
            color: root.accentColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.045
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        Text {
            visible: malBackend.phase === "waiting"
            width: parent.width
            text: "OR SAVE THE CODE AS " + malBackend.codeFileName
            color: root.secondaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.028
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        Text {
            visible: malBackend.phase === "need_client"
            width: parent.width
            text: "ADD CLIENT_ID AND CLIENT_SECRET UNDER MODULES / COM.240MP.MAL IN CONFIG.JSON. REGISTER THIS REDIRECT AT MYANIMELIST.NET/APICONFIG"
            color: root.secondaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.03
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        Text {
            visible: malBackend.phase === "need_client" || malBackend.phase === "error"
            width: parent.width
            text: malBackend.redirectUri
            color: root.accentColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.04
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        Text {
            visible: malBackend.phase === "need_client"
            width: parent.width
            text: malBackend.clientHint
            color: root.tertiaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.028
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        Text {
            visible: malBackend.phase === "linked" || malBackend.phase === "need_client" || malBackend.phase === "error"
            width: parent.width
            text: scrobbleOn ? "SCROBBLE ON" : "SCROBBLE OFF"
            color: root.secondaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.033
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            visible: malBackend.statusLine !== ""
            width: parent.width
            text: malBackend.statusLine
            color: root.accentColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.032
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        Text {
            visible: malBackend.phase === "linked"
            width: parent.width
            text: "WATCHED LIBRARY EPISODES UPDATE YOUR MAL LIST"
            color: root.secondaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.028
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
    }

    Text {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.06
        anchors.leftMargin: root.sw * 0.08
        color: root.tertiaryColor
        font.family: root.globalFont
        font.pixelSize: root.sh * 0.03
        text: {
            var back = root.hints.back + ":BACK"
            var change = root.hints.change + ":SCROBBLE"
            if (malBackend.phase === "error")
                return root.hints.select + ":RETRY    " + change + "    " + back
            if (malBackend.phase === "waiting")
                return root.hints.select + ":READ CODE    " + back
            if (malBackend.phase === "linked")
                return root.hints.select + ":DISCONNECT    " + change + "    " + back
            if (malBackend.phase === "need_client")
                return change + "    " + back
            return back
        }
    }
}
