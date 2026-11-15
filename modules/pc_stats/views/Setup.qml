import QtQuick

FocusScope {
    id: setupRoot

    property var navParams: ({})
    signal goBack()

    focus: true

    // A remote cannot type an address. The key lives in config.json, same as
    // the other one-time module values. The default, when the key is absent,
    // is 10.0.0.44 — this screen is what you get after clearing it.
    readonly property string path: pcStatsBackend ? pcStatsBackend.configPath : ""

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace
                || event.key === Qt.Key_Back) {
            goBack()
            event.accepted = true
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
    }

    Column {
        anchors.centerIn: parent
        width: root.sw * 0.78
        spacing: root.sh * 0.03

        Text {
            text: "NO PC ADDRESS"
            color: "#E6E6E6"
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
            wrapMode: Text.WordWrap
            font.pixelSize: root.sh * 0.05
        }

        Text {
            text: "Set host under modules / com.240mp.pc_stats in config.json. Leave the key out to use 10.0.0.44. lhm_port defaults to 8085 and frames_port to 8086."
            color: "#8A8A8A"
            font.family: root.globalFont
            font.capitalization: Font.AllUppercase
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
            wrapMode: Text.WordWrap
            font.pixelSize: root.sh * 0.03
        }

        Text {
            text: setupRoot.path
            color: "#8A8A8A"
            font.family: root.globalFont
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
            wrapMode: Text.WordWrap
            font.pixelSize: root.sh * 0.026
        }
    }

    Text {
        text: root.hints.back + ":BACK"
        color: "#8A8A8A"
        font.family: root.globalFont
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.bottomMargin: root.sh * 0.08
        anchors.leftMargin: root.sw * 0.08
        font.pixelSize: root.sh * 0.033
    }
}
