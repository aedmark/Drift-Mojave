import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dialog allowing the user to select which attributes from the copied clip
// (transform/motion, speed, volume, video/audio effects, transitions)
// are pasted onto the selected clips in a single undo step.
ThemedDialog {
    id: root

    title: qsTr("Paste Attributes")
    preferredWidth: Theme.dialogWidthMd
    acceptText: qsTr("Paste")
    rejectText: qsTr("Cancel")

    property var clipAttrs: ({})
    property bool pasteTransform: true
    property bool pasteSpeed: false
    property bool pasteVolume: false
    property bool pasteEffects: true
    property bool pasteAudioEffects: false
    property bool pasteTransitions: false
    property bool pasteColorGrade: false
    property bool pasteVelocity: false
    property bool pasteAudioSwitches: false
    property bool pasteEventSwitches: false
    property bool replaceEffects: false

    readonly property bool hasAnySelected: pasteTransform || pasteSpeed || pasteVolume
                                           || pasteEffects || pasteAudioEffects || pasteTransitions
                                           || pasteColorGrade || pasteVelocity || pasteAudioSwitches
                                           || pasteEventSwitches

    showAccept: hasAnySelected

    function openDialog() {
        clipAttrs = EditorState.clipboardAttributes()
        if (!clipAttrs || !clipAttrs.hasClip)
            return

        pasteTransform = (clipAttrs.hasTransform !== false)
        pasteSpeed = (clipAttrs.hasSpeed === true)
        pasteVolume = false
        pasteEffects = (clipAttrs.hasEffects === true)
        pasteAudioEffects = (clipAttrs.hasAudioEffects === true)
        pasteTransitions = (clipAttrs.hasTransitions === true)
        pasteColorGrade = false
        pasteVelocity = (clipAttrs.hasVelocity === true)
        pasteAudioSwitches = (clipAttrs.hasAudioSwitches === true)
        pasteEventSwitches = (clipAttrs.hasEventSwitches === true)
        replaceEffects = false

        open()
    }

    function selectAll() {
        if (clipAttrs.hasTransform !== false)
            pasteTransform = true
        if (clipAttrs.hasSpeed !== false)
            pasteSpeed = true
        if (clipAttrs.isAudioOrVideo !== false)
            pasteVolume = true
        if (clipAttrs.hasEffects === true)
            pasteEffects = true
        if (clipAttrs.hasAudioEffects === true)
            pasteAudioEffects = true
        if (clipAttrs.hasTransitions === true)
            pasteTransitions = true
        if (clipAttrs.hasColorGrade === true && !pasteEffects)
            pasteColorGrade = true
        if (clipAttrs.hasVelocity === true)
            pasteVelocity = true
        if (clipAttrs.isAudioOrVideo !== false)
            pasteAudioSwitches = true
        if (clipAttrs.hasClip === true)
            pasteEventSwitches = true
    }

    function selectNone() {
        pasteTransform = false
        pasteSpeed = false
        pasteVolume = false
        pasteEffects = false
        pasteAudioEffects = false
        pasteTransitions = false
        pasteColorGrade = false
        pasteVelocity = false
        pasteAudioSwitches = false
        pasteEventSwitches = false
    }

    onAccepted: {
        EditorState.pasteAttributes({
            "transform": root.pasteTransform,
            "speed": root.pasteSpeed,
            "volume": root.pasteVolume,
            "effects": root.pasteEffects,
            "audioEffects": root.pasteAudioEffects,
            "transitions": root.pasteTransitions,
            "colorGrade": root.pasteColorGrade,
            "velocity": root.pasteVelocity,
            "audioSwitches": root.pasteAudioSwitches,
            "eventSwitches": root.pasteEventSwitches,
            "replaceEffects": root.replaceEffects
        })
    }

    contentItem: Column {
        width: parent ? parent.width : Theme.dialogWidthMd
        spacing: Theme.spacingLg

        // Context header
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.c.pasteAttributesDialog.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            text: {
                const name = (root.clipAttrs && root.clipAttrs.clipName) ? root.clipAttrs.clipName : qsTr("Clip")
                const targets = (root.clipAttrs && root.clipAttrs.targetClipCount) ? root.clipAttrs.targetClipCount : 1
                return qsTr("Pasting from “%1” onto %n selected clip(s):", "", targets).arg(name)
            }
        }

        PasteAttributesVideoSection { dialog: root }

        PasteAttributesAudioSection { dialog: root }

        PasteAttributesTransitionsSection { dialog: root }

        // Options separator
        Rectangle {
            width: parent.width
            height: Theme.c.pasteAttributesDialog.borderWidth
            color: Theme.c.pasteAttributesDialog.divider
        }

        // Effect Options
        ThemedCheckBox {
            width: parent.width
            text: qsTr("Replace existing effects (instead of appending)")
            checked: root.replaceEffects
            enabled: root.pasteEffects || root.pasteAudioEffects || root.pasteColorGrade
            onToggled: root.replaceEffects = checked
        }

        // Quick selection buttons
        Row {
            spacing: Theme.spacingMd
            anchors.right: parent.right

            ThemedButton {
                variant: "ghost"
                text: qsTr("Select All")
                onClicked: root.selectAll()
            }

            ThemedButton {
                variant: "ghost"
                text: qsTr("Select None")
                onClicked: root.selectNone()
            }
        }
    }
}
