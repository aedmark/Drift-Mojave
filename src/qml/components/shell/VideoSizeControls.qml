import QtQuick
import QtQuick.Window
import Drift

// Canvas size and frame rate. Shared by the header Video dialog and Android Settings.
Column {
    id: root

    spacing: Theme.spacingLg

    signal layoutChooserOpened()

    property int canvasW: { void EditorState.tracksRevision; return EditorState.projectFile.projectWidth() }
    property int canvasH: { void EditorState.tracksRevision; return EditorState.projectFile.projectHeight() }
    property int canvasFps: { void EditorState.tracksRevision; return EditorState.projectFile.projectFps() }
    property int canvasFpsNum: { void EditorState.tracksRevision; return EditorState.projectFile.projectFpsNum() }
    property int canvasFpsDen: { void EditorState.tracksRevision; return EditorState.projectFile.projectFpsDen() }
    property bool canvasDropFrame: { void EditorState.tracksRevision; return EditorState.projectFile.projectDropFrame() }

    readonly property var fpsPresets: [
        { label: "23.976 fps (NTSC Film)", num: 24000, den: 1001 },
        { label: "24 fps (Film)", num: 24, den: 1 },
        { label: "25 fps (PAL)", num: 25, den: 1 },
        { label: "29.97 fps (NTSC)", num: 30000, den: 1001 },
        { label: "30 fps", num: 30, den: 1 },
        { label: "50 fps (PAL)", num: 50, den: 1 },
        { label: "59.94 fps (NTSC)", num: 60000, den: 1001 },
        { label: "60 fps", num: 60, den: 1 },
        { label: qsTr("Custom"), num: 0, den: 1 }
    ]

    readonly property int matchedFpsPresetIndex: {
        for (var i = 0; i < fpsPresets.length - 1; ++i) {
            if (fpsPresets[i].num === root.canvasFpsNum && fpsPresets[i].den === root.canvasFpsDen)
                return i
        }
        return fpsPresets.length - 1
    }

    readonly property var canvasPresets: [
        { label: qsTr("Custom"), w: 0, h: 0 },
        { label: "1920×1080 (16:9)", w: 1920, h: 1080 },
        { label: "3840×2160 (4K)", w: 3840, h: 2160 },
        { label: "1080×1920 (9:16)", w: 1080, h: 1920 },
        { label: "1080×1080 (1:1)", w: 1080, h: 1080 },
        { label: "1440×1080 (4:3)", w: 1440, h: 1080 }
    ]

    ThemedButton {
        width: parent.width
        variant: "secondary"
        glyph: Theme.icons.ratio
        text: qsTr("Choose layout…")
        tooltip: qsTr("Pick a platform template (YouTube, Instagram, TikTok, …) and quality")
        enabled: !EditorState.preview.canvasCropMode
        onClicked: {
            if (ShellBridge.main && ShellBridge.main.openLayoutChooser)
                ShellBridge.main.openLayoutChooser()
            root.layoutChooserOpened()
        }
    }

    ThemedComboBox {
        width: parent.width
        enabled: !EditorState.preview.canvasCropMode
        model: root.canvasPresets.map(function (p) { return p.label })
        tooltip: qsTr("Change the video size. Clips keep their current size and position.")
        currentIndex: {
            const presets = root.canvasPresets
            for (var i = 1; i < presets.length; ++i) {
                if (presets[i].w === root.canvasW && presets[i].h === root.canvasH)
                    return i
            }
            return 0
        }
        onActivated: {
            const preset = root.canvasPresets[currentIndex]
            if (preset.w > 0)
                EditorState.projectFile.setProjectResolution(preset.w, preset.h)
        }
    }

    Row {
        width: parent.width
        spacing: Theme.spacingLg

        Column {
            width: (parent.width - parent.spacing) / 2
            spacing: Theme.spacingSm
            ThemedLabel { text: qsTr("Width") }
            ThemedNumberField {
                width: parent.width
                enabled: !EditorState.preview.canvasCropMode
                from: 16
                to: 7680
                step: 2
                unit: "px"
                value: root.canvasW
                onEdited: v => EditorState.projectFile.setProjectResolution(v, root.canvasH)
            }
        }

        Column {
            width: (parent.width - parent.spacing) / 2
            spacing: Theme.spacingSm
            ThemedLabel { text: qsTr("Height") }
            ThemedNumberField {
                width: parent.width
                enabled: !EditorState.preview.canvasCropMode
                from: 16
                to: 4320
                step: 2
                unit: "px"
                value: root.canvasH
                onEdited: v => EditorState.projectFile.setProjectResolution(root.canvasW, v)
            }
        }
    }

    Column {
        width: parent.width
        spacing: Theme.spacingSm
        ThemedLabel { text: qsTr("Frames per second") }
        Row {
            width: parent.width
            spacing: Theme.spacingMd
            ThemedComboBox {
                width: root.matchedFpsPresetIndex === root.fpsPresets.length - 1
                       ? (parent.width - parent.spacing) * 0.6
                       : parent.width
                enabled: !EditorState.preview.canvasCropMode
                model: root.fpsPresets.map(function (p) { return p.label })
                currentIndex: root.matchedFpsPresetIndex
                onActivated: {
                    const preset = root.fpsPresets[currentIndex]
                    if (preset.num > 0)
                        EditorState.projectFile.setProjectFpsRational(preset.num, preset.den, root.canvasDropFrame)
                }
            }
            ThemedNumberField {
                visible: root.matchedFpsPresetIndex === root.fpsPresets.length - 1
                width: (parent.width - parent.spacing) * 0.4
                enabled: !EditorState.preview.canvasCropMode
                from: 1
                to: 240
                unit: "fps"
                value: root.canvasFps
                onEdited: v => EditorState.projectFile.setProjectFpsRational(v, 1, false)
            }
        }
        ThemedCheckBox {
            visible: root.canvasFpsDen === 1001 && (root.canvasFpsNum === 30000 || root.canvasFpsNum === 60000)
            enabled: !EditorState.preview.canvasCropMode
            checked: root.canvasDropFrame
            text: qsTr("Drop-frame timecode (;)")
            onToggled: EditorState.projectFile.setProjectFpsRational(root.canvasFpsNum, root.canvasFpsDen, checked)
        }
    }

    ThemedLabel {
        width: parent.width
        wrapMode: Text.WordWrap
        text: qsTr("Changing size doesn’t shrink your clips — anything outside the new edges is cut off.")
    }

    ThemedLabel {
        width: parent.width
        wrapMode: Text.WordWrap
        text: qsTr("Clips keep their length. A higher rate samples more pictures per second from the same footage.")
    }
}
