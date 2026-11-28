import QtQuick
import Drift

ThemedDialog {
    id: root

    property int trackIndex: -1
    property int clipIndex: -1
    property var previewResult: ({})

    title: qsTr("Music Auto-Remix")
    acceptText: qsTr("Apply Remix")
    preferredWidth: Theme.dialogWidthSm

    function openFor(track, clip, currentDurationSec) {
        trackIndex = track
        clipIndex = clip
        const dur = currentDurationSec > 0 ? currentDurationSec : 15.0
        durationField.text = dur.toFixed(1)
        segmentsSlider.value = 50
        variationsSlider.value = 50
        refreshPreview()
        open()
    }

    function parsedDuration() {
        const v = parseFloat(durationField.text)
        return (isFinite(v) && v > 0) ? v : 0
    }

    function refreshPreview() {
        if (trackIndex < 0 || clipIndex < 0)
            return
        const target = parsedDuration()
        if (target <= 0) {
            previewResult = ({ ok: false, error: qsTr("Enter a positive target duration") })
            return
        }
        previewResult = EditorState.analyzeMusicRemix(
            trackIndex, clipIndex, target,
            Math.round(segmentsSlider.value), Math.round(variationsSlider.value))
    }

    onAccepted: {
        const target = parsedDuration()
        if (target > 0) {
            EditorState.applyMusicRemix(
                trackIndex, clipIndex, target,
                Math.round(segmentsSlider.value), Math.round(variationsSlider.value))
        }
    }

    contentItem: Column {
        width: parent ? parent.width : root.preferredWidth
        spacing: Theme.spacingMd

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Re-sequences beat-matched sections with 20 ms equal-power crossfades to fit a new duration without altering tempo or pitch.")
        }

        ThemedLabel {
            text: qsTr("Target duration (seconds)")
        }
        ThemedTextField {
            id: durationField
            width: parent.width
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            onTextEdited: root.refreshPreview()
        }

        Row {
            width: parent.width
            ThemedLabel { text: qsTr("Cut density (Segments)") }
            Item { width: 8; height: 1 }
            ThemedLabel { text: Math.round(segmentsSlider.value) + "%" }
        }
        ThemedSlider {
            id: segmentsSlider
            width: parent.width
            from: 0
            to: 100
            stepSize: 5
            value: 50
            onMoved: root.refreshPreview()
        }

        Row {
            width: parent.width
            ThemedLabel { text: qsTr("Transition flexibility (Variations)") }
            Item { width: 8; height: 1 }
            ThemedLabel { text: Math.round(variationsSlider.value) + "%" }
        }
        ThemedSlider {
            id: variationsSlider
            width: parent.width
            from: 0
            to: 100
            stepSize: 5
            value: 50
            onMoved: root.refreshPreview()
        }

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            color: root.previewResult && root.previewResult.ok
                   ? Theme.c.foreground
                   : Theme.c.warning
            text: {
                if (!root.previewResult)
                    return ""
                if (root.previewResult.ok) {
                    return qsTr("Plan: %1 s at %2 BPM (%3 cut(s))")
                        .arg((root.previewResult.actualDuration || 0).toFixed(2))
                        .arg(Math.round(root.previewResult.bpm || 0))
                        .arg(root.previewResult.cutCount || 0)
                }
                return root.previewResult.error || qsTr("Cannot remix this audio clip")
            }
        }
    }
}
