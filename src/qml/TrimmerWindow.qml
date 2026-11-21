import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Drift

// Dedicated VEGAS-style Trimmer window (Alt+Shift+T) for auditioning media bin assets,
// setting In/Out points (I/O), dual-shuttle scrubbing (J/K/L), and inserting or overwriting
// subclips directly onto the timeline at the playhead.
Window {
    id: root

    property int assetIndex: 0
    property real inSeconds: 0.0
    property real outSeconds: 0.0
    property bool loopRange: false
    property string streamMode: "both" // "both" | "video" | "audio"

    readonly property var currentAsset: assetIndex >= 0 && assetIndex < AssetLibrary.count
                                        ? AssetLibrary.assetAt(assetIndex) : ({})
    readonly property real duration: Math.max(0.0, EditorState.assetPreview.duration || currentAsset.durationSeconds || 0.0)
    readonly property real position: EditorState.assetPreview.position || 0.0

    function openTrimmer(index) {
        if (AssetLibrary.count > 0) {
            const target = (index !== undefined && index >= 0 && index < AssetLibrary.count) ? index : Math.min(Math.max(0, assetIndex), AssetLibrary.count - 1)
            loadAsset(target)
        }
        root.show()
        root.raise()
        root.requestActivate()
    }

    function loadAsset(index) {
        if (index < 0 || index >= AssetLibrary.count)
            return
        assetIndex = index
        EditorState.assetPreview.windowOpen = true
        EditorState.assetPreview.begin(index)
        inSeconds = 0.0
        outSeconds = Math.max(0.0, EditorState.assetPreview.duration || (AssetLibrary.assetAt(index).durationSeconds || 0.0))
    }

    function markIn() {
        inSeconds = Math.max(0.0, Math.min(position, duration))
        if (outSeconds <= inSeconds)
            outSeconds = duration
    }

    function markOut() {
        outSeconds = Math.max(0.0, Math.min(position, duration))
        if (outSeconds <= inSeconds)
            inSeconds = 0.0
    }

    function commitToTimeline(overwrite) {
        if (assetIndex < 0 || assetIndex >= AssetLibrary.count)
            return
        const effOut = outSeconds > inSeconds ? outSeconds : duration
        if (effOut <= inSeconds)
            return
        EditorState.assetPreview.pause()
        if (EditorState.insertSubclipFromTrimmer(assetIndex, inSeconds, effOut, overwrite, streamMode)) {
            Toasts.success(overwrite ? qsTr("Subclip overwritten to timeline") : qsTr("Subclip inserted to timeline"))
        }
    }

    function formatTime(sec) {
        const s = Math.max(0, sec)
        const mins = Math.floor(s / 60)
        const rem = (s - mins * 60).toFixed(2)
        return (mins < 10 ? "0" : "") + mins + ":" + (rem.length < 5 ? "0" : "") + rem
    }

    width: 860
    height: 600
    minimumWidth: 620
    minimumHeight: 440
    title: currentAsset.name
           ? qsTr("Trimmer — %1").arg(currentAsset.name)
           : qsTr("Trimmer")
    color: {
        const opacity = ThemeManager.window.backgroundOpacity
        const multiplier = (opacity === undefined || opacity === null) ? 1 : opacity
        return WindowBackdrop.windowBackground(Theme.c.mediaPreviewWindow.background, multiplier)
    }

    Component.onCompleted: WindowBackdrop.attach(root)
    onClosing: {
        EditorState.assetPreview.pause()
        EditorState.assetPreview.end()
        EditorState.assetPreview.windowOpen = false
    }

    Connections {
        target: AssetLibrary
        function onCountChanged() {
            if (!root.visible)
                return
            if (AssetLibrary.count === 0) {
                EditorState.assetPreview.pause()
                EditorState.assetPreview.end()
                root.assetIndex = 0
                root.inSeconds = 0.0
                root.outSeconds = 0.0
            } else if (!EditorState.assetPreview.active || root.assetIndex >= AssetLibrary.count) {
                root.loadAsset(Math.min(Math.max(0, root.assetIndex), AssetLibrary.count - 1))
            }
        }
    }

    Connections {
        target: EditorState.assetPreview
        function onPositionChanged() {
            if (root.loopRange && EditorState.assetPreview.playing && root.outSeconds > root.inSeconds
                    && EditorState.assetPreview.position >= root.outSeconds) {
                EditorState.assetPreview.seek(root.inSeconds)
            }
        }
        function onSessionChanged() {
            if (root.outSeconds <= 0 && root.duration > 0)
                root.outSeconds = root.duration
        }
    }

    FocusScope {
        anchors.fill: parent
        focus: true

        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Space) {
                if (EditorState.assetPreview.playing)
                    EditorState.assetPreview.pause()
                else
                    EditorState.assetPreview.play()
                event.accepted = true
            } else if (event.key === Qt.Key_I) {
                root.markIn()
                event.accepted = true
            } else if (event.key === Qt.Key_O) {
                root.markOut()
                event.accepted = true
            } else if (event.key === Qt.Key_J) {
                EditorState.assetPreview.shuttleReverse()
                event.accepted = true
            } else if (event.key === Qt.Key_K) {
                EditorState.assetPreview.shuttleStop()
                event.accepted = true
            } else if (event.key === Qt.Key_L) {
                EditorState.assetPreview.shuttleForward()
                event.accepted = true
            } else if (event.key === Qt.Key_Left) {
                EditorState.assetPreview.stepFrames(-1)
                event.accepted = true
            } else if (event.key === Qt.Key_Right) {
                EditorState.assetPreview.stepFrames(1)
                event.accepted = true
            } else if (event.key === Qt.Key_A) {
                root.commitToTimeline((event.modifiers & Qt.ShiftModifier) !== 0)
                event.accepted = true
            } else if (event.key === Qt.Key_Escape) {
                root.close()
                event.accepted = true
            }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacingLg
            spacing: Theme.spacingMd

            // Top asset selector + stream mode bar
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Text {
                    text: qsTr("Media:")
                    color: Theme.c.multicamAngleTile.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                }

                ThemedComboBox {
                    id: assetCombo
                    Layout.fillWidth: true
                    model: {
                        const list = []
                        for (let i = 0; i < AssetLibrary.count; i++) {
                            const a = AssetLibrary.assetAt(i)
                            list.push(a.name || qsTr("Asset %1").arg(i + 1))
                        }
                        return list
                    }
                    currentIndex: root.assetIndex
                    onActivated: (idx) => root.loadAsset(idx)
                }

                ThemedComboBox {
                    implicitWidth: 136
                    model: [qsTr("Video + Audio"), qsTr("Video Only"), qsTr("Audio Only")]
                    readonly property var modeValues: ["both", "video", "audio"]
                    currentIndex: Math.max(0, modeValues.indexOf(root.streamMode))
                    onActivated: (idx) => root.streamMode = modeValues[idx]
                }
            }

            // Video / Audio preview frame
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#09090C"
                radius: Theme.c.previewOptionsMenu.radius
                border.width: 1
                border.color: Theme.c.previewOptionsMenu.border
                clip: true

                Image {
                    anchors.fill: parent
                    anchors.margins: 4
                    fillMode: Image.PreserveAspectFit
                    cache: false
                    visible: AssetLibrary.count > 0 && EditorState.assetPreview.active && root.currentAsset.kind !== "audio"
                    source: (EditorState.assetPreview.active && root.currentAsset.kind !== "audio")
                            ? ("image://clippreview/frame?rev=" + EditorState.assetPreview.revision)
                            : ""
                }

                Column {
                    anchors.centerIn: parent
                    spacing: Theme.spacingSm
                    visible: AssetLibrary.count > 0 && root.currentAsset.kind === "audio"

                    IconGlyph {
                        anchors.horizontalCenter: parent.horizontalCenter
                        glyph: Theme.icons.audioLines
                        iconSize: 40
                        iconColor: Theme.c.multicamAngleTile.borderActive
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: root.currentAsset.name || qsTr("Audio Clip")
                        color: Theme.c.multicamAngleTile.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeMd
                        font.weight: Font.Medium
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("Audio-only source — %1").arg(root.formatTime(root.duration))
                        color: Theme.c.multicamAngleTile.foreground
                        opacity: 0.7
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: AssetLibrary.count === 0
                    text: qsTr("Import media into the project bin to trim subclips here.")
                    color: Theme.c.multicamAngleTile.foreground
                    opacity: 0.7
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                }
            }

            // Scrub bar with In/Out range highlight
            Item {
                id: scrubBar
                Layout.fillWidth: true
                implicitHeight: 28

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: 8
                    radius: 4
                    color: "#25252E"
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.duration > 0 && root.outSeconds > root.inSeconds
                    x: (root.inSeconds / Math.max(0.001, root.duration)) * parent.width
                    width: Math.max(4, ((root.outSeconds - root.inSeconds) / Math.max(0.001, root.duration)) * parent.width)
                    height: 12
                    radius: 3
                    color: Theme.c.multicamAngleTile.borderActive
                    opacity: 0.6
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    x: Math.max(0, Math.min(parent.width - 3, (root.position / Math.max(0.001, root.duration)) * parent.width))
                    width: 3
                    height: 22
                    radius: 1
                    color: "#FFFFFF"
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    function seekAt(mx) {
                        if (root.duration <= 0)
                            return
                        const frac = Math.max(0, Math.min(1, mx / Math.max(1, width)))
                        EditorState.assetPreview.seek(frac * root.duration)
                    }
                    onPressed: (mouse) => seekAt(mouse.x)
                    onPositionChanged: (mouse) => { if (pressed) seekAt(mouse.x) }
                }
            }

            // Timecodes & shuttle transport bar
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Text {
                    text: qsTr("Pos %1  |  In %2  |  Out %3  |  Dur %4")
                        .arg(root.formatTime(root.position))
                        .arg(root.formatTime(root.inSeconds))
                        .arg(root.formatTime(root.outSeconds))
                        .arg(root.formatTime(Math.max(0, root.outSeconds - root.inSeconds)))
                    color: Theme.c.multicamAngleTile.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    Layout.fillWidth: true
                }

                ThemedButton {
                    text: qsTr("J −")
                    tooltip: qsTr("Shuttle reverse (J)")
                    onClicked: EditorState.assetPreview.shuttleReverse()
                }
                ThemedButton {
                    text: EditorState.assetPreview.playing ? qsTr("Pause") : qsTr("Play")
                    tooltip: qsTr("Play / Pause (Space)")
                    onClicked: EditorState.assetPreview.playing ? EditorState.assetPreview.pause() : EditorState.assetPreview.play()
                }
                ThemedButton {
                    text: qsTr("L +")
                    tooltip: qsTr("Shuttle forward (L)")
                    onClicked: EditorState.assetPreview.shuttleForward()
                }
                ThemedButton {
                    text: "-1f"
                    tooltip: qsTr("Step backward 1 frame (Left)")
                    onClicked: EditorState.assetPreview.stepFrames(-1)
                }
                ThemedButton {
                    text: "+1f"
                    tooltip: qsTr("Step forward 1 frame (Right)")
                    onClicked: EditorState.assetPreview.stepFrames(1)
                }
            }

            // Mark In/Out + Insert / Overwrite actions
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                ThemedButton {
                    text: qsTr("Mark In (I)")
                    onClicked: root.markIn()
                }
                ThemedButton {
                    text: qsTr("Mark Out (O)")
                    onClicked: root.markOut()
                }
                ThemedButton {
                    text: qsTr("Clear I/O")
                    variant: "text"
                    onClicked: {
                        root.inSeconds = 0.0
                        root.outSeconds = root.duration
                    }
                }
                ThemedCheckBox {
                    text: qsTr("Loop I/O")
                    checked: root.loopRange
                    onToggled: root.loopRange = checked
                }

                Item { Layout.fillWidth: true }

                ThemedButton {
                    text: qsTr("Insert (A)")
                    enabled: AssetLibrary.count > 0 && root.outSeconds > root.inSeconds
                    tooltip: qsTr("Insert subclip at playhead and ripple downstream clips right")
                    onClicked: root.commitToTimeline(false)
                }
                ThemedButton {
                    text: qsTr("Overwrite (Shift+A)")
                    enabled: AssetLibrary.count > 0 && root.outSeconds > root.inSeconds
                    tooltip: qsTr("Overwrite timeline range at playhead with subclip")
                    onClicked: root.commitToTimeline(true)
                }
            }
        }
    }
}
