import QtQuick
import Components

FocusScope {
    id: accountRoot

    property var navParams: ({})
    property bool scrobbleOn: false
    property int focusIndex: 0
    property var optionRows: []

    signal navigateTo(string path, var params)
    signal goBack()

    function readScrobble() {
        var v = appCore.get_setting(moduleRoot.moduleId, "enabled")
        scrobbleOn = (v === true || v === "ON")
    }

    function toggleScrobble() {
        scrobbleOn = !scrobbleOn
        appCore.save_setting(moduleRoot.moduleId, "enabled", scrobbleOn)
        rebuildRows()
    }

    function rebuildRows() {
        var rows = [{
            kind: "track",
            label: "TRACK PROGRESS AUTOMATICALLY: " + (scrobbleOn ? "ON" : "OFF")
        }]
        var libs = malBackend.libraries || []
        for (var i = 0; i < libs.length; i++) {
            rows.push({
                kind: "library",
                id: libs[i].id,
                enabled: libs[i].enabled,
                label: libs[i].title + ": " + (libs[i].enabled ? "ON" : "OFF")
            })
        }
        if (malBackend.phase === "linked")
            rows.push({ kind: "disconnect", label: "DISCONNECT" })
        optionRows = rows
        if (focusIndex >= rows.length)
            focusIndex = Math.max(0, rows.length - 1)
    }

    function activateRow() {
        var row = optionRows[focusIndex]
        if (!row)
            return
        if (row.kind === "library") {
            malBackend.setLibraryTracked(row.id, !row.enabled)
            return
        }
        if (row.kind === "disconnect") {
            disconnectChoice.open()
            return
        }
        if (malBackend.phase === "error")
            malBackend.connectAccount()
        else if (malBackend.phase === "waiting")
            malBackend.submitCodeFile()
        else
            toggleScrobble()
    }

    focus: true

    Keys.onPressed: function(event) {
        if (disconnectChoice.visible)
            return
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace || event.key === Qt.Key_Back) {
            if (malBackend.phase === "waiting" || malBackend.phase === "error")
                malBackend.cancelConnect()
            goBack()
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            focusIndex = focusIndex > 0 ? focusIndex - 1 : optionRows.length - 1
            event.accepted = true
        } else if (event.key === Qt.Key_Down) {
            focusIndex = focusIndex < optionRows.length - 1 ? focusIndex + 1 : 0
            event.accepted = true
        } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
            var row = optionRows[focusIndex]
            if (row && row.kind === "library")
                malBackend.setLibraryTracked(row.id, !row.enabled)
            else if (!row || row.kind === "track")
                toggleScrobble()
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            activateRow()
            event.accepted = true
        }
    }

    Component.onCompleted: {
        readScrobble()
        rebuildRows()
        malBackend.refreshLibraries()
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

    Connections {
        target: malBackend
        function onLibrariesChanged() { accountRoot.rebuildRows() }
        function onUpdated() { accountRoot.rebuildRows() }
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

        ListView {
            id: optionList
            width: parent.width
            height: Math.min(contentHeight, malBackend.phase === "waiting" ? root.sh * 0.22 : root.sh * 0.4)
            clip: true
            interactive: false
            model: accountRoot.optionRows
            currentIndex: accountRoot.focusIndex
            onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)

            delegate: Item {
                width: optionList.width
                height: root.sh * 0.05
                Rectangle {
                    anchors.fill: parent
                    color: index === accountRoot.focusIndex ? root.accentColor : "transparent"
                }
                Text {
                    anchors.fill: parent
                    anchors.leftMargin: root.sw * 0.012
                    anchors.rightMargin: root.sw * 0.012
                    verticalAlignment: Text.AlignVCenter
                    text: modelData.label
                    color: index === accountRoot.focusIndex ? root.surfaceColor : root.primaryColor
                    font.family: root.globalFont
                    font.pixelSize: root.sh * 0.028
                    elide: Text.ElideRight
                }
            }
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
            var change = root.hints.change + ":TRACK PROGRESS"
            if (malBackend.phase === "error")
                return root.hints.select + ":RETRY    " + change + "    " + back
            if (malBackend.phase === "waiting")
                return root.hints.select + ":READ CODE    " + back
            if (malBackend.phase === "linked")
                return root.hints.select + ":SELECT    " + change + "    " + back
            if (malBackend.phase === "need_client")
                return change + "    " + back
            return back
        }
    }

    // Cancel is first so it is the highlighted default. A stray Enter on the
    // linked screen must not drop the saved account.
    ChoiceOverlay {
        id: disconnectChoice
        anchors.fill: parent
        promptText: "Disconnect MyAnimeList?"
        subtitleText: malBackend.username
        choices: [
            { label: "Cancel", action: "cancel" },
            { label: "Disconnect", action: "disconnect" }
        ]
        onClosed: accountRoot.forceActiveFocus()
        onActivated: function(action) {
            if (action === "disconnect")
                malBackend.logout()
        }
    }
}
