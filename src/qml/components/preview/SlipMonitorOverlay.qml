import QtQuick
import QtQuick.Controls.Basic
import Drift

// 4-Up Slip Monitor overlay (Premiere / FilmCraft workflow):
// Displays Prev Out | Next In (top anchors) and Slip In | Slip Out (bottom live targets)
// while slipping a clip on the timeline with Y or Alt+Drag.
Rectangle {
    id: root

    readonly property var st: EditorState.slipPreviewState
    readonly property int rev: EditorState.slipPreviewRevision

    anchors.fill: parent
    anchors.margins: Theme.spacingMd
    radius: Theme.radiusMd
    color: Qt.rgba(0.06, 0.07, 0.09, 0.94)
    border.width: 1
    border.color: Theme.accent

    Column {
        anchors.fill: parent
        anchors.margins: Theme.spacingSm
        spacing: Theme.spacingSm

        // Header bar: Clip name, delta frames, source window
        Row {
            id: headerRow
            width: parent.width
            spacing: Theme.spacingSm

            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.c.previewOriginalBadge.foreground
                font.pixelSize: Theme.fontSizeSm
                font.bold: true
                elide: Text.ElideRight
                width: Math.max(80, headerRow.width - deltaPill.width - windowLabel.width - Theme.spacingSm * 2)
                text: {
                    const name = st.clipName || qsTr("Clip")
                    return qsTr("Slip Monitor: %1").arg(name)
                }
            }

            Rectangle {
                id: deltaPill
                anchors.verticalCenter: parent.verticalCenter
                height: deltaText.implicitHeight + Theme.spacingXs
                width: deltaText.implicitWidth + Theme.spacingSm * 2
                radius: Theme.radiusSm
                color: Theme.accent

                Text {
                    id: deltaText
                    anchors.centerIn: parent
                    color: Theme.c.previewOriginalBadge.foreground
                    font.pixelSize: Theme.fontSizeXs
                    font.bold: true
                    text: {
                        const df = Number(st.deltaFrames || 0)
                        return (df >= 0 ? "+" : "") + df + "f"
                    }
                }
            }

            Text {
                id: windowLabel
                anchors.verticalCenter: parent.verticalCenter
                color: Qt.rgba(1, 1, 1, 0.7)
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.monoFontFamily
                text: {
                    const iTc = (st.in && st.in.timecode) ? st.in.timecode : ""
                    const oTc = (st.out && st.out.timecode) ? st.out.timecode : ""
                    return iTc && oTc ? (iTc + "  →  " + oTc) : ""
                }
            }
        }

        // 2x2 Quad Frame Grid
        Grid {
            id: quadGrid
            width: parent.width
            height: Math.max(80, parent.height - headerRow.height - Theme.spacingSm)
            columns: 2
            rows: 2
            columnSpacing: Theme.spacingSm
            rowSpacing: Theme.spacingSm

            readonly property real cellW: Math.max(40, (width - columnSpacing) / 2)
            readonly property real cellH: Math.max(40, (height - rowSpacing) / 2)

            Repeater {
                model: [
                    { slot: 0, title: qsTr("Prev Out"), key: "prev", primary: false },
                    { slot: 3, title: qsTr("Next In"),  key: "next", primary: false },
                    { slot: 1, title: qsTr("Slip In"),  key: "in",   primary: true  },
                    { slot: 2, title: qsTr("Slip Out"), key: "out",  primary: true  }
                ]

                delegate: Rectangle {
                    required property var modelData
                    readonly property var info: root.st[modelData.key] || ({})
                    readonly property bool valid: info.valid === true
                    readonly property bool hasImage: info.hasImage === true

                    width: quadGrid.cellW
                    height: quadGrid.cellH
                    radius: Theme.radiusSm
                    color: Qt.rgba(0.03, 0.04, 0.05, 1.0)
                    border.width: modelData.primary ? 2 : 1
                    border.color: modelData.primary ? Theme.accent : Qt.rgba(1, 1, 1, 0.14)
                    clip: true

                    Image {
                        anchors.fill: parent
                        anchors.margins: modelData.primary ? 2 : 1
                        fillMode: Image.PreserveAspectFit
                        asynchronous: false
                        cache: false
                        visible: parent.valid && parent.hasImage
                        source: (parent.valid && parent.hasImage)
                            ? ("image://slippreview/" + modelData.slot + "?rev=" + root.rev)
                            : ""
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: !parent.valid
                        color: Qt.rgba(1, 1, 1, 0.65)
                        font.pixelSize: Theme.fontSizeXs
                        text: qsTr("No Adjacent Clip")
                    }

                    // Top-left label badge
                    Rectangle {
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.margins: Theme.spacingXs
                        height: titleText.implicitHeight + 4
                        width: Math.min(parent.width - Theme.spacingXs * 2, titleText.implicitWidth + Theme.spacingSm)
                        radius: Theme.radiusXs
                        color: Qt.rgba(0.05, 0.06, 0.08, 0.84)

                        Text {
                            id: titleText
                            anchors.centerIn: parent
                            width: parent.width - Theme.spacingXs
                            elide: Text.ElideRight
                            color: modelData.primary ? Theme.accent : Theme.c.previewOriginalBadge.foreground
                            font.pixelSize: Theme.fontSizeXs
                            font.bold: true
                            text: info.clipName ? (modelData.title + " · " + info.clipName) : modelData.title
                        }
                    }

                    // Bottom-right timecode badge
                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.right: parent.right
                        anchors.margins: Theme.spacingXs
                        visible: parent.valid && Boolean(info.timecode)
                        height: tcText.implicitHeight + 4
                        width: tcText.implicitWidth + Theme.spacingSm
                        radius: Theme.radiusXs
                        color: Qt.rgba(0.05, 0.06, 0.08, 0.84)

                        Text {
                            id: tcText
                            anchors.centerIn: parent
                            color: Theme.c.previewOriginalBadge.foreground
                            font.pixelSize: Theme.fontSizeXs
                            font.family: Theme.monoFontFamily
                            text: info.timecode || ""
                        }
                    }
                }
            }
        }
    }
}
