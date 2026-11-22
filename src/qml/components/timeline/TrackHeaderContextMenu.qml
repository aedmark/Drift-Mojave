import QtQuick
import Drift

// Right-click menu for one track header.
ThemedContextMenu {
    id: trackContextMenu

    property var header
    property var column
    property var renameDialog
    property var deleteDialog
    signal coversRequested()

    property bool hasEmptyTracks: false
    onAboutToShow: hasEmptyTracks = EditorState.hasEmptyTracks()

    ThemedMenuItem {
        text: qsTr("Rename…")
        icon.name: Theme.icons.pencil
        onTriggered: {
            column.pendingRenameTrack = header.index
            // Custom name only — pre-filling the type+position fallback
            // ("Video 1") would persist it as a real name on an unchanged
            // confirm, then stick after the track is reordered.
            renameDialog.nameField.text = column.tracks[header.index]?.name || ""
            renameDialog.open()
        }
    }
    ThemedMenuItem {
        visible: header.laneCount > 0
        height: visible ? implicitHeight : 0
        text: header.lanesCollapsed ? qsTr("Expand child lanes") : qsTr("Collapse child lanes")
        icon.name: header.lanesCollapsed ? Theme.icons.chevronDown : Theme.icons.chevronUp
        onTriggered: EditorState.setTrackLanesCollapsed(header.index, !header.lanesCollapsed)
    }
    ThemedContextMenu {
        title: qsTr("Track Folder")
        icon.name: Theme.icons.folder
        readonly property string curFolder: (column.tracks[header.index] && column.tracks[header.index].folderName) || ""
        readonly property bool curCollapsed: !!(column.tracks[header.index] && column.tracks[header.index].folderCollapsed)

        Repeater {
            model: ["Dialogue", "Music", "SFX", "B-Roll", "Graphics"]
            delegate: ThemedMenuItem {
                required property string modelData
                text: modelData
                checked: parent.curFolder === modelData
                onTriggered: EditorState.setTrackFolder(header.index, modelData)
            }
        }
        ThemedMenuSeparator { visible: parent.curFolder.length > 0 }
        ThemedMenuItem {
            visible: parent.curFolder.length > 0
            text: parent.curCollapsed ? qsTr("Expand Folder") : qsTr("Collapse Folder")
            onTriggered: EditorState.setTrackFolderCollapsed(parent.curFolder, !parent.curCollapsed)
        }
        ThemedMenuItem {
            visible: parent.curFolder.length > 0
            text: qsTr("Remove from Folder")
            onTriggered: EditorState.setTrackFolder(header.index, "")
        }
    }
    ThemedMenuSeparator {}
    ThemedMenuItem {
        text: header.trackMuted ? qsTr("Unmute track")
                                       : qsTr("Mute track")
        icon.name: header.trackMuted ? Theme.icons.volumeHigh
                                            : Theme.icons.volumeOff
        onTriggered: EditorState.setTrackMuted(header.index, !header.trackMuted)
    }
    ThemedMenuItem {
        visible: header.wantSolo
        text: header.trackSolo ? qsTr("Unsolo track") : qsTr("Solo track")
        icon.name: Theme.icons.headphones
        onTriggered: EditorState.setTrackSolo(header.index, !header.trackSolo)
    }
    ThemedMenuItem {
        visible: header.wantMic
        text: header.isRecordingHere ? qsTr("Stop recording") : qsTr("Record voiceover")
        icon.name: Theme.icons.mic
        onTriggered: {
            if (EditorState.isRecordingAudio)
                EditorState.stopAudioRecording()
            else
                EditorState.startAudioRecording(header.index)
        }
    }
    ThemedMenuItem {
        visible: header.wantLanes
        text: header.lanesOn ? qsTr("Show one combined waveform") : qsTr("Show each channel separately")
        icon.name: Theme.icons.split
        onTriggered: {
            const on = !header.lanesOn
            EditorState.setTrackShowChannelWaveforms(header.index, on)
            if (on)
                column.growRowForChannelLanes(header.index, header.laneChannels)
        }
    }
    ThemedMenuItem {
        text: header.trackHidden ? qsTr("Show track")
                                        : qsTr("Hide track")
        icon.name: header.trackHidden ? Theme.icons.eye
                                             : Theme.icons.eyeOff
        onTriggered: EditorState.setTrackHidden(header.index, !header.trackHidden)
    }
    ThemedMenuItem {
        text: header.trackLocked ? qsTr("Unlock track") : qsTr("Lock track")
        icon.name: header.trackLocked ? Theme.icons.lockOpen : Theme.icons.lock
        onTriggered: EditorState.setTrackLocked(header.index, !header.trackLocked)
    }
    // A review track: still shown and heard while editing, never exported. Not offered for a
    // transform or camera layer, whose absence would silently reframe everything under it.
    ThemedMenuItem {
        visible: header.trackType === "video" || header.trackType === "audio"
                 || header.trackType === "text" || header.trackType === "shape"
                 || header.trackType === "subtitle"
        text: header.trackExcludedFromExport ? qsTr("Include in export") : qsTr("Exclude from export")
        icon.name: Theme.icons.film
        onTriggered: EditorState.setTrackExcludedFromExport(header.index, !header.trackExcludedFromExport)
    }
    ThemedContextMenu {
        title: qsTr("Envelopes")
        icon.name: Theme.icons.trendingUp
        visible: (header.trackType === "video" || header.trackType === "audio"
                  || header.trackType === "text" || header.trackType === "shape"
                  || header.trackType === "subtitle")
                 && !header.isTransformLayer && !header.isCameraLayer

        ThemedMenuItem {
            visible: header.trackType === "audio" || header.trackType === "video"
            text: qsTr("Volume Envelope (Shift+V)")
            checkable: true
            checked: column.tracks[header.index]?.showVolumeEnvelope === true
            onTriggered: EditorState.toggleTrackEnvelope(header.index, "volume")
        }
        ThemedMenuItem {
            visible: header.trackType === "audio" || header.trackType === "video"
            text: qsTr("Pan Envelope (Shift+P)")
            checkable: true
            checked: column.tracks[header.index]?.showPanEnvelope === true
            onTriggered: EditorState.toggleTrackEnvelope(header.index, "pan")
        }
        ThemedMenuItem {
            visible: header.trackType === "video" || header.trackType === "text"
                     || header.trackType === "shape" || header.trackType === "subtitle"
            text: qsTr("Composite Level Envelope (Alt+Shift+V)")
            checkable: true
            checked: column.tracks[header.index]?.showCompositeEnvelope === true
            onTriggered: EditorState.toggleTrackEnvelope(header.index, "composite")
        }
        ThemedMenuItem {
            visible: header.trackType === "audio" || header.trackType === "video"
            text: qsTr("Mute Envelope (Alt+Shift+M)")
            checkable: true
            checked: column.tracks[header.index]?.showMuteEnvelope === true
            onTriggered: EditorState.toggleTrackEnvelope(header.index, "mute")
        }
        ThemedMenuSeparator {}
        ThemedMenuItem {
            text: qsTr("Clear All Envelopes")
            icon.name: Theme.icons.trash
            enabled: (column.tracks[header.index]?.volumeEnvelopePoints?.length || 0) > 0
                     || (column.tracks[header.index]?.panEnvelopePoints?.length || 0) > 0
                     || (column.tracks[header.index]?.compositeEnvelopePoints?.length || 0) > 0
                     || (column.tracks[header.index]?.muteEnvelopePoints?.length || 0) > 0
            onTriggered: EditorState.clearAllTrackEnvelopes(header.index)
        }
    }
    ThemedMenuSeparator {
        visible: header.isTransformLayer || header.transformable
                 || header.isCameraLayer
    }
    // Back-to-back camera clips are cuts between framings; each new one starts at rest.
    ThemedMenuItem {
        visible: header.isCameraLayer
        text: qsTr("Add camera clip at playhead")
        icon.name: Theme.icons.video
        onTriggered: EditorState.addCameraClip()
    }
    // A submenu cannot be hidden per row, so the span choices open as their
    // own menu from here.
    ThemedMenuItem {
        visible: header.isTransformLayer
        text: qsTr("Covers…")
        icon.name: Theme.icons.layers
        onTriggered: trackContextMenu.coversRequested()
    }
    ThemedMenuItem {
        visible: header.isTransformLayer
        text: qsTr("Select covered clips")
        icon.name: Theme.icons.maximize
        onTriggered: column.selectCoveredClips(header.index)
    }
    ThemedMenuItem {
        visible: header.transformable
        text: qsTr("Add transform layer above")
        icon.name: Theme.icons.maximize
        onTriggered: EditorState.addTransformLayerAbove(header.index)
    }
    ThemedContextMenu {
        id: trackVideoFxMenu
        title: qsTr("Track Video FX")
        icon.name: Theme.icons.sparkles
        visible: header.transformable

        Repeater {
            model: (column.tracks[header.index]?.effects || []).length
            delegate: ThemedMenuItem {
                required property int index
                readonly property var effectItem: (column.tracks[header.index]?.effects || [])[index] || ({})
                text: (effectItem.label || effectItem.catalogId || qsTr("Effect"))
                      + (effectItem.enabled === false ? qsTr(" [Bypassed]") : "")
                icon.name: effectItem.enabled !== false ? Theme.icons.check : Theme.icons.slash
                onTriggered: EditorState.setTrackVideoEffectEnabled(header.index, index, effectItem.enabled === false)
            }
        }
        ThemedMenuSeparator { visible: (column.tracks[header.index]?.effects || []).length > 0 }
        ThemedContextMenu {
            id: trackAddVideoFxSub
            title: qsTr("Add video effect…")
            Instantiator {
                model: EditorState.effectCatalog()
                delegate: ThemedMenuItem {
                    required property var modelData
                    text: modelData.displayName || modelData.name || modelData.id || ""
                    onTriggered: EditorState.addTrackVideoEffect(header.index, modelData.id)
                }
                onObjectAdded: (idx, item) => trackAddVideoFxSub.insertItem(idx, item)
                onObjectRemoved: (idx, item) => trackAddVideoFxSub.removeItem(item)
            }
        }
        ThemedMenuItem {
            visible: (column.tracks[header.index]?.effects || []).length > 0
            text: qsTr("Remove all video effects")
            icon.name: Theme.icons.trash
            onTriggered: {
                const fx = column.tracks[header.index]?.effects || []
                for (let i = fx.length - 1; i >= 0; i--)
                    EditorState.removeTrackVideoEffect(header.index, i)
            }
        }
    }
    ThemedContextMenu {
        title: qsTr("Track Motion")
        icon.name: Theme.icons.move
        visible: header.transformable

        ThemedMenuItem {
            readonly property bool motionOn: column.tracks[header.index]?.motion?.enabled === true
            text: motionOn ? qsTr("Disable track motion") : qsTr("Enable track motion")
            icon.name: motionOn ? Theme.icons.check : ""
            onTriggered: EditorState.setTrackMotion(header.index, { enabled: !motionOn })
        }
        ThemedMenuSeparator {}
        ThemedMenuItem {
            text: qsTr("PiP Top-Right (30%)")
            onTriggered: EditorState.setTrackMotion(header.index, { enabled: true, scaleX: 0.3, scaleY: 0.3, posX: 0.32, posY: -0.32, rotationDeg: 0.0 })
        }
        ThemedMenuItem {
            text: qsTr("PiP Top-Left (30%)")
            onTriggered: EditorState.setTrackMotion(header.index, { enabled: true, scaleX: 0.3, scaleY: 0.3, posX: -0.32, posY: -0.32, rotationDeg: 0.0 })
        }
        ThemedMenuItem {
            text: qsTr("PiP Bottom-Right (30%)")
            onTriggered: EditorState.setTrackMotion(header.index, { enabled: true, scaleX: 0.3, scaleY: 0.3, posX: 0.32, posY: 0.32, rotationDeg: 0.0 })
        }
        ThemedMenuItem {
            text: qsTr("PiP Bottom-Left (30%)")
            onTriggered: EditorState.setTrackMotion(header.index, { enabled: true, scaleX: 0.3, scaleY: 0.3, posX: -0.32, posY: 0.32, rotationDeg: 0.0 })
        }
        ThemedMenuItem {
            text: qsTr("Center 50%")
            onTriggered: EditorState.setTrackMotion(header.index, { enabled: true, scaleX: 0.5, scaleY: 0.5, posX: 0.0, posY: 0.0, rotationDeg: 0.0 })
        }
        ThemedMenuSeparator {}
        ThemedMenuItem {
            text: qsTr("Reset track motion")
            onTriggered: EditorState.resetTrackMotion(header.index)
        }
    }
    ThemedContextMenu {
        id: compositeParentMenu
        title: qsTr("Composite Parent Track")
        icon.name: Theme.icons.layers
        visible: header.transformable

        ThemedMenuItem {
            text: qsTr("None (Independent)")
            icon.name: (column.tracks[header.index]?.compositeParentTrackId || "") === "" ? Theme.icons.check : ""
            onTriggered: EditorState.setTrackCompositeParent(header.index, "")
        }
        Repeater {
            model: column.tracks.length
            delegate: ThemedMenuItem {
                required property int index
                readonly property var cand: column.tracks[index] || ({})
                visible: index !== header.index && (cand.type === "video" || cand.type === "text" || cand.type === "shape")
                text: cand.name && cand.name.length > 0 ? cand.name : (qsTr("Track %1").arg(index + 1))
                icon.name: (column.tracks[header.index]?.compositeParentTrackId || "") === cand.id ? Theme.icons.check : ""
                onTriggered: EditorState.setTrackCompositeParent(header.index, cand.id || "")
            }
        }
    }
    ThemedContextMenu {
        title: qsTr("Track Composite Mode")
        visible: header.transformable

        Repeater {
            model: [
                { label: qsTr("Normal"), id: "normal" },
                { label: qsTr("Multiply"), id: "multiply" },
                { label: qsTr("Screen"), id: "screen" },
                { label: qsTr("Overlay"), id: "overlay" },
                { label: qsTr("Add"), id: "add" },
                { label: qsTr("Soft Light"), id: "softLight" },
                { label: qsTr("Difference"), id: "difference" }
            ]
            delegate: ThemedMenuItem {
                required property var modelData
                text: modelData.label
                icon.name: (column.tracks[header.index]?.compositeBlendMode || "normal") === modelData.id ? Theme.icons.check : ""
                onTriggered: EditorState.setTrackCompositeBlendMode(header.index, modelData.id)
            }
        }
    }
    ThemedMenuSeparator {
        visible: column.tracks[header.index]?.type === "video"
    }
    // The active entry swaps its icon for a tick, like the header's
    // Settings menu.
    ThemedMenuItem {
        visible: column.tracks[header.index]?.type === "video"
        text: column.clipDisplayLabel(0)
        icon.name: header.clipDisplay === 0 ? Theme.icons.check
                                                   : column.clipDisplayGlyph(0)
        onTriggered: EditorState.setTrackClipDisplay(header.index, 0)
    }
    ThemedMenuItem {
        visible: column.tracks[header.index]?.type === "video"
        text: column.clipDisplayLabel(1)
        icon.name: header.clipDisplay === 1 ? Theme.icons.check
                                                   : column.clipDisplayGlyph(1)
        onTriggered: EditorState.setTrackClipDisplay(header.index, 1)
    }
    ThemedMenuItem {
        visible: column.tracks[header.index]?.type === "video"
        text: column.clipDisplayLabel(2)
        icon.name: header.clipDisplay === 2 ? Theme.icons.check
                                                   : column.clipDisplayGlyph(2)
        onTriggered: EditorState.setTrackClipDisplay(header.index, 2)
    }
    ThemedMenuSeparator {}
    ThemedContextMenu {
        title: qsTr("Track height")

        ThemedMenuItem {
            text: qsTr("Short")
            icon.name: column.isHeightPreset(header.index, 0.6)
                       ? Theme.icons.check : Theme.icons.listChevronsDownUp
            onTriggered: EditorState.setTrackHeightScale(header.index, 0.6)
        }
        ThemedMenuItem {
            text: qsTr("Tall")
            icon.name: column.isHeightPreset(header.index, 1.0)
                       ? Theme.icons.check : Theme.icons.list
            onTriggered: EditorState.setTrackHeightScale(header.index, 1.0)
        }
        ThemedMenuItem {
            text: qsTr("Taller")
            icon.name: column.isHeightPreset(header.index, 1.6)
                       ? Theme.icons.check : Theme.icons.listChevronsUpDown
            onTriggered: EditorState.setTrackHeightScale(header.index, 1.6)
        }
        ThemedMenuSeparator {
            visible: !column.touchHold
        }
        ThemedMenuItem {
            visible: !column.touchHold
            enabled: false
            text: qsTr("Drag the bottom edge or scroll over the header to fine-tune")
            icon.name: Theme.icons.info
        }
    }
    // Desktop fine-tunes a lane by wheeling over its header. Touch has no
    // wheel, so the same nudge is offered explicitly.
    ThemedMenuItem {
        visible: column.touchHold
        text: qsTr("Taller row")
        icon.name: Theme.icons.chevronUp
        enabled: (column.tracks[header.index]?.heightScale ?? 1.0) < EditorState.trackHeightScaleMax()
        onTriggered: EditorState.nudgeTrackHeightScale(header.index, 1)
    }
    ThemedMenuItem {
        visible: column.touchHold
        text: qsTr("Shorter row")
        icon.name: Theme.icons.chevronDown
        enabled: (column.tracks[header.index]?.heightScale ?? 1.0) > EditorState.trackHeightScaleMin()
        onTriggered: EditorState.nudgeTrackHeightScale(header.index, -1)
    }
    ThemedMenuSeparator {}
    ThemedMenuItem {
        text: qsTr("Delete track")
        icon.name: Theme.icons.trash
        onTriggered: {
            column.pendingDeleteTrack = header.index
            deleteDialog.open()
        }
    }
    ThemedMenuItem {
        text: qsTr("Remove empty tracks")
        icon.name: Theme.icons.trash
        enabled: trackContextMenu.hasEmptyTracks
        onTriggered: EditorState.removeEmptyTracks()
    }
}
