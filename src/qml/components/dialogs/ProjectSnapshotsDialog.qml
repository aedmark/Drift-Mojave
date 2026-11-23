import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

// Milestone snapshot manager: save labeled lightweight .drift snapshots,
// restore any snapshot onto the timeline, or delete old snapshots.
ThemedDialog {
    id: root

    property var snapshots: []

    title: qsTr("Project Milestone Snapshots")
    preferredWidth: Theme.dialogWidthMd
    showFooter: false
    showAccept: false
    showReject: false
    acceptOnReturn: false

    function openDialog() {
        refresh()
        open()
    }

    function refresh() {
        snapshots = EditorState.projectFile.listProjectSnapshots() || []
    }

    function formatSize(bytes) {
        const b = Number(bytes || 0)
        if (b >= 1024 * 1024)
            return (b / (1024 * 1024)).toFixed(1) + " MB"
        if (b >= 1024)
            return Math.round(b / 1024) + " KB"
        return b + " B"
    }

    Connections {
        target: EditorState.projectFile

        function onProjectSnapshotsChanged() {
            if (root.visible)
                root.refresh()
        }
    }

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : 460

        RowLayout {
            width: parent.width
            spacing: Theme.spacingSm

            ThemedTextField {
                id: labelInput
                Layout.fillWidth: true
                placeholderText: qsTr("Optional milestone label (e.g. Picture Lock)")
                onAccepted: saveBtn.clicked()
            }

            ThemedButton {
                id: saveBtn
                text: qsTr("Save Snapshot")
                variant: "primary"
                onClicked: {
                    EditorState.projectFile.saveProjectSnapshot(labelInput.text)
                    labelInput.text = ""
                    root.refresh()
                }
            }
        }

        ThemedLabel {
            width: parent.width
            visible: root.snapshots.length === 0
            text: qsTr("No milestone snapshots yet.")
            tone: "muted"
            size: "sm"
        }

        ScrollView {
            width: parent.width
            visible: root.snapshots.length > 0
            height: Math.min(280, snapListCol.implicitHeight + Theme.spacingSm * 2)
            clip: true

            Column {
                id: snapListCol
                width: parent.width
                spacing: Theme.spacingSm

                Repeater {
                    model: root.snapshots

                    delegate: RowLayout {
                        required property var modelData
                        width: snapListCol.width
                        spacing: Theme.spacingSm

                        Column {
                            Layout.fillWidth: true
                            spacing: 2

                            ThemedLabel {
                                width: parent.width
                                text: modelData.fileName || ""
                                tone: "default"
                                size: "sm"
                                font.weight: Font.Medium
                                elide: Text.ElideMiddle
                            }

                            ThemedLabel {
                                width: parent.width
                                text: (modelData.createdAt || "") + " • " + root.formatSize(modelData.sizeBytes)
                                tone: "muted"
                                size: "xs"
                                elide: Text.ElideRight
                            }
                        }

                        ThemedButton {
                            text: qsTr("Restore")
                            variant: "secondary"
                            onClicked: {
                                EditorState.projectFile.restoreProjectSnapshot(modelData.path)
                                root.close()
                            }
                        }

                        ThemedButton {
                            text: qsTr("Delete")
                            variant: "destructive"
                            onClicked: {
                                EditorState.projectFile.deleteProjectSnapshot(modelData.path)
                                root.refresh()
                            }
                        }
                    }
                }
            }
        }

        Row {
            anchors.right: parent.right
            spacing: Theme.spacingMd

            ThemedButton {
                text: qsTr("Close")
                variant: "secondary"
                onClicked: root.close()
            }
        }
    }
}
