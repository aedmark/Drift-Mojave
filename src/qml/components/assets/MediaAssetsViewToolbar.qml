import QtQuick
import Drift
// Grid, list, tree and sort toggles that share the media search row.
Row {
    id: root

    property bool gridMode: false
    property string viewMode: "grid"
    property bool treeMode: false
    property bool sortByKind: false
    property string smartFilter: "all"

    signal sortToggled()
    signal smartFilterSelected(string mode)
    signal generatorRequested(string generatorId)

    spacing: Theme.spacingSm

    IconButton {
        id: filterBtn
        glyph: Theme.icons.sliders
        variant: "ghost"
        tooltip: qsTr("Filter media bin")
        active: root.smartFilter !== "all"
        onClicked: smartFilterMenu.popup()

        ThemedContextMenu {
            id: smartFilterMenu

            ThemedMenuItem {
                text: qsTr("All media")
                checkable: true
                checked: root.smartFilter === "all"
                onTriggered: root.smartFilterSelected("all")
            }
            ThemedMenuItem {
                text: qsTr("Unused on timeline")
                checkable: true
                checked: root.smartFilter === "unused"
                onTriggered: root.smartFilterSelected("unused")
            }
            ThemedMenuItem {
                text: qsTr("Unconverted (Long-GOP)")
                checkable: true
                checked: root.smartFilter === "unconverted"
                onTriggered: root.smartFilterSelected("unconverted")
            }
            ThemedMenuItem {
                text: qsTr("Missing files")
                checkable: true
                checked: root.smartFilter === "missing"
                onTriggered: root.smartFilterSelected("missing")
            }
            ThemedMenuItem {
                text: qsTr("Subclips")
                checkable: true
                checked: root.smartFilter === "subclips"
                onTriggered: root.smartFilterSelected("subclips")
            }
            ThemedMenuSeparator { }
            ThemedMenuItem {
                text: qsTr("Remove unused media from project")
                icon.name: Theme.icons.trash
                onTriggered: EditorState.removeUnusedMedia()
            }
            ThemedMenuItem {
                text: qsTr("Consolidate project media to folder…")
                icon.name: Theme.icons.folder
                onTriggered: {
                    var url = FileDialogs.openDirectory(qsTr("Consolidate Project Media to Folder"))
                    if (url != "")
                        EditorState.consolidateProjectMedia(url, true)
                }
            }
        }
    }

    IconButton {
        id: generatorBtn
        glyph: Theme.icons.sparkles
        variant: "ghost"
        tooltip: qsTr("Insert VEGAS Media Generator")
        onClicked: generatorMenu.popup()

        ThemedContextMenu {
            id: generatorMenu

            Repeater {
                model: EditorState.mediaGeneratorCatalog()
                delegate: ThemedMenuItem {
                    required property var modelData
                    text: modelData.name || modelData.id
                    onTriggered: root.generatorRequested(modelData.id)
                }
            }
        }
    }

    IconButton {
        glyph: Theme.icons.layoutGrid
        variant: "ghost"
        tooltip: qsTr("Grid view")
        active: root.gridMode
        onClicked: EditorState.mediaViewMode = "grid"
    }

    IconButton {
        glyph: Theme.icons.list
        variant: "ghost"
        tooltip: qsTr("List view")
        active: root.viewMode === "list"
        onClicked: EditorState.mediaViewMode = "list"
    }

    IconButton {
        glyph: Theme.icons.listTree
        variant: "ghost"
        tooltip: qsTr("Tree view")
        active: root.treeMode
        onClicked: EditorState.mediaViewMode = "tree"
    }

    IconButton {
        glyph: root.sortByKind ? Theme.icons.sortByKind : Theme.icons.sortByName
        variant: "ghost"
        tooltip: root.sortByKind ? qsTr("Sort by name") : qsTr("Sort by type")
        onClicked: root.sortToggled()
    }
}
