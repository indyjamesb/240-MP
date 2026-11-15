import QtQuick

// Live PC readings. Black field, light type. Stat updates are not key presses:
// do not call idleTracker.resetActivity() or set mpvActive / scriptActive.
// Block Screensaver (default on) holds the app screensaver via displayHold
// while this view is up. The panels still drift.
//
// Section Swap trades the two halves. The CPU half is CPU through NET, in
// that order. The GPU half is the GPU lines with the frame graph beside
// them. Neither half is rebuilt, so values, graph history, and polling stay
// put, and the drift nudges keep applying on top of whichever half is where.
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

    // Bound to the backend until applySwapTarget assigns it, so reopening the
    // screen paints the half that was already showing and does not flash.
    property bool shownGpuOnTop: pcStatsBackend ? pcStatsBackend.gpuOnTop : false
    property bool pendingGpuOnTop: false
    property bool swapArmed: false
    property int swapEpoch: 0
    property real swapVeil: 0
    // Same insets the two halves used when the CPU block was always on top.
    readonly property real blockOrigin: header.height + root.sh * 0.015
    readonly property real blockGap: root.sh * 0.02

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

    function pcGpuOnTop() {
        return pcStatsBackend ? pcStatsBackend.gpuOnTop : false
    }

    // Snap while the halves are hidden (offline, first paint, swap turned
    // off). Otherwise cover the screen, move the halves under the cover, and
    // uncover once. The interval is minutes, so this is not a flicker loop.
    function applySwapTarget(animate) {
        var next = pcGpuOnTop()
        pendingGpuOnTop = next
        if (!(animate && showStats && next !== shownGpuOnTop)) {
            swapEpoch += 1
            if (swapFade.running)
                swapFade.stop()
            shownGpuOnTop = next
            swapVeil = 0
            return
        }
        if (swapFade.running)
            return
        swapEpoch += 1
        swapFade.epoch = swapEpoch
        swapFade.start()
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
        // Take over from the initial binding before arming the fade, so the
        // open itself is not a transition.
        applySwapTarget(false)
        swapArmed = true
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
                font.pixelSize: root.sh * 0.05
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
            width: parent.width
            height: topCol.implicitHeight
            property real nudgeX: 0
            property real nudgeY: 0
            y: (statsRoot.shownGpuOnTop
                ? statsRoot.blockOrigin + bottomRow.height + statsRoot.blockGap
                : statsRoot.blockOrigin) + nudgeY

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
                        { k: "FAN", v: statsRoot.hw.fanLine || "--" }
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
                            font.pixelSize: root.sh * 0.04
                            width: root.sw * 0.09
                        }

                        Text {
                            id: valueText
                            x: tag.width
                            width: Math.max(0, topCol.width - tag.width)
                            text: modelData.v
                            color: statsRoot.ink
                            font.family: root.globalFont
                            font.capitalization: Font.AllUppercase
                            font.pixelSize: root.sh * 0.048
                            wrapMode: modelData.k === "FAN" ? Text.WordWrap : Text.NoWrap
                            elide: modelData.k === "FAN" ? Text.ElideNone : Text.ElideRight
                            maximumLineCount: modelData.k === "FAN" ? 2 : 1
                        }
                    }
                }

                // One line per drive. Later lines indent to the first drive's name.
                Repeater {
                    model: (statsRoot.hw.driveLines && statsRoot.hw.driveLines.length)
                           ? statsRoot.hw.driveLines : ["--"]
                    delegate: Item {
                        width: topCol.width
                        height: driveValue.implicitHeight

                        Text {
                            visible: index === 0
                            text: "DRV"
                            color: statsRoot.dim
                            font.family: root.globalFont
                            font.capitalization: Font.AllUppercase
                            font.pixelSize: root.sh * 0.04
                            width: root.sw * 0.09
                        }

                        Text {
                            id: driveValue
                            x: root.sw * 0.09
                            width: Math.max(0, parent.width - x)
                            text: modelData
                            color: statsRoot.ink
                            font.family: root.globalFont
                            font.capitalization: Font.AllUppercase
                            font.pixelSize: root.sh * 0.048
                            elide: Text.ElideRight
                        }
                    }
                }

                Repeater {
                    model: [{ k: "NET", v: statsRoot.hw.netLine || "--" }]
                    delegate: Item {
                        width: topCol.width
                        height: netValue.implicitHeight

                        Text {
                            text: modelData.k
                            color: statsRoot.dim
                            font.family: root.globalFont
                            font.capitalization: Font.AllUppercase
                            font.pixelSize: root.sh * 0.04
                            width: root.sw * 0.09
                        }

                        Text {
                            id: netValue
                            x: root.sw * 0.09
                            width: Math.max(0, parent.width - x)
                            text: modelData.v
                            color: statsRoot.ink
                            font.family: root.globalFont
                            font.capitalization: Font.AllUppercase
                            font.pixelSize: root.sh * 0.048
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
            // Not swapped: still stacked under the CPU half, including that
            // half's vertical nudge. Swapped: this half sits under the header
            // and the CPU half follows it. The graph stays on the right.
            y: statsRoot.shownGpuOnTop
               ? statsRoot.blockOrigin
               : topPanel.y + topPanel.height + statsRoot.blockGap
            width: parent.width
            // Tall enough for the labeled GPU lines. The frame pane keeps the
            // height it had before those labels, so the graph does not grow.
            height: Math.max(gpuCol.implicitHeight, frameSizeRuler.implicitHeight)

            Item {
                id: gpuPane
                x: nudgeX
                y: nudgeY
                width: parent.width * 0.5
                height: gpuCol.implicitHeight
                clip: true
                property real nudgeX: 0
                property real nudgeY: 0

                Behavior on nudgeX { NumberAnimation { duration: 12000; easing.type: Easing.InOutSine } }
                Behavior on nudgeY { NumberAnimation { duration: 12000; easing.type: Easing.InOutSine } }

                Column {
                    id: gpuCol
                    width: parent.width
                    spacing: root.sh * 0.012

                    Text {
                        width: parent.width
                        text: statsRoot.hw.gpuShort || statsRoot.hw.gpuName || "GPU"
                        color: statsRoot.dim
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.046
                        elide: Text.ElideRight
                    }
                    Repeater {
                        // VRAM is last. No memory-clock line, and no delegate after it.
                        model: [
                            statsRoot.hw.gpuTempLine || "",
                            statsRoot.hw.gpuHotLine || "",
                            statsRoot.hw.gpuLoadLine || "",
                            statsRoot.hw.gpuClockLine || "",
                            statsRoot.hw.gpuPowerLine || "",
                            statsRoot.hw.gpuVramLine || ""
                        ]
                        delegate: Text {
                            width: gpuCol.width
                            visible: modelData !== ""
                            height: visible ? implicitHeight : 0
                            text: modelData
                            color: statsRoot.ink
                            font.family: root.globalFont
                            font.capitalization: Font.AllUppercase
                            font.pixelSize: root.sh * 0.046
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            // Same four line-heights the frame block used before the GPU
            // labels, so that block's size and the graph inside it stay put.
            Column {
                id: frameSizeRuler
                opacity: 0
                enabled: false
                width: 10
                spacing: root.sh * 0.008
                Text { font.family: root.globalFont; font.pixelSize: root.sh * 0.032; text: "X" }
                Text { font.family: root.globalFont; font.pixelSize: root.sh * 0.04; text: "X" }
                Text { font.family: root.globalFont; font.pixelSize: root.sh * 0.04; text: "X" }
                Text { font.family: root.globalFont; font.pixelSize: root.sh * 0.036; text: "X" }
            }

            Item {
                id: framePane
                // Height locked to the pre-label GPU block so this pane,
                // the FPS line, and the graph keep their 7e4f930 size.
                x: parent.width * 0.52 + nudgeX
                y: 0
                width: parent.width * 0.48
                height: frameSizeRuler.implicitHeight
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
                        // Fixed 0–33.3 ms, the same ceiling as a 30 fps frame.
                        // Anything slower clips to the top of the box.
                        var lo = 0
                        var hi = 33.3
                        var span = hi - lo
                        var sum = 0
                        var counted = 0
                        var plot = []
                        for (var s = 0; s < n; ++s) {
                            var raw = Number(samples[s])
                            if (raw !== raw)
                                raw = 0
                            sum += raw
                            counted += 1
                            var drawn = raw
                            if (s > 0 && s < n - 1) {
                                var prev = Number(samples[s - 1])
                                var next = Number(samples[s + 1])
                                if (prev === prev && next === next) {
                                    var neighbor = (prev + next) * 0.5
                                    // Quiet jitter only. A real hitch stays put.
                                    if (Math.abs(raw - neighbor) <= 2)
                                        drawn = raw * 0.65 + neighbor * 0.35
                                }
                            }
                            if (drawn < lo)
                                drawn = lo
                            else if (drawn > hi)
                                drawn = hi
                            plot.push(drawn)
                        }
                        if (counted > 0) {
                            var avg = sum / counted
                            if (avg < lo)
                                avg = lo
                            else if (avg > hi)
                                avg = hi
                            var ay = (height - 1) - ((avg - lo) / span) * (height - 2)
                            ctx.strokeStyle = "rgba(200,200,200,0.28)"
                            ctx.lineWidth = 1
                            ctx.beginPath()
                            ctx.moveTo(0, ay)
                            ctx.lineTo(width - 1, ay)
                            ctx.stroke()
                        }
                        ctx.strokeStyle = statsRoot.trace
                        ctx.lineWidth = Math.max(1, root.sh * 0.004)
                        ctx.beginPath()
                        for (var p = 0; p < n; ++p) {
                            var px = (p / (n - 1)) * (width - 1)
                            var py = (height - 1) - ((plot[p] - lo) / span) * (height - 2)
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
        function onGpuOnTopChanged() { statsRoot.applySwapTarget(statsRoot.swapArmed) }
    }

    SequentialAnimation {
        id: swapFade
        property int epoch: 0

        NumberAnimation {
            target: statsRoot
            property: "swapVeil"
            to: 1
            duration: 90
            easing.type: Easing.Linear
        }
        ScriptAction {
            script: statsRoot.shownGpuOnTop = statsRoot.pendingGpuOnTop
        }
        NumberAnimation {
            target: statsRoot
            property: "swapVeil"
            to: 0
            duration: 150
            easing.type: Easing.Linear
        }
        onFinished: {
            if (swapFade.epoch !== statsRoot.swapEpoch)
                return
            statsRoot.swapVeil = 0
            if (statsRoot.swapArmed && statsRoot.pcGpuOnTop() !== statsRoot.shownGpuOnTop)
                Qt.callLater(function() { statsRoot.applySwapTarget(true) })
        }
    }

    // One full-screen black cover. The halves themselves stay at full opacity,
    // so the move does not push the text or the graph canvas into an
    // offscreen layer. Cheap on a Pi 4, and it hides the position snap.
    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: statsRoot.swapVeil
        visible: opacity > 0
        enabled: false
        z: 5
    }
}
