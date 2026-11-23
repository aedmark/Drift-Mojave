import QtQuick
import QtQuick.Layouts
import Drift

// Split-grid multicamera overlay rendered inside the preview pane when
// EditorState.inlineMulticamEnabled is active. Keys 1..9 or clicking a tile
// cut at the playhead onto that camera take in a single undo step.
Rectangle {
    id: root

    color: "#E8101014"

    readonly property var takes: EditorState.inlineMulticamTakes || []
    readonly property int takeCount: takes.length
    readonly property int gridColumns: takeCount <= 1 ? 1 : (takeCount <= 4 ? 2 : 3)
    readonly property int gridRows: Math.max(1, Math.ceil(Math.min(takeCount, 9) / gridColumns))

    function formatTime(sec) {
        const s = Math.max(0, sec || 0)
        const mins = Math.floor(s / 60)
        const rem = (s - mins * 60).toFixed(2)
        return (mins < 10 ? "0" : "") + mins + ":" + (rem.length < 5 ? "0" : "") + rem
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd
        spacing: Theme.spacingSm

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 28
            radius: Theme.c.previewOptionsMenu.radius
            color: "#CC1B1B22"
            border.width: 1
            border.color: Theme.c.previewOptionsMenu.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingMd
                anchors.rightMargin: Theme.spacingSm
                spacing: Theme.spacingSm

                IconGlyph {
                    glyph: Theme.icons.video
                    iconSize: Theme.iconSizeSm
                    iconColor: Theme.c.multicamAngleTile.borderActive
                }

                Text {
                    Layout.fillWidth: true
                    text: root.takeCount > 0
                          ? qsTr("Inline Multicamera (%1 angles) — Press 1–9 or click angle to cut")
                                .arg(root.takeCount)
                          : qsTr("Inline Multicamera — Select or place playhead over a multi-take video clip")
                    color: Theme.c.multicamAngleTile.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }

                Text {
                    text: EditorState.projectFile.formatTimecode(EditorState.playheadSeconds)
                    color: Theme.c.multicamAngleTile.foreground
                    opacity: 0.8
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                ThemedButton {
                    text: qsTr("Prev Cut")
                    variant: "text"
                    tooltip: qsTr("Jump to previous multicam cut")
                    onClicked: EditorState.jumpToAdjacentMulticamCut(-1)
                }

                ThemedButton {
                    text: qsTr("Next Cut")
                    variant: "text"
                    tooltip: qsTr("Jump to next multicam cut")
                    onClicked: EditorState.jumpToAdjacentMulticamCut(1)
                }

                ThemedButton {
                    text: qsTr("Undo")
                    variant: "text"
                    enabled: EditorState.canUndo
                    tooltip: qsTr("Undo last angle cut")
                    onClicked: EditorState.undo()
                }

                ThemedButton {
                    text: EditorState.playing ? qsTr("Pause") : qsTr("Play")
                    glyph: EditorState.playing ? Theme.icons.pause : Theme.icons.play
                    variant: "text"
                    tooltip: qsTr("Play / Pause (Space)")
                    onClicked: EditorState.togglePlayback()
                }

                ThemedButton {
                    text: qsTr("Exit")
                    variant: "text"
                    tooltip: qsTr("Exit Inline Multicamera Mode (Alt+Shift+C)")
                    onClicked: EditorState.setInlineMulticamEnabled(false)
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Text {
                anchors.centerIn: parent
                visible: root.takeCount === 0
                width: Math.min(parent.width - 32, 360)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.c.multicamAngleTile.foreground
                opacity: 0.75
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                text: qsTr("No multi-take clip at the playhead.\nSelect 2+ video clips on the timeline and choose Take → Create Multicamera Track from Selection.")
            }

            GridLayout {
                anchors.fill: parent
                visible: root.takeCount > 0
                columns: root.gridColumns
                rows: root.gridRows
                columnSpacing: Theme.spacingSm
                rowSpacing: Theme.spacingSm

                Repeater {
                    model: Math.min(root.takeCount, 9)

                    delegate: Rectangle {
                        id: tile
                        required property int index
                        readonly property var takeData: root.takes[index] || ({})
                        readonly property bool isActive: takeData.active === true

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: Theme.c.multicamAngleTile.background
                        radius: Theme.c.multicamAngleTile.radius
                        border.width: isActive ? Theme.c.multicamAngleTile.borderWidthActive : Theme.c.multicamAngleTile.borderWidth
                        border.color: isActive ? Theme.c.multicamAngleTile.borderActive : Theme.c.multicamAngleTile.border
                        clip: true

                        Image {
                            anchors.fill: parent
                            anchors.margins: 2
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            cache: false
                            source: "image://multicam/" + index + "?rev=" + EditorState.inlineMulticamRevision
                        }

                        Rectangle {
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.margins: Theme.spacingXs
                            width: 22
                            height: 22
                            radius: Theme.c.multicamAngleTile.radius
                            color: tile.isActive ? Theme.c.multicamAngleTile.borderActive : "#CC18181E"

                            Text {
                                anchors.centerIn: parent
                                text: String(tile.index + 1)
                                color: "#FFFFFF"
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                                font.weight: Font.Bold
                            }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 2
                            height: 20
                            color: "#B3121218"

                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.spacingSm
                                anchors.rightMargin: Theme.spacingSm
                                verticalAlignment: Text.AlignVCenter
                                text: tile.takeData.name || qsTr("Angle %1").arg(tile.index + 1)
                                color: tile.isActive ? Theme.c.multicamAngleTile.borderActive : Theme.c.multicamAngleTile.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: EditorState.punchMulticamTake(tile.index)
                        }
                    }
                }
            }
        }
    }
}
