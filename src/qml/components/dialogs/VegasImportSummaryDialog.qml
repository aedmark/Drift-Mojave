import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

// Post-import summary report shown after opening a Sony / MAGIX VEGAS Pro (.veg) project,
// with missing-media relink from a chosen folder.
ThemedDialog {
    id: root

    property var report: ({})

    readonly property int missingCount: Number(report.missingAssets || 0)
    readonly property int onlineCount: Number(report.onlineAssets || 0)
    readonly property int autoProResCount: Number(report.autoRemappedProRes || 0)
    readonly property var missingFiles: report.missingFiles || []

    title: qsTr("VEGAS Pro Import Summary")
    preferredWidth: Theme.dialogWidthMd
    showFooter: false
    showAccept: false
    showReject: false
    acceptOnReturn: false

    function openWithReport(summary) {
        report = summary || ({})
        open()
    }

    Connections {
        target: EditorState.projectFile

        function onLastVegasImportReportChanged() {
            if (root.visible && EditorState.projectFile.lastVegasImportReport)
                root.report = EditorState.projectFile.lastVegasImportReport
        }
    }

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : 420

        ThemedLabel {
            width: parent.width
            text: root.report.projectName || qsTr("Imported VEGAS Project")
            tone: "default"
            size: "base"
            font.weight: Font.DemiBold
            font.pixelSize: Theme.fontSizeLg
            elide: Text.ElideRight
        }

        GridLayout {
            width: parent.width
            columns: 2
            columnSpacing: Theme.spacingLg
            rowSpacing: Theme.spacingXs

            ThemedLabel {
                text: qsTr("Format:")
                tone: "muted"
                size: "sm"
            }
            ThemedLabel {
                Layout.fillWidth: true
                text: qsTr("%1 • %2")
                      .arg(root.report.resolution || "—")
                      .arg(root.report.frameRate || "—")
                tone: "default"
                size: "sm"
            }

            ThemedLabel {
                text: qsTr("Timeline:")
                tone: "muted"
                size: "sm"
            }
            ThemedLabel {
                Layout.fillWidth: true
                text: qsTr("%1 video, %2 audio track(s) • %3 clip(s)")
                      .arg(Number(root.report.videoTracks || 0))
                      .arg(Number(root.report.audioTracks || 0))
                      .arg(Number(root.report.totalClips || 0))
                tone: "default"
                size: "sm"
            }

            ThemedLabel {
                text: qsTr("Annotations:")
                tone: "muted"
                size: "sm"
            }
            ThemedLabel {
                Layout.fillWidth: true
                text: qsTr("%1 marker(s) • %2 region(s)")
                      .arg(Number(root.report.markers || 0))
                      .arg(Number(root.report.regions || 0))
                tone: "default"
                size: "sm"
            }

            ThemedLabel {
                text: qsTr("Media:")
                tone: "muted"
                size: "sm"
            }
            ThemedLabel {
                Layout.fillWidth: true
                text: root.missingCount > 0
                      ? qsTr("%1 online • %2 missing").arg(root.onlineCount).arg(root.missingCount)
                      : qsTr("%1 file(s) online").arg(root.onlineCount)
                tone: root.missingCount > 0 ? "warning" : "default"
                size: "sm"
                font.weight: root.missingCount > 0 ? Font.DemiBold : Font.Normal
            }
        }

        ThemedLabel {
            width: parent.width
            visible: root.autoProResCount > 0
            wrapMode: Text.WordWrap
            text: qsTr("Automatically matched %n existing ProRes 422 transcode(s).", "", root.autoProResCount)
            tone: "muted"
            size: "sm"
        }

        Column {
            width: parent.width
            spacing: Theme.spacingXs
            visible: root.missingCount > 0

            ThemedLabel {
                width: parent.width
                text: qsTr("Missing media files:")
                tone: "warning"
                size: "sm"
                font.weight: Font.Medium
            }

            ScrollView {
                width: parent.width
                height: Math.min(130, missingListCol.implicitHeight + Theme.spacingSm * 2)
                clip: true

                Column {
                    id: missingListCol
                    width: parent.width
                    spacing: 2

                    Repeater {
                        model: root.missingFiles

                        delegate: ThemedLabel {
                            required property string modelData
                            width: missingListCol.width
                            text: "• " + modelData
                            tone: "muted"
                            size: "xs"
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }
        }

        Row {
            anchors.right: parent.right
            spacing: Theme.spacingMd

            ThemedButton {
                visible: root.missingCount > 0
                text: qsTr("Locate Missing Media Folder…")
                variant: "primary"
                onClicked: {
                    var folder = FileDialogs.openDirectory(qsTr("Locate Missing Media Folder"), "")
                    if (folder != "") {
                        EditorState.projectFile.relinkMissingMediaInFolder(folder)
                        root.report = EditorState.projectFile.lastVegasImportReport
                    }
                }
            }

            ThemedButton {
                text: qsTr("Close")
                variant: root.missingCount > 0 ? "secondary" : "primary"
                onClicked: root.close()
            }
        }
    }
}
