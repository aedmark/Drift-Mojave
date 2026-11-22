import QtQuick
import Drift

SettingsPage {
    id: page

    pageId: "keymap"
    title: qsTr("Keyboard shortcuts")

    Text {
        width: parent.width
        visible: page.query.length === 0
        wrapMode: Text.WordWrap
        text: qsTr("Click a shortcut, then press the keys. Esc cancels, Backspace clears.")
        color: Theme.c.settingsRow.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    SettingsRow {
        title: qsTr("Keymap preset")
        searchId: "keymap-preset"
        description: qsTr("Switch between VEGAS Pro (Mojave default) and popular NLE shortcut layouts.")
        keywords: "keymap preset vegas premiere resolve final cut capcut kdenlive shotcut"
        query: page.query

        Flow {
            width: Math.min(340, page.width * 0.52)
            spacing: Theme.spacingXs

            Repeater {
                model: EditorState.keymapPresets()
                delegate: ThemedButton {
                    required property var modelData
                    variant: EditorState.activeKeymapPreset === modelData.id ? "primary" : "secondary"
                    text: modelData.name
                    tooltip: modelData.description || ""
                    onClicked: {
                        if (EditorState.applyKeymapPreset(modelData.id))
                            Toasts.success(qsTr("Applied %1 keymap preset").arg(modelData.name))
                    }
                }
            }
        }
    }

    SettingsRow {
        title: qsTr("Reset to defaults")
        searchId: "keymap-reset-defaults"
        description: qsTr("Restores every shortcut to its original key.")
        keywords: "restore original default shortcut"
        query: page.query

        ThemedButton {
            variant: "secondary"
            text: qsTr("Reset to defaults")
            glyph: Theme.icons.undo
            onClicked: {
                EditorState.resetShortcuts()
                Toasts.success(qsTr("Shortcuts reset to defaults."))
            }
        }
    }

    Repeater {
        model: EditorState.actions || []

        SettingsRow {
            required property var modelData

            title: modelData.label
            keywords: modelData.id + " " + (modelData.shortcut || "") + " shortcut key binding hotkey"
            query: page.query

            ShortcutCaptureField {
                width: 160
                actionId: modelData.id
                shortcut: modelData.shortcut
            }
        }
    }
}
