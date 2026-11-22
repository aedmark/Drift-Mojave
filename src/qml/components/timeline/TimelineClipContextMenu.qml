import QtQuick
import Drift

// Right-click menu for one timeline clip. The owning clip is passed in; the loader unloads it on close.
ThemedContextMenu {
    id: clipContextMenu

    required property var owner
    signal unloadRequested()

    property bool canPasteEffects: false
    property bool canPasteAttributes: false
    property bool canMergeTrackSubtitles: false
    property bool canRemoveSilence: false
    property bool canSyncByAudio: false
    property bool canReplaceMedia: false
    property bool canDeleteWithEffects: false
    onAboutToShow: {
        canRemoveSilence = EditorState.audioAlign.canRemoveSilence(owner.trackIndex, owner.clipIndex)
        canSyncByAudio = EditorState.audioAlign.canSyncByAudio()
        canReplaceMedia = typeof owner.panel.requestReplaceClip === "function"
                && EditorState.replaceCandidatesForClip(owner.trackIndex, owner.clipIndex).length > 0
        canPasteEffects = EditorState.clipboardHasEffects()
        // Over what the delete acts on (the selection, linked partners included), masks included.
        canDeleteWithEffects = EditorState.selectionHasPinnedAdjustments()
        canPasteAttributes = EditorState.canPasteAttributes()
        canMergeTrackSubtitles = owner.trackType === "subtitle"
                && EditorState.canMergeAllSubtitlesOnTrack(owner.trackIndex)
        rebuildMoveToTrackMenu()
        rebuildTakeMenu()
    }

    property var moveToTrackMenu: null

    function rebuildMoveToTrackMenu() {
        if (moveToTrackMenu) {
            removeMenu(moveToTrackMenu)
            moveToTrackMenu = null
        }
        const targets = EditorState.compatibleTracksForClip(owner.trackIndex, owner.clipIndex)
        if (targets.length === 0)
            return
        const sub = moveToTrackMenuComponent.createObject(null)
        for (let i = 0; i < targets.length; i++) {
            const label = targets[i].name || owner.panel.trackNameAt(targets[i].index)
            sub.addItem(moveToTrackItemComponent.createObject(null,
                { text: label, targetTrack: targets[i].index }))
        }
        let at = count
        for (let i = 0; i < count; i++) {
            if (itemAt(i) === moveToOwnTrackItem) {
                at = i + 1
                break
            }
        }
        insertMenu(at, sub)
        moveToTrackMenu = sub
    }

    Component {
        id: moveToTrackMenuComponent
        ThemedContextMenu {
            title: qsTr("Move to track")
        }
    }

    Component {
        id: moveToTrackItemComponent
        ThemedMenuItem {
            required property int targetTrack
            onTriggered: EditorState.moveClipToTrack(clipContextMenu.owner.trackIndex,
                                                     clipContextMenu.owner.clipIndex, targetTrack,
                                                     clipContextMenu.owner.clipData.start || 0)
        }
    }

    property var takeMenu: null

    function rebuildTakeMenu() {
        if (takeMenu) {
            removeMenu(takeMenu)
            takeMenu = null
        }
        if (owner.trackType !== "video" && owner.trackType !== "audio")
            return
        if (owner.isTransformClip || owner.clipData.kind === "adjustment")
            return

        const sub = takeMenuComponent.createObject(null)
        const takes = owner.clipData.takes || []
        const activeIdx = owner.clipData.activeTakeIndex || 0

        if (takes.length > 0) {
            for (let i = 0; i < takes.length; i++) {
                const t = takes[i]
                const name = t.name || (qsTr("Take %1").arg(i + 1))
                sub.addItem(takeMenuItemComponent.createObject(null, {
                    text: (i === activeIdx ? "✓  " : "    ") + name,
                    takeIndex: i
                }))
            }
            sub.addItem(takeSeparatorComponent.createObject(null))
            sub.addItem(takeActionComponent.createObject(null, {
                text: qsTr("Next Take (T)"),
                actionMethod: function() { EditorState.nextTake() }
            }))
            sub.addItem(takeActionComponent.createObject(null, {
                text: qsTr("Previous Take (Shift+T)"),
                actionMethod: function() { EditorState.previousTake() }
            }))
            sub.addItem(takeActionComponent.createObject(null, {
                text: qsTr("Delete Active Take"),
                actionMethod: function() { EditorState.deleteActiveTake(owner.trackIndex, owner.clipIndex) }
            }))
            sub.addItem(takeSeparatorComponent.createObject(null))
        }

        sub.addItem(takeActionComponent.createObject(null, {
            text: qsTr("Add Take…"),
            actionMethod: function() {
                if (typeof owner.panel.requestAddTake === "function") {
                    owner.panel.requestAddTake(owner.trackIndex, owner.clipIndex)
                }
            }
        }))
        sub.addItem(takeActionComponent.createObject(null, {
            text: qsTr("Create Multicamera Track from Selection"),
            actionMethod: function() { EditorState.createMulticamTrackFromSelection() }
        }))
        sub.addItem(takeActionComponent.createObject(null, {
            text: EditorState.inlineMulticamEnabled
                  ? qsTr("Exit Inline Multicamera Mode (Alt+Shift+C)")
                  : qsTr("Inline Multicamera Mode (Alt+Shift+C)"),
            actionMethod: function() { EditorState.toggleInlineMulticam() }
        }))

        let at = count
        for (let i = 0; i < count; i++) {
            if (itemAt(i) === replaceItem) {
                at = i + 1
                break
            }
        }
        insertMenu(at, sub)
        takeMenu = sub
    }

    Component {
        id: takeMenuComponent
        ThemedContextMenu {
            title: qsTr("Take")
        }
    }

    Component {
        id: takeMenuItemComponent
        ThemedMenuItem {
            required property int takeIndex
            onTriggered: EditorState.setActiveTake(clipContextMenu.owner.trackIndex,
                                                   clipContextMenu.owner.clipIndex,
                                                   takeIndex)
        }
    }

    Component {
        id: takeActionComponent
        ThemedMenuItem {
            required property var actionMethod
            onTriggered: actionMethod()
        }
    }

    Component {
        id: takeSeparatorComponent
        ThemedMenuSeparator {}
    }
    // Deferred: onClosed runs inside the Popup's own close path, and dropping the
    // Loader's item there would destroy an object still unwinding.
    onClosed: {
        if (takeMenu) {
            removeMenu(takeMenu)
            takeMenu = null
        }
        unloadRequested()
    }

    ThemedMenuItem {
        text: qsTr("Properties")
        icon.name: Theme.icons.sliders
        onTriggered: {
            if (!owner.selected)
                EditorState.selectClip(owner.trackIndex, owner.clipIndex)
            if (typeof owner.panel.openClipProperties === "function")
                owner.panel.openClipProperties()
        }
    }
    ThemedMenuItem {
        // Touch route into multi-clip selection; the panel owns the mode and
        // the desktop panel does not declare the property at all.
        text: qsTr("Select multiple")
        icon.name: Theme.icons.check
        visible: owner.panel.multiSelectActive === false
        onTriggered: owner.panel.multiSelectActive = true
    }
    ThemedMenuItem {
        text: qsTr("Open composite")
        icon.name: Theme.icons.layers
        visible: owner.clipData.kind === "composite"
        onTriggered: EditorState.openCompositeClip(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Flatten composite")
        icon.name: Theme.icons.film
        visible: owner.clipData.kind === "composite"
        enabled: !EditorState.exportInProgress
        onTriggered: EditorState.flattenComposite(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Dissolve composite")
        icon.name: Theme.icons.layers
        visible: owner.clipData.kind === "composite"
        enabled: EditorState.canDissolveComposite(owner.trackIndex, owner.clipIndex)
        onTriggered: EditorState.dissolveComposite(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Make composite")
        icon.name: Theme.icons.layers
        visible: EditorState.makeCompositeAvailable
        onTriggered: EditorState.makeCompositeFromSelection()
    }
    ThemedMenuItem {
        text: EditorState.selection.length > 1 ? qsTr("Transform together")
                                               : qsTr("Add transform layer")
        icon.name: Theme.icons.group
        visible: EditorState.transformTogetherAvailable && !owner.isTransformClip
        onTriggered: EditorState.makeTransformLayerFromSelection()
    }
    ThemedMenuItem {
        text: qsTr("Select transform layer")
        icon.name: Theme.icons.maximize
        visible: owner.hasTransformParent && !owner.isTransformClip
        onTriggered: {
            EditorState.selectClip(owner.trackIndex, owner.clipIndex)
            EditorState.selectTransformParent()
        }
    }
    ThemedMenuItem {
        text: qsTr("Covers…")
        icon.name: Theme.icons.layers
        visible: owner.isTransformClip
        onTriggered: {
            if (!owner.selected)
                EditorState.selectClip(owner.trackIndex, owner.clipIndex)
            if (typeof owner.panel.openClipProperties === "function")
                owner.panel.openClipProperties()
        }
    }
    ThemedMenuItem {
        text: qsTr("Select covered clips")
        icon.name: Theme.icons.maximize
        visible: owner.isTransformClip
        onTriggered: EditorState.selectTransformChildren(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuSeparator { }
    ThemedMenuItem {
        text: qsTr("Split at current time")
        icon.name: Theme.icons.scissors
        onTriggered: EditorState.splitAtPlayhead()
    }
    ThemedMenuItem {
        text: qsTr("Split item at current time")
        icon.name: Theme.icons.scissors
        // Scoped to just this clip (and its linked partner, e.g. companion
        // audio) — splitAtPlayhead() above cuts every clip under the playhead
        // across every track, which is surprising when picked from one clip's menu.
        visible: EditorState.playheadSeconds > owner.clipData.start
                && EditorState.playheadSeconds < owner.clipData.start + owner.clipData.duration
        onTriggered: EditorState.splitClipAt(owner.trackIndex, owner.clipIndex,
                                             EditorState.playheadSeconds)
    }
    ThemedMenuItem {
        text: qsTr("Separate audio")
        icon.name: Theme.icons.audioLines
        // CapCut: only offer extract when the clip still has embedded audio.
        visible: owner.trackType === "video"
                 && EditorState.canSeparateAudioClip(owner.trackIndex, owner.clipIndex)
        onTriggered: EditorState.separateAudioFromClip(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Separate all audio tracks")
        icon.name: Theme.icons.audioLines
        visible: owner.trackType === "video"
                 && EditorState.canSeparateAudioClip(owner.trackIndex, owner.clipIndex)
                 && EditorState.clipAudioStreamCount(owner.trackIndex, owner.clipIndex) > 1
        onTriggered: EditorState.separateAllAudioTracks(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        text: owner.clipData.enabled === false
              ? qsTr("Unmute / Enable event (Alt+Shift+Q)")
              : qsTr("Mute / Disable event (Alt+Shift+Q)")
        icon.name: owner.clipData.enabled === false ? Theme.icons.volumeHigh : Theme.icons.volumeOff
        onTriggered: EditorState.setClipEnabled(owner.trackIndex, owner.clipIndex, owner.clipData.enabled === false)
    }
    ThemedMenuItem {
        text: owner.clipData.locked ? qsTr("Unlock event (Alt+Shift+L)") : qsTr("Lock event (Alt+Shift+L)")
        icon.name: owner.clipData.locked ? Theme.icons.lockOpen : Theme.icons.lock
        onTriggered: EditorState.setClipLocked(owner.trackIndex, owner.clipIndex, !owner.clipData.locked)
    }
    ThemedMenuItem {
        text: qsTr("Create subclip in Media Bin")
        icon.name: Theme.icons.copyPlus
        visible: (owner.clipData.kind === "video" || owner.clipData.kind === "audio") && !owner.isTransformClip
        onTriggered: {
            const subId = EditorState.createSubclipFromTimelineClip(owner.trackIndex, owner.clipIndex)
            if (subId.length > 0)
                Toasts.success(qsTr("Subclip added to Media Bin"))
        }
    }
    ThemedMenuItem {
        text: owner.clipData.loop ? qsTr("Stop looping") : qsTr("Loop")
        icon.name: Theme.icons.repeat
        visible: owner.clipData.kind === "video" || owner.clipData.kind === "audio"
        onTriggered: EditorState.setClipLoop(owner.trackIndex, owner.clipIndex, !owner.clipData.loop)
    }
    ThemedContextMenu {
        title: qsTr("Audio Switches")
        icon.name: Theme.icons.audioLines
        visible: (owner.clipData.kind === "audio"
                  || (owner.clipData.kind === "video" && !!owner.clipData.hasEmbeddedAudio))
                 && !owner.isTransformClip

        ThemedMenuItem {
            text: qsTr("Normalize (Shift+N)")
            icon.name: Theme.icons.sliders
            onTriggered: EditorState.normalizeClipAudio(owner.trackIndex, owner.clipIndex, -0.1)
        }
        ThemedMenuItem {
            text: qsTr("Add gain envelope point at playhead")
            icon.name: Theme.icons.trendingUp
            onTriggered: EditorState.addClipVolumeEnvelopePoint(
                owner.trackIndex, owner.clipIndex,
                Math.max(0, EditorState.playheadSeconds - (owner.clipData.start || 0)), 1.0)
        }
        ThemedMenuItem {
            text: qsTr("Reset clip gain envelope")
            onTriggered: EditorState.clearClipVolumeEnvelope(owner.trackIndex, owner.clipIndex)
        }
        ThemedMenuItem {
            text: qsTr("Reset Normalization")
            visible: Math.abs((owner.clipData.normalizeGain !== undefined ? owner.clipData.normalizeGain : 1.0) - 1.0) > 0.005
            onTriggered: EditorState.resetClipNormalize(owner.trackIndex, owner.clipIndex)
        }
        ThemedMenuSeparator {}
        ThemedMenuItem {
            text: qsTr("Invert Phase")
            checkable: true
            checked: !!owner.clipData.invertPhase
            onTriggered: EditorState.setClipInvertPhase(owner.trackIndex, owner.clipIndex, !owner.clipData.invertPhase)
        }
        ThemedMenuItem {
            text: qsTr("Lock Pitch to Stretch")
            checkable: true
            checked: !!owner.clipData.lockPitchToStretch
            onTriggered: EditorState.setClipLockPitchToStretch(owner.trackIndex, owner.clipIndex, !owner.clipData.lockPitchToStretch)
        }
        ThemedContextMenu {
            title: qsTr("Channels")

            ThemedMenuItem {
                text: qsTr("Stereo (Normal)")
                checkable: true
                checked: (owner.clipData.audioChannelMode || "stereo") === "stereo"
                onTriggered: EditorState.setClipAudioChannelMode(owner.trackIndex, owner.clipIndex, "stereo")
            }
            ThemedMenuItem {
                text: qsTr("Left Only")
                checkable: true
                checked: owner.clipData.audioChannelMode === "leftOnly"
                onTriggered: EditorState.setClipAudioChannelMode(owner.trackIndex, owner.clipIndex, "leftOnly")
            }
            ThemedMenuItem {
                text: qsTr("Right Only")
                checkable: true
                checked: owner.clipData.audioChannelMode === "rightOnly"
                onTriggered: EditorState.setClipAudioChannelMode(owner.trackIndex, owner.clipIndex, "rightOnly")
            }
            ThemedMenuItem {
                text: qsTr("Combine Mono (L+R)")
                checkable: true
                checked: owner.clipData.audioChannelMode === "combineMono"
                onTriggered: EditorState.setClipAudioChannelMode(owner.trackIndex, owner.clipIndex, "combineMono")
            }
            ThemedMenuItem {
                text: qsTr("Swap Channels")
                checkable: true
                checked: owner.clipData.audioChannelMode === "swapChannels"
                onTriggered: EditorState.setClipAudioChannelMode(owner.trackIndex, owner.clipIndex, "swapChannels")
            }
        }
        ThemedContextMenu {
            title: qsTr("Pitch Shift")

            ThemedMenuItem {
                text: qsTr("+12 Semitones (+1 Octave)")
                onTriggered: EditorState.setClipPitchSemitones(owner.trackIndex, owner.clipIndex,
                                                               (owner.clipData.pitchSemitones || 0) + 12)
            }
            ThemedMenuItem {
                text: qsTr("+1 Semitone (Alt+Shift+=)")
                onTriggered: EditorState.setClipPitchSemitones(owner.trackIndex, owner.clipIndex,
                                                               (owner.clipData.pitchSemitones || 0) + 1)
            }
            ThemedMenuItem {
                text: qsTr("-1 Semitone (Alt+Shift+-)")
                onTriggered: EditorState.setClipPitchSemitones(owner.trackIndex, owner.clipIndex,
                                                               (owner.clipData.pitchSemitones || 0) - 1)
            }
            ThemedMenuItem {
                text: qsTr("-12 Semitones (-1 Octave)")
                onTriggered: EditorState.setClipPitchSemitones(owner.trackIndex, owner.clipIndex,
                                                               (owner.clipData.pitchSemitones || 0) - 12)
            }
            ThemedMenuSeparator {}
            ThemedMenuItem {
                text: qsTr("Reset Pitch (0 st)")
                enabled: Math.abs(owner.clipData.pitchSemitones || 0) > 0.01
                onTriggered: EditorState.setClipPitchSemitones(owner.trackIndex, owner.clipIndex, 0)
            }
        }
    }
    ThemedMenuItem {
        text: qsTr("Remove silence")
        icon.name: Theme.icons.audioLines
        visible: clipContextMenu.canRemoveSilence
        enabled: !EditorState.audioAlign.running
        onTriggered: EditorState.audioAlign.removeSilence(owner.trackIndex, owner.clipIndex)
    }
    ThemedContextMenu {
        title: qsTr("Sync by audio")
        icon.name: Theme.icons.audioLines
        visible: clipContextMenu.canSyncByAudio
        enabled: !EditorState.audioAlign.running

        ThemedMenuItem {
            text: qsTr("Align clips")
            onTriggered: EditorState.audioAlign.syncByAudio(false, false)
        }
        ThemedMenuItem {
            text: qsTr("Align and Group")
            onTriggered: EditorState.audioAlign.syncByAudio(true, false)
        }
        ThemedMenuItem {
            text: qsTr("Align, Group, and Mute Camera Scratch Audio")
            onTriggered: EditorState.audioAlign.syncByAudio(true, true)
        }
    }
    ThemedMenuItem {
        text: qsTr("Cancel audio analysis")
        icon.name: Theme.icons.x
        visible: EditorState.audioAlign.running
        onTriggered: EditorState.audioAlign.cancel()
    }
    ThemedMenuItem {
        readonly property string blocker: EditorState.masking.convertClipToMaskBlocker(owner.trackIndex, owner.clipIndex)
        text: blocker !== "" ? qsTr("Convert to mask") + " — " + blocker : qsTr("Convert to mask")
        icon.name: Theme.icons.mask
        visible: owner.trackType === "video"
        enabled: EditorState.masking.canConvertClipToMask(owner.trackIndex, owner.clipIndex)
        onTriggered: EditorState.masking.convertClipToMask(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        readonly property var refs: owner.selected ? [] : [{ "track": owner.trackIndex, "clip": owner.clipIndex }]
        text: qsTr("Use as mask (nested timeline)")
        icon.name: Theme.icons.mask
        visible: owner.trackType === "video" && owner.clipData.kind !== "adjustment"
        enabled: EditorState.masking.canConvertClipsToSequenceMask(refs)
        onTriggered: EditorState.masking.convertClipsToSequenceMask(refs)
    }
    ThemedMenuItem {
        readonly property string sequenceId: EditorState.masking.maskSequenceId(owner.trackIndex, owner.clipIndex)
        text: qsTr("Open mask timeline")
        icon.name: Theme.icons.layers
        visible: sequenceId !== ""
        onTriggered: EditorState.openSequence(sequenceId)
    }
    ThemedMenuItem {
        text: qsTr("Convert to Apple ProRes 422…")
        icon.name: Theme.icons.rabbit
        visible: owner.mediaAssetId.length > 0 && !owner.mediaEditFriendly
        onTriggered: EditorState.bin.convertToProRes([owner.mediaAssetId])
    }
    ThemedMenuItem {
        text: qsTr("Convert to edit-friendly format")
        icon.name: Theme.icons.rabbit
        // Converts the bin media, so every clip using it follows along.
        visible: owner.mediaAssetId.length > 0 && !owner.mediaEditFriendly
        onTriggered: EditorState.bin.convertToConstantFrameRate([owner.mediaAssetId])
    }
    ThemedMenuItem {
        text: qsTr("Crop…")
        icon.name: Theme.icons.crop
        // Desktop only: AndroidMain has no crop window for timeline clips.
        visible: (owner.clipData.kind === "video" || owner.clipData.kind === "image")
                 && typeof ShellBridge.main?.openCrop === "function"
        onTriggered: ShellBridge.main.openCrop(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Enhance video…")
        icon.name: Theme.icons.sparkles
        // Desktop only: AndroidMain has no restore window.
        visible: owner.clipData.kind === "video"
                 && typeof ShellBridge.main?.openRestore === "function"
        onTriggered: ShellBridge.main.openRestore(owner.trackIndex, owner.clipIndex)
    }
    ThemedContextMenu {
        title: qsTr("Group")
        icon.name: Theme.icons.group
        visible: !owner.isTransformClip && owner.clipData.kind !== "adjustment"

        ThemedMenuItem {
            text: qsTr("Create New (G)")
            icon.name: Theme.icons.link
            enabled: EditorState.canGroupSelection()
            onTriggered: EditorState.groupSelectedClips()
        }
        ThemedMenuItem {
            text: qsTr("Remove from Group (U)")
            icon.name: Theme.icons.unlink
            enabled: !!owner.clipData.linked || EditorState.canUngroupSelection()
            onTriggered: EditorState.ungroupSelectedClips()
        }
        ThemedMenuItem {
            text: qsTr("Clear Group")
            icon.name: Theme.icons.unlink
            enabled: !!owner.clipData.linked || EditorState.canUngroupSelection()
            onTriggered: EditorState.clearGroupSelectedClips()
        }
        ThemedMenuItem {
            text: qsTr("Select All in Group")
            icon.name: Theme.icons.group
            enabled: !!owner.clipData.linked
            onTriggered: EditorState.selectAllInGroup(owner.trackIndex, owner.clipIndex)
        }
        ThemedMenuSeparator {}
        ThemedMenuItem {
            text: (EditorState.ignoreGrouping ? "✓  " : "    ") + qsTr("Ignore Event Grouping (Ctrl+Shift+U)")
            onTriggered: EditorState.toggleIgnoreGrouping()
        }
    }
    ThemedMenuItem {
        text: qsTr("Unlink")
        icon.name: Theme.icons.unlink
        visible: !!owner.clipData.linked && EditorState.unlinkAvailable
        onTriggered: EditorState.unlinkSelectedClips()
    }
    ThemedMenuItem {
        text: qsTr("Link")
        icon.name: Theme.icons.link
        // Needs two or more clips selected (Shift+click to add).
        visible: EditorState.linkAvailable
        onTriggered: EditorState.linkSelectedClips()
    }
    ThemedMenuItem {
        text: qsTr("Merge subtitle clips")
        icon.name: Theme.icons.linkTwo
        visible: owner.trackType === "subtitle"
        enabled: EditorState.mergeAvailable
        onTriggered: EditorState.mergeSelectedClips()
    }
    ThemedMenuItem {
        text: qsTr("Merge all subtitles on this track")
        icon.name: Theme.icons.linkTwo
        visible: owner.trackType === "subtitle"
        enabled: clipContextMenu.canMergeTrackSubtitles
        onTriggered: EditorState.mergeAllSubtitlesOnTrack(owner.trackIndex)
    }
    ThemedMenuItem {
        text: qsTr("Convert to text clips")
        icon.name: Theme.icons.type
        visible: owner.trackType === "subtitle"
        onTriggered: EditorState.convertSubtitleToTextClips(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Convert to subtitle")
        icon.name: Theme.icons.captions
        visible: owner.trackType === "text" && EditorState.textToSubtitleAvailable
        onTriggered: owner.panel.requestConvertTextToSubtitle()
    }
    ThemedMenuSeparator { }
    ThemedMenuItem {
        text: qsTr("Cut")
        icon.name: Theme.icons.scissors
        onTriggered: EditorState.cutSelection()
    }
    ThemedMenuItem {
        text: qsTr("Copy")
        icon.name: Theme.icons.copy
        onTriggered: EditorState.copySelection()
    }
    ThemedMenuItem {
        text: qsTr("Paste attributes…")
        icon.name: Theme.icons.clipboardPaste
        enabled: clipContextMenu.canPasteAttributes
        onTriggered: EditorState.requestPasteAttributes()
    }
    ThemedMenuItem {
        text: qsTr("Duplicate")
        icon.name: Theme.icons.copyPlus
        onTriggered: EditorState.duplicateSelectedClip()
    }
    ThemedMenuItem {
        text: qsTr("Rename…")
        icon.name: Theme.icons.pencil
        onTriggered: owner.panel.requestRenameClip(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        id: replaceItem
        text: qsTr("Replace with…")
        icon.name: Theme.icons.repeat
        visible: clipContextMenu.canReplaceMedia
        onTriggered: owner.panel.requestReplaceClip(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuSeparator { visible: !owner.isTransformClip }
    ThemedMenuItem {
        text: qsTr("Copy effects")
        icon.name: Theme.icons.wand
        visible: owner.hasAnyEffects
        onTriggered: EditorState.copyClipEffectsToClipboard(owner.trackIndex,
                                                            owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Paste effects")
        icon.name: Theme.icons.clipboardPaste
        visible: !owner.isTransformClip
        enabled: clipContextMenu.canPasteEffects
        onTriggered: EditorState.pasteEffectsFromClipboard(owner.trackIndex,
                                                           owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Paste Event Attributes…")
        icon.name: Theme.icons.clipboardPaste
        visible: !owner.isTransformClip && EditorState.canPasteAttributes()
        onTriggered: EditorState.requestPasteAttributes()
    }
    ThemedMenuItem {
        text: qsTr("Save effects as preset…")
        icon.name: Theme.icons.save
        visible: owner.hasAnyEffects
        onTriggered: owner.panel.requestSaveEffectPreset(owner.trackIndex,
                                                             owner.clipIndex)
    }
    ThemedContextMenu {
        title: qsTr("Apply FX Chain Preset")
        icon.name: Theme.icons.sparkles
        visible: !owner.isTransformClip && EditorState.userEffectPresets().length > 0

        Repeater {
            model: EditorState.userEffectPresets()
            ThemedMenuItem {
                required property var modelData
                text: modelData.label
                onTriggered: EditorState.applyEffectPresetToSelection(modelData.id, false)
            }
        }
    }
    ThemedMenuItem {
        text: qsTr("Color grade…")
        icon.name: Theme.icons.blend
        visible: owner.clipData.kind !== "audio" && owner.clipData.kind !== "subtitle"
        onTriggered: {
            if (!owner.selected)
                EditorState.selectClip(owner.trackIndex, owner.clipIndex)
            Workspaces.activeId = "color"
        }
    }
    ThemedMenuItem {
        text: qsTr("Media FX…")
        icon.name: Theme.icons.sparkles
        visible: !owner.isTransformClip && !!owner.clipData && !!owner.clipData.assetId && owner.clipData.assetId.length > 0
        onTriggered: {
            if (typeof owner.panel.requestMediaFx === "function") {
                owner.panel.requestMediaFx(owner.clipData.assetId)
            }
        }
    }
    ThemedMenuItem {
        text: qsTr("Blur area…")
        icon.name: Theme.icons.eyeOff
        visible: owner.trackType === "video" && !owner.isTransformClip
        onTriggered: EditorState.addEffect(owner.trackIndex, owner.clipIndex,
                                           "builtin.effects.privacy_blur")
    }
    ThemedMenuItem {
        text: qsTr("Edit keyframes…")
        icon.name: Theme.icons.diamondPlus
        onTriggered: {
            if (!owner.selected)
                EditorState.selectClip(owner.trackIndex, owner.clipIndex)
            Workspaces.activeId = "keyframes"
        }
    }
    ThemedMenuItem {
        text: owner.clipData.hasVelocityEnvelope && owner.clipData.velocityEnvelopeEnabled
              ? qsTr("Remove velocity envelope")
              : qsTr("Insert velocity envelope")
        icon.name: Theme.icons.trendingUp
        visible: owner.trackType === "video" && owner.clipData.kind === "video"
        onTriggered: EditorState.toggleClipVelocityEnvelope(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        text: qsTr("Mask & track…")
        icon.name: Theme.icons.mask
        visible: owner.trackType === "video"
        onTriggered: {
            if (!owner.selected)
                EditorState.selectClip(owner.trackIndex, owner.clipIndex)
            Workspaces.activeId = "masking-tracking"
        }
    }
    ThemedMenuSeparator { visible: owner.clipData.kind === "adjustment" && !owner.isTransformClip }
    ThemedMenuItem {
        text: qsTr("Unlink from clip")
        icon.name: Theme.icons.unlink
        visible: owner.pinnedToClip
        onTriggered: EditorState.unlinkAdjustment(owner.trackIndex, owner.clipIndex)
    }
    ThemedMenuItem {
        id: moveToOwnTrackItem
        text: qsTr("Move to its own track")
        icon.name: Theme.icons.layers
        // Only meaningful for a nested one: a standalone adjustment already has one.
        visible: owner.clipData.kind === "adjustment"
                 && owner.panel.tracks[owner.trackIndex].isAdjustmentLane === true
        onTriggered: EditorState.moveAdjustmentToOwnTrack(owner.trackIndex,
                                                          owner.clipIndex)
    }
    ThemedMenuSeparator { }
    ThemedMenuItem {
        text: qsTr("Delete")
        icon.name: Theme.icons.trash
        onTriggered: EditorState.deleteSelectedClip()
    }
    ThemedMenuItem {
        text: qsTr("Ripple Delete")
        icon.name: Theme.icons.foldHorizontal
        onTriggered: EditorState.rippleDelete()
    }
    ThemedMenuItem {
        // Plain Delete leaves a graded clip's effects on the timeline, unlinked; this takes them
        // (and its masks) with it, in the same undo step.
        text: qsTr("Delete with effects")
        icon.name: Theme.icons.trash
        visible: clipContextMenu.canDeleteWithEffects
        onTriggered: EditorState.deleteSelectedClipWithEffects()
    }
}
