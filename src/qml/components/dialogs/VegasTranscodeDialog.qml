import QtQuick
import QtQuick.Controls.Basic
import Drift

// Shown when opening a Sony / MAGIX VEGAS Pro project with camera raw / AVCHD (MTS) media
// offering one-click batch parallel transcode to Apple ProRes 422, and as a live progress
// dialog whenever any ProRes conversion is running.
ThemedDialog {
    id: root

    property var assetIds: []
    property var projectUrl: null

    readonly property bool transcoding: EditorState.bin.editing && EditorState.bin.editIsConversion
    property bool _wasTranscoding: false

    title: transcoding ? qsTr("Transcoding to Apple ProRes 422")
                       : qsTr("Convert Media to Apple ProRes 422")
    preferredWidth: Theme.dialogWidthMd
    showFooter: false
    showAccept: false
    showReject: false
    acceptOnReturn: false

    function openFor(ids, url) {
        if (!ids || ids.length === 0)
            return
        assetIds = ids
        projectUrl = url
        open()
    }

    function openProgress() {
        open()
    }

    Connections {
        target: EditorState.bin

        function onEditChanged() {
            const active = root.transcoding
            if (root._wasTranscoding && !active && root.visible && root.assetIds.length === 0)
                root.close()
            root._wasTranscoding = active
        }
    }

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : 400

        LabelledProgressRing {
            width: parent.width
            visible: root.transcoding
            value: EditorState.bin.editProgress
            indeterminate: root.transcoding && EditorState.bin.editProgress <= 0.001
        }

        ThemedLabel {
            width: parent.width
            horizontalAlignment: root.transcoding ? Text.AlignHCenter : Text.AlignLeft
            wrapMode: Text.WordWrap
            text: root.transcoding
                  ? (EditorState.bin.editStatus.length > 0
                     ? EditorState.bin.editStatus
                     : qsTr("Transcoding to Apple ProRes 422…"))
                  : qsTr("This VEGAS Pro timeline contains %n unconverted AVCHD / camera raw clip(s).", "", root.assetIds.length)
            tone: "default"
            size: root.transcoding ? "sm" : "base"
            font.weight: root.transcoding ? Font.Medium : Font.DemiBold
            font.pixelSize: root.transcoding ? Theme.fontSizeSm : Theme.fontSizeLg
        }

        ThemedLabel {
            width: parent.width
            horizontalAlignment: root.transcoding ? Text.AlignHCenter : Text.AlignLeft
            wrapMode: Text.WordWrap
            text: root.transcoding
                  ? (EditorState.bin.editName.length > 0
                     ? qsTr("%1 — You can hide this dialog to keep editing while transcoding runs in the background.")
                           .arg(EditorState.bin.editName)
                     : qsTr("You can hide this dialog to keep editing while transcoding runs in the background."))
                  : qsTr("Drift Mojave works best with edit-friendly intraframe codecs. Converting these clips to Apple ProRes 422 now ensures smooth timeline playback, responsive frame-accurate scrubbing, and prevents audio sync drift.\n\nTranscoding runs in the background using multi-threaded FFmpeg without modifying your original media files.")
            tone: "muted"
            size: "sm"
            lineHeight: 1.35
        }

        Row {
            anchors.right: parent.right
            spacing: Theme.spacingMd

            ThemedButton {
                text: root.transcoding ? qsTr("Cancel transcode") : qsTr("Keep Original Media")
                variant: root.transcoding ? "destructive" : "secondary"
                onClicked: {
                    if (root.transcoding)
                        EditorState.bin.cancelEdit()
                    root.assetIds = []
                    root.close()
                }
            }

            ThemedButton {
                text: root.transcoding ? qsTr("Run in background") : qsTr("Convert to ProRes 422")
                variant: root.transcoding ? "secondary" : "primary"
                onClicked: {
                    if (root.transcoding) {
                        root.close()
                        return
                    }
                    const ids = root.assetIds
                    const url = root.projectUrl
                    root.assetIds = []
                    if (ids && ids.length > 0)
                        EditorState.bin.convertToProRes(ids, url)
                    else
                        root.close()
                }
            }
        }
    }
}
