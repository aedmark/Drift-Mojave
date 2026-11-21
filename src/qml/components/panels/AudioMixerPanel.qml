import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

PanelFrame {
    id: root

    readonly property bool hasClip: EditorState.selectedTrack >= 0 && EditorState.selectedClip >= 0
    property string loudnessText: qsTr("Loudness: not measured")
    property bool loudnessIsError: false
    property bool loudnessNormalizing: false
    property var liveLoudness: ({
        momentaryLufs: -70.0,
        shortTermLufs: -70.0,
        integratedLufs: -70.0,
        truePeakDbfs: -120.0,
        maxHoldTruePeakDbfs: -120.0,
        loudnessRangeLu: 0.0,
        clipping: false
    })

    function refreshLiveLoudness() {
        liveLoudness = EditorState.masterLoudness()
    }

    Timer {
        interval: 150
        repeat: true
        running: root.visible && EditorState.playing
        onTriggered: root.refreshLiveLoudness()
    }

    Connections {
        target: EditorState
        function onPlayingChanged() {
            root.refreshLiveLoudness()
        }
        function onSelectionChanged() {
            EditorState.cancelFocusedClipLoudness()
            root.loudnessText = qsTr("Loudness: not measured")
            root.loudnessIsError = false
        }
        function onLoudnessFinished(result) {
            if (root.loudnessNormalizing)
                root.showResult(result, result.clipping
                                        ? qsTr("Normalized to %1 LUFS, but peaks clip").arg(result.target_lufs)
                                        : qsTr("Normalized to %1 LUFS").arg(result.target_lufs))
            else
                root.showResult(result, qsTr("Loudness: %1 LUFS, true peak %2 dBTP")
                                       .arg(result.lufs?.toFixed(1)).arg(result.true_peak_db?.toFixed(1)))
        }
    }

    function showResult(result, text) {
        loudnessIsError = !result.ok
        loudnessText = result.ok ? text : (result.detail ?? result.error ?? qsTr("Failed"))
    }

    function analyze() {
        loudnessNormalizing = false
        EditorState.startFocusedClipLoudness(false, 0)
    }

    function normalize() {
        loudnessNormalizing = true
        EditorState.startFocusedClipLoudness(true, targetBox.currentValue)
    }

    AssetsPanelHeader {
        id: header
        width: parent.width
        height: Theme.panelHeaderHeight
        title: qsTr("Audio Mixer")
    }

    Rectangle {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: footerCol.implicitHeight + Theme.spacingMd * 2
        color: Theme.c.audioMixerStrip.background

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Theme.c.audioMixerStrip.border
        }

        Column {
            id: footerCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.spacingMd
            anchors.rightMargin: Theme.spacingMd
            spacing: Theme.spacingSm

            RowLayout {
                width: parent.width
                spacing: Theme.spacingSm

                Text {
                    Layout.fillWidth: true
                    text: {
                        const fmt = (v, floor) => v <= floor ? "−∞" : v.toFixed(1)
                        return qsTr("Master LUFS  M %1 · S %2 · I %3  |  TP %4 dB")
                            .arg(fmt(root.liveLoudness.momentaryLufs, -69.5))
                            .arg(fmt(root.liveLoudness.shortTermLufs, -69.5))
                            .arg(fmt(root.liveLoudness.integratedLufs, -69.5))
                            .arg(fmt(root.liveLoudness.maxHoldTruePeakDbfs, -119.5))
                    }
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    color: (root.liveLoudness.clipping || root.liveLoudness.maxHoldTruePeakDbfs > -1.0)
                           ? Theme.c.audioMixerStrip.destructive
                           : Theme.c.audioMixerStrip.foregroundMuted
                    elide: Text.ElideRight

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            EditorState.resetMasterLoudness()
                            root.refreshLiveLoudness()
                        }
                    }

                    ThemedToolTip {
                        text: qsTr("Real-time ITU-R BS.1770 / EBU R128 loudness (Momentary / Short-Term / Integrated LUFS & True Peak hold). Click to reset.")
                    }
                }

                ThemedButton {
                    text: qsTr("+ Bus")
                    tooltip: qsTr("Add an audio sub-bus")
                    onClicked: EditorState.addAudioBus()
                }
            }

            Row {
                width: parent.width
                spacing: Theme.spacingSm

                Text {
                    width: Math.max(0, parent.width - analyzeBtn.width - parent.spacing)
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !EditorState.loudnessBusy
                    text: root.loudnessText
                    elide: Text.ElideRight
                    color: root.loudnessIsError ? Theme.c.audioMixerStrip.destructive
                                                : Theme.c.audioMixerStrip.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                Column {
                    width: Math.max(0, parent.width - analyzeBtn.width - parent.spacing)
                    anchors.verticalCenter: parent.verticalCenter
                    visible: EditorState.loudnessBusy
                    spacing: Theme.spacingXs

                    Text {
                        text: qsTr("Measuring… %1%").arg(Math.round(EditorState.loudnessProgress * 100))
                        color: Theme.c.audioMixerStrip.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }

                    ThemedProgressBar {
                        width: parent.width
                        barHeight: 4
                        value: EditorState.loudnessProgress
                    }
                }

                ThemedButton {
                    id: analyzeBtn
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Analyze")
                    variant: "secondary"
                    glyph: Theme.icons.audioLines
                    tooltip: root.hasClip ? qsTr("Measure loudness of the selected clip")
                                          : qsTr("Select a clip first")
                    enabled: root.hasClip && !EditorState.loudnessBusy
                    onClicked: root.analyze()
                }
            }

            Row {
                spacing: Theme.spacingSm

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Normalize to")
                    color: Theme.c.audioMixerStrip.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                ThemedComboBox {
                    id: targetBox
                    anchors.verticalCenter: parent.verticalCenter
                    width: 110
                    enabled: root.hasClip
                    model: [
                        { label: qsTr("-14 LUFS"), value: -14 },
                        { label: qsTr("-16 LUFS"), value: -16 },
                        { label: qsTr("-23 LUFS"), value: -23 }
                    ]
                    textRole: "label"
                    valueRole: "value"
                }

                ThemedButton {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Normalize")
                    variant: "secondary"
                    glyph: Theme.icons.gauge
                    tooltip: root.hasClip ? qsTr("Set the selected clip's volume to the chosen loudness")
                                          : qsTr("Select a clip first")
                    enabled: root.hasClip && !EditorState.loudnessBusy
                    onClicked: root.normalize()
                }
            }
        }
    }

    AudioMixerStrip {
        anchors.top: header.bottom
        anchors.bottom: footer.top
        width: parent.width
        resizing: true
        embedded: true
    }
}
