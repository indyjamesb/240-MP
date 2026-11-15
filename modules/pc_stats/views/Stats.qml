import QtQuick

// Live PC readings. The background stays black and the type stays light so a
// CRT is not holding a bright field. Numbers update from the network thread's
// results; those updates are not key presses, so they must not call
// idleTracker.resetActivity() or set mpvActive. The app screensaver in
// Main.qml blanks this screen on the same idle timeout as every other view.
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

    property int driftPhase: 0
    readonly property var driftShifts: [
        { bx: 0,    by: 0,    lx: 0,    ly: 0,    rx: 0,    ry: 0 },
        { bx: 0.7,  by: 0.5,  lx: 0.35, ly: -0.2, rx: -0.4, ry: 0.45 },
        { bx: -0.55, by: 0.85, lx: -0.3, ly: 0.35, rx: 0.45, ry: -0.25 },
        { bx: 0.4,  by: -0.7, lx: 0.2,  ly: 0.4,  rx: -0.35, ry: -0.4 },
        { bx: -0.8, by: -0.35, lx: -0.25, ly: -0.35, rx: 0.3, ry: 0.3 }
    ]

    function advanceDrift() {
        driftPhase = (driftPhase + 1) % driftShifts.length
        var s = driftShifts[driftPhase]
        board.shiftX = s.bx * root.sw * 0.01
        board.shiftY = s.by * root.sh * 0.012
        leftPanel.nudgeX = s.lx * root.sw * 0.008
        leftPanel.nudgeY = s.ly * root.sh * 0.01
        rightPanel.nudgeX = s.rx * root.sw * 0.008
        rightPanel.nudgeY = s.ry * root.sh * 0.01
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
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
    }

    Item {
        id: board
        x: root.sw * 0.06 + shiftX
        y: root.sh * 0.05 + shiftY
        width: root.sw * 0.88
        height: root.sh * 0.9
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
                font.pixelSize: root.sh * 0.048
            }

            Text {
                text: statsRoot.hw.machine || (pcStatsBackend ? pcStatsBackend.host : "")
                color: statsRoot.dim
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.032
                width: Math.max(0, board.width * 0.62)
                elide: Text.ElideRight
            }
        }

        Column {
            id: message
            visible: !statsRoot.showStats
            anchors.centerIn: parent
            width: parent.width * 0.8
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
                wrapMode: Text.WordWrap
                text: (pcStatsBackend && pcStatsBackend.offline) ? "WILL RETRY" : ""
                color: statsRoot.dim
                font.family: root.globalFont
                font.capitalization: Font.AllUppercase
                font.pixelSize: root.sh * 0.032
            }
        }

        Item {
            id: leftPanel
            visible: statsRoot.showStats
            x: nudgeX
            y: header.height + root.sh * 0.03 + nudgeY
            width: board.width * 0.5
            height: board.height - y - root.sh * 0.06
            property real nudgeX: 0
            property real nudgeY: 0

            Behavior on nudgeX { NumberAnimation { duration: 12000; easing.type: Easing.InOutSine } }
            Behavior on nudgeY { NumberAnimation { duration: 12000; easing.type: Easing.InOutSine } }

            Column {
                width: parent.width
                spacing: root.sh * 0.016

                Text {
                    width: parent.width
                    text: statsRoot.hw.cpuName || "CPU"
                    color: statsRoot.dim
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.03
                    elide: Text.ElideRight
                }

                Repeater {
                    model: [
                        { k: "CPU", v: statsRoot.hw.cpuLine || "--" },
                        { k: "RAM", v: statsRoot.hw.ramLine || "--" },
                        { k: "FAN", v: statsRoot.hw.fanLine || "--" },
                        { k: "DRIVE", v: statsRoot.hw.driveLine || "--" },
                        { k: "NET", v: statsRoot.hw.netLine || "--" }
                    ]
                    delegate: Item {
                        width: leftPanel.width
                        height: valueText.implicitHeight

                        Text {
                            id: tag
                            text: modelData.k
                            color: statsRoot.dim
                            font.family: root.globalFont
                            font.capitalization: Font.AllUppercase
                            font.pixelSize: root.sh * 0.032
                            width: root.sw * 0.09
                        }

                        Text {
                            id: valueText
                            x: tag.width
                            width: Math.max(0, leftPanel.width - tag.width)
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
            id: rightPanel
            visible: statsRoot.showStats
            x: board.width * 0.52 + nudgeX
            y: header.height + root.sh * 0.03 + nudgeY
            width: board.width * 0.48
            height: board.height - y - root.sh * 0.06
            property real nudgeX: 0
            property real nudgeY: 0

            Behavior on nudgeX { NumberAnimation { duration: 12000; easing.type: Easing.InOutSine } }
            Behavior on nudgeY { NumberAnimation { duration: 12000; easing.type: Easing.InOutSine } }

            Column {
                id: rightCol
                width: parent.width
                spacing: root.sh * 0.012

                Text {
                    width: parent.width
                    text: statsRoot.hw.gpuName || "GPU"
                    color: statsRoot.dim
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.03
                    elide: Text.ElideRight
                }

                Item {
                    width: parent.width
                    height: gpuValue.implicitHeight

                    Text {
                        id: gpuTag
                        text: "GPU"
                        color: statsRoot.dim
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.032
                        width: root.sw * 0.09
                    }

                    Text {
                        id: gpuValue
                        x: gpuTag.width
                        width: Math.max(0, parent.width - gpuTag.width)
                        text: statsRoot.hw.gpuTempLine || "--"
                        color: statsRoot.ink
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.048
                        elide: Text.ElideRight
                    }
                }
                Text {
                    width: parent.width
                    text: statsRoot.hw.gpuLoadLine || "--"
                    color: statsRoot.ink
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.046
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: statsRoot.hw.gpuMemLine || "--"
                    color: statsRoot.ink
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.04
                    elide: Text.ElideRight
                }

                Item { width: 1; height: root.sh * 0.02 }

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

                Row {
                    spacing: root.sw * 0.02
                    visible: (statsRoot.frames.state || "") === "live"
                    height: visible ? implicitHeight : 0

                    Text {
                        text: (statsRoot.frames.fpsAvg || "--") + " AVG"
                        color: statsRoot.ink
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.055
                    }
                    Text {
                        text: (statsRoot.frames.fpsLow || "--") + " 1% LOW"
                        color: statsRoot.ink
                        font.family: root.globalFont
                        font.capitalization: Font.AllUppercase
                        font.pixelSize: root.sh * 0.055
                    }
                }

                Text {
                    width: parent.width
                    visible: (statsRoot.frames.state || "") === "waiting"
                    height: visible ? implicitHeight : 0
                    text: "WAITING FOR FRAMES"
                    color: statsRoot.dim
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.036
                }

                Text {
                    text: "FRAME MS"
                    color: statsRoot.dim
                    font.family: root.globalFont
                    font.capitalization: Font.AllUppercase
                    font.pixelSize: root.sh * 0.026
                }
            }

            Canvas {
                id: graph
                x: 0
                width: parent.width
                y: rightCol.implicitHeight + root.sh * 0.008
                height: Math.max(root.sh * 0.12, parent.height - y)
                antialiasing: false

                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    var samples = statsRoot.frames.frameMs || []
                    var n = samples.length
                    if (n < 2 || width < 2 || height < 2)
                        return
                    var yMax = 33.3
                    for (var i = 0; i < n; ++i) {
                        var sample = Number(samples[i])
                        if (sample > yMax)
                            yMax = sample
                    }
                    ctx.strokeStyle = statsRoot.trace
                    ctx.lineWidth = Math.max(1, root.sh * 0.003)
                    ctx.beginPath()
                    for (var p = 0; p < n; ++p) {
                        var px = (n === 1) ? 0 : (p / (n - 1)) * (width - 1)
                        var py = height - (Number(samples[p]) / yMax) * (height - 1)
                        if (p === 0)
                            ctx.moveTo(px, py)
                        else
                            ctx.lineTo(px, py)
                    }
                    ctx.stroke()
                }
            }
        }

        Text {
            text: root.hints.back + ":BACK"
            color: statsRoot.dim
            font.family: root.globalFont
            font.pixelSize: root.sh * 0.03
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
        function onHardwareChanged() { graph.requestPaint() }
    }

}
