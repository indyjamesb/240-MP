import QtQuick
import Components

FocusScope {
    id: accountRoot

    property var navParams: ({})

    signal navigateTo(string path, var params)
    signal goBack()

    focus: true

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            if (malBackend.phase === "waiting")
                malBackend.cancelConnect()
            else
                goBack()
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (malBackend.phase === "ready" || malBackend.phase === "error")
                malBackend.connectAccount()
            else if (malBackend.phase === "waiting")
                malBackend.submitCodeFile()
            else if (malBackend.phase === "linked")
                malBackend.logout()
            event.accepted = true
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

        // Black modules on white. Theme colors would break phone cameras,
        // and the code is sized to about half the screen so a 240p CRT can read it.
        Item {
            width: parent.width
            height: qrCanvas.visible ? qrCanvas.height : 0
            visible: qrCanvas.visible
            Canvas {
            id: qrCanvas
            visible: malBackend.phase === "waiting" && malBackend.qrSide > 0
            width: root.sh * 0.5
            height: width
            anchors.horizontalCenter: parent.horizontalCenter
            onPaint: {
                var ctx = getContext("2d")
                ctx.fillStyle = "white"
                ctx.fillRect(0, 0, width, height)
                var n = malBackend.qrSide
                if (n <= 0)
                    return
                var cell = width / n
                ctx.fillStyle = "black"
                var bits = malBackend.qrBits
                for (var y = 0; y < n; y++) {
                    for (var x = 0; x < n; x++) {
                        if (bits.charAt(y * n + x) === "1")
                            ctx.fillRect(Math.floor(x * cell), Math.floor(y * cell),
                                         Math.ceil(cell), Math.ceil(cell))
                    }
                }
            }
            onVisibleChanged: if (visible) requestPaint()
            Connections {
                target: malBackend
                function onUpdated() { qrCanvas.requestPaint() }
            }
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
            visible: malBackend.phase === "ready" || malBackend.phase === "need_client" || malBackend.phase === "error"
            width: parent.width
            text: malBackend.phase === "need_client"
                  ? "PUT CLIENT_ID IN CONFIG.JSON UNDER MODULES / COM.240MP.MAL"
                  : "REGISTER THIS REDIRECT AT MYANIMELIST.NET/APICONFIG"
            color: root.secondaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.03
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        Text {
            visible: malBackend.phase === "ready" || malBackend.phase === "error"
            width: parent.width
            text: malBackend.redirectUri
            color: root.accentColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.04
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }

        Text {
            visible: malBackend.phase !== "waiting"
            width: parent.width
            text: malBackend.clientHint
            color: root.tertiaryColor
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.028
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
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
            if (malBackend.phase === "ready" || malBackend.phase === "error")
                return root.hints.select + ":CONNECT    " + back
            if (malBackend.phase === "waiting")
                return root.hints.select + ":READ CODE    " + back
            if (malBackend.phase === "linked")
                return root.hints.select + ":DISCONNECT    " + back
            return back
        }
    }
}
