import QtQuick

// Live PC readings. Black field, light type. Stat updates are not key presses:
// do not call idleTracker.resetActivity() or set mpvActive / scriptActive.
// Block Screensaver (default on) holds the app screensaver via displayHold
// while this view is up. The panels still drift.
FocusScope {
    id: statsRoot

    property var navParams: ({})
    signal goBack()

    readonly property var hw: (pcStatsBackend && pcStatsBackend.hardware) ? pcStatsBackend.hardware : ({})
    readonly property var frames: (pcStatsBackend && pcStatsBackend.frames) ? pcStatsBackend.frames : ({})
    readonly property bool showStats: pcStatsBackend && !pcStatsBackend.connecting && !pcStatsBackend.offline
    readonly property color ink: "#E6E6E6"
    readonly property color dim: "#8A8A8A"
    readonly property color trace: "#C8C8C8"
    readonly property string moduleId: "com.240mp.pc_stats"

    property int driftPhase: 0
    readonly property var driftShifts: [
        { bx: 0,    by: 0,    tx: 0,    ty: 0,    gx: 0,    gy: 0 },
        { bx: 0.55, by: 0.4,  tx: 0.3,  ty: -0.2, gx: -0.35, gy: 0.3 },
        { bx: -0.45, by: 0.6, tx: -0.25, ty: 0.25, gx: 0.3, gy: -0.2 },
        { bx: 0.3,  by: -0.5, tx: 0.15, ty: 0.3,  gx: -0.25, gy: -0.3 },
        { bx: -0.6, by: -0.25, tx: -0.2, ty: -0.25, gx: 0.2, gy: 0.25 }
    ]

    function advanceDrift() {
        driftPhase = (driftPhase + 1) % driftShifts.length
        var s = driftShifts[driftPhase]
        // Stay inside the 8% safe margin: the board is already inset, and
        // these shifts are only a few pixels on a 480-line CRT.
        board.shiftX = s.bx * root.sw * 0.006
        board.shiftY = s.by * root.sh * 0.008
        topPanel.nudgeX = s.tx * root.sw * 0.004
        topPanel.nudgeY = s.ty * root.sh * 0.004
        gpuPane.nudgeX = s.gx * root.sw * 0.004
        gpuPane.nudgeY = s.gy * root.sh * 0.004
        framePane.nudgeX = -s.gx * root.sw * 0.003
        framePane.nudgeY = -s.gy * root.sh * 0.003
    }

    function blockScreensaver() {
        var cfg = appCore ? appCore.get_settings() : ({})
        var mod = (cfg.modules && cfg.modules[moduleId]) || ({})
        var v = mod.suppress_screensaver
        if (v === undefined || v === null || v === "")
            return true
        return v === true || v === "ON" || v === "on"
    }

    function applyScreensaverHold() {
        if (!idleTracker)
            return
        var hold = blockScreensaver()
        idleTracker.displayHold = hold
        if (hold && root.screenSaverActive)
            root.dismissScreenSaver()
    }

    focus: true

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace
                || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
    }

    Component.onCompleted: {
        if (pcStatsBackend)
            pcStatsBackend.start()
        applyScreensaverHold()
    }

    Component.onDestruction: {
        if (idleTracker)
            idleTracker.displayHold = false
    }

    Connections {
        target: appCore
        function onModuleSettingChanged(moduleId, key, value) {
            if (moduleId === statsRoot.moduleId && key === "suppress_screensaver")
                statsRoot.applyScreensaverHold()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
    }

    Item {
        id: board
        // ~8% safe area. Drift above stays inside about 6%.
        x: root.sw * 0.08 + shiftX
        y: root.sh * 0.07 + shiftY
        width: root.sw * 0.84
        height: root.sh * 0.86
        property real shiftX: 0
        property real shiftY: 0

        Behavior on shiftX { NumberAnimation { duration: 12000; easing.type: Easing.InOutSine } }
        Behavior on shiftY { NumberAnimation { duration: 12000; easing.type: Easing.InOutSine } }

        Row {
            id: header
            width: parent.width
            spacing: root.sw * 0.02

            Text {
                text: "PC STATS"
                color: statsRoot.ink
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.042
            }

            Text {
                text: statsRoot.hw.machine || (pcStatsBackend ? pcStatsBackend.host : "")
                color: statsRoot.dim
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.03
                width: Math.max(0, board.width * 0.55)
                elide: Text.ElideRight
            }
        }

        Column {
            id: message
            visible: !statsRoot.showStats
            anchors.centerIn: parent
            width: parent.width * 0.9
            spacing: root.sh * 0.025

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: (pcStatsBackend && pcStatsBackend.offline) ? "PC OFFLINE" : "CONNECTING"
                color: statsRoot.ink
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.07
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: pcStatsBackend ? pcStatsBackend.host : ""
                color: statsRoot.dim
                font.family: root.globalFont
                font.pixelSize: root.sh * 0.04
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: (pcStatsBackend && pcStatsBackend.offline) ? "WILL RETRY" : ""
                color: statsRoot.dim
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.032
            }
        }

        Item {
            id: topPanel
            visible: statsRoot.showStats
            x: nudgeX
            y: header.height + root.sh * 0.015 + nudgeY
            width: parent.width
            height: topCol.implicitHeight
            property real nudgeX: 0
            property real nudgeY: 0

            Behavior on nudgeX { NumberAnimation { duration: 12000; easing.type: Easing.InOutSine } }
            Behavior on nudgeY { NumberAnimation { duration: 12000; easing.type: Easing.InOutSine } }

            Column {
                id: topCol
                width: parent.width
                spacing: root.sh * 0.008

                Repeater {
                    model: [
                        { k: "CPU", v: statsRoot.hw.cpuLine || "--" },
                        { k: "RAM", v: statsRoot.hw.ramLine || "--" },
                        { k: "FAN", v: statsRoot.hw.fanLine || "--" },
                        { k: "DRV", v: statsRoot.hw.driveLine || "--" },
                        { k: "NET", v: statsRoot.hw.netLine || "--" }
                    ]
                    delegate: Item {
                        width: topCol.width
                        height: valueText.implicitHeight

                        Text {
                            id: tag
                            text: modelData.k
                            color: statsRoot.dim
                            font.family: root.globalFont
                            font.capitalization: Font.AllUppercase
                            font.pixelSize: root.sh * 0.032
                            width: root.sw * 0.07
                        }

                        Text {
                            id: valueText
                            x: tag.width
                            width: Math.max(0, topCol.width - tag.width)
                            text: modelData.v
                            color: statsRoot.ink
                            font.family: root.globalFont
                            font.capitalization: Font.AllUppercase
                            font.pixelSize: root.sh * 0.04
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        Item {
            id: bottomRow
            visible: statsRoot.showStats
            x: 0
            y: topPanel.y + topPanel.height + root.sh * 0.02
            width: parent.width
            // As tall as the GPU text, so the frame block cannot hang below it.
            height: gpuCol.implicitHeight

            Item {
                id: gpuPane
                x: nudgeX
                y: nudgeY
                width: parent.width * 0.5
                height: gpuCol.implicitHeight
                property real nudgeX: 0
                property real nudgeY: 0

                Behavior on nudgeX { NumberAnimation { duration: 12000; easing.type: Easing.InOutSine } }
                Behavior on nudgeY { NumberAnimation { duration: 12000; easing.type: Easing.InOutSine } }

                Column {
                    id: gpuCol
                    width: parent.width
                    spacing: root.sh * 0.008

                    Text {
                        width: parent.width
                        text: statsRoot.hw.gpuShort || statsRoot.hw.gpuName || "GPU"
                        color: statsRoot.dim
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.032
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: statsRoot.hw.gpuTempLine || "--"
                        color: statsRoot.ink
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.04
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: statsRoot.hw.gpuLoadLine || "--"
                        color: statsRoot.ink
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.04
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: statsRoot.hw.gpuMemLine || "--"
                        color: statsRoot.ink
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.036
                        elide: Text.ElideRight
                    }
                }
            }

            Item {
                id: framePane
                // Same height as the NVIDIA block, not the leftover screen.
                x: parent.width * 0.52 + nudgeX
                y: 0
                width: parent.width * 0.48
                height: gpuCol.implicitHeight
                property real nudgeX: 0
                property real nudgeY: 0

                Behavior on nudgeX { NumberAnimation { duration: 12000; easing.type: Easing.InOutSine } }
                Behavior on nudgeY { NumberAnimation { duration: 12000; easing.type: Easing.InOutSine } }

                Column {
                    id: frameHead
                    width: parent.width
                    spacing: root.sh * 0.004

                    Text {
                        width: parent.width
                        text: {
                            var state = statsRoot.frames.state || ""
                            if (state === "live" || state === "waiting")
                                return statsRoot.frames.process || ""
                            if (state === "unavailable")
                                return "NO FRAME DATA"
                            return "NO GAME"
                        }
                        color: statsRoot.dim
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.03
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        visible: (statsRoot.frames.state || "") === "live"
                        height: visible ? implicitHeight : 0
                        text: (statsRoot.frames.fpsAvg || "--") + " AVG  "
                              + (statsRoot.frames.fpsLow || "--") + " 1% LOW"
                        color: statsRoot.ink
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.04
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        visible: (statsRoot.frames.state || "") === "waiting"
                        height: visible ? implicitHeight : 0
                        text: "WAITING"
                        color: statsRoot.dim
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.032
                    }
                }

                Canvas {
                    id: graph
                    x: 0
                    y: frameHead.implicitHeight + root.sh * 0.006
                    width: parent.width
                    height: Math.max(0, parent.height - y)
                    antialiasing: false

                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        var samples = statsRoot.frames.frameMs || []
                        var n = samples.length
                        if (n < 2 || width < 2 || height < 2)
                            return
                        var lo = Number(statsRoot.frames.frameLo)
                        var hi = Number(statsRoot.frames.frameHi)
                        if (!(hi > lo)) {
                            lo = Number(samples[0])
                            hi = lo
                            for (var i = 1; i < n; ++i) {
                                var sample = Number(samples[i])
                                if (sample < lo) lo = sample
                                if (sample > hi) hi = sample
                            }
                            var pad = Math.max((hi - lo) * 0.2, Math.max(hi * 0.05, 0.4))
                            lo = Math.max(0, lo - pad)
                            hi = hi + pad
                        }
                        var span = hi - lo
                        if (span <= 0)
                            return
                        ctx.strokeStyle = statsRoot.trace
                        ctx.lineWidth = Math.max(1, root.sh * 0.004)
                        ctx.beginPath()
                        for (var p = 0; p < n; ++p) {
                            var px = (p / (n - 1)) * (width - 1)
                            var py = (height - 1) - ((Number(samples[p]) - lo) / span) * (height - 2)
                            if (p === 0)
                                ctx.moveTo(px, py)
                            else
                                ctx.lineTo(px, py)
                        }
                        ctx.stroke()
                    }
                }
            }
        }

        Text {
            text: root.hints.back + ":BACK"
            color: statsRoot.dim
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.028
            anchors.left: parent.left
            anchors.bottom: parent.bottom
        }
    }

    Timer {
        id: driftTimer
        interval: pcStatsBackend ? pcStatsBackend.driftIntervalMs : 180000
        repeat: true
        running: true
        onTriggered: statsRoot.advanceDrift()
        onIntervalChanged: if (running) restart()
    }

    Connections {
        target: pcStatsBackend
        function onFramesChanged() { graph.requestPaint() }
    }
}
