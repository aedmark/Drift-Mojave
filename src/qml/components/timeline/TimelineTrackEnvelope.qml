import QtQuick
import Drift

// Track-level VEGAS automation envelope overlay (Volume, Pan, Composite Level, Mute).
// Point handles are directly draggable with live preview and single-step undo on release.
Item {
    id: root

    required property var panel
    required property int trackIndex
    required property var trackData

    readonly property bool showVolume: !!trackData && trackData.showVolumeEnvelope === true
    readonly property bool showPan: !!trackData && trackData.showPanEnvelope === true
    readonly property bool showComposite: !!trackData && trackData.showCompositeEnvelope === true
    readonly property bool showMute: !!trackData && trackData.showMuteEnvelope === true
    readonly property bool anyVisible: showVolume || showPan || showComposite || showMute

    visible: anyVisible && width > 0 && height > 8
    clip: true

    readonly property real padY: 5.0
    readonly property real usableH: Math.max(1.0, height - padY * 2.0)

    function valueToY(kind, v) {
        let norm = 0.5
        if (kind === "volume")
            norm = Math.max(0.0, Math.min(1.0, v / 2.0))
        else if (kind === "pan")
            norm = Math.max(0.0, Math.min(1.0, (v + 1.0) * 0.5))
        else if (kind === "composite")
            norm = Math.max(0.0, Math.min(1.0, v))
        else if (kind === "mute")
            norm = v < 0.5 ? 0.08 : 0.92
        return padY + (1.0 - norm) * usableH
    }

    function yToValue(kind, y) {
        const norm = Math.max(0.0, Math.min(1.0, 1.0 - (y - padY) / usableH))
        if (kind === "volume") {
            let v = norm * 2.0
            if (Math.abs(v - 1.0) < 0.04)
                v = 1.0
            return Math.max(0.0, Math.min(2.0, v))
        }
        if (kind === "pan") {
            let v = norm * 2.0 - 1.0
            if (Math.abs(v) < 0.04)
                v = 0.0
            return Math.max(-1.0, Math.min(1.0, v))
        }
        if (kind === "composite") {
            if (norm > 0.97) return 1.0
            if (norm < 0.03) return 0.0
            return norm
        }
        if (kind === "mute")
            return norm < 0.5 ? 0.0 : 1.0
        return norm
    }

    function formatValue(kind, v) {
        if (kind === "volume") {
            if (v <= 0.0001) return qsTr("Volume: -inf dB")
            const db = 20.0 * Math.log(v) / Math.LN10
            return qsTr("Volume: %1%2 dB").arg(db > 0.05 ? "+" : "").arg(db.toFixed(1))
        }
        if (kind === "pan") {
            if (Math.abs(v) < 0.01) return qsTr("Pan: Center")
            return v < 0 ? qsTr("Pan: %1% L").arg(Math.round(-v * 100))
                         : qsTr("Pan: %1% R").arg(Math.round(v * 100))
        }
        if (kind === "composite")
            return qsTr("Composite: %1%").arg(Math.round(v * 100))
        if (kind === "mute")
            return v < 0.5 ? qsTr("Mute: Muted") : qsTr("Mute: Active")
        return ""
    }

    function strokeColor(kind) {
        switch (kind) {
        case "volume": return "#3b82f6"
        case "pan": return "#f59e0b"
        case "composite": return "#a855f7"
        case "mute": return "#ef4444"
        default: return "#94a3b8"
        }
    }

    function primaryVisibleKind() {
        if (showVolume) return "volume"
        if (showComposite) return "composite"
        if (showPan) return "pan"
        if (showMute) return "mute"
        return ""
    }

    readonly property var flattenedPoints: {
        const list = []
        const appendKind = (kind, pts, shown) => {
            if (!shown || !pts)
                return
            for (let i = 0; i < pts.length; ++i) {
                list.push({
                    kind: kind,
                    time: pts[i].time,
                    value: pts[i].value,
                    hold: !!pts[i].hold,
                    color: strokeColor(kind)
                })
            }
        }
        if (!trackData)
            return list
        appendKind("volume", trackData.volumeEnvelopePoints, showVolume)
        appendKind("pan", trackData.panEnvelopePoints, showPan)
        appendKind("composite", trackData.compositeEnvelopePoints, showComposite)
        appendKind("mute", trackData.muteEnvelopePoints, showMute)
        return list
    }

    onFlattenedPointsChanged: envelopeCanvas.requestPaint()
    onWidthChanged: envelopeCanvas.requestPaint()
    onHeightChanged: envelopeCanvas.requestPaint()

    Connections {
        target: panel
        function onPxPerSecondChanged() { envelopeCanvas.requestPaint() }
    }

    Canvas {
        id: envelopeCanvas
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (!root.anyVisible)
                return

            const pps = root.panel ? root.panel.pxPerSecond : 100
            const drawCurve = (kind, pts, shown) => {
                if (!shown || !pts || pts.length === 0)
                    return
                ctx.save()
                ctx.strokeStyle = root.strokeColor(kind)
                ctx.lineWidth = 2.0
                ctx.beginPath()
                const firstX = Math.max(0, pts[0].time * pps)
                const firstY = root.valueToY(kind, pts[0].value)
                ctx.moveTo(0, firstY)
                ctx.lineTo(firstX, firstY)
                for (let i = 1; i < pts.length; ++i) {
                    const prev = pts[i - 1]
                    const cur = pts[i]
                    const x = cur.time * pps
                    const y = root.valueToY(kind, cur.value)
                    if (kind === "mute" || prev.hold) {
                        const prevY = root.valueToY(kind, prev.value)
                        ctx.lineTo(x, prevY)
                    }
                    ctx.lineTo(x, y)
                }
                const lastY = root.valueToY(kind, pts[pts.length - 1].value)
                ctx.lineTo(width, lastY)
                ctx.stroke()
                ctx.restore()
            }

            if (!trackData)
                return
            drawCurve("volume", trackData.volumeEnvelopePoints, root.showVolume)
            drawCurve("pan", trackData.panEnvelopePoints, root.showPan)
            drawCurve("composite", trackData.compositeEnvelopePoints, root.showComposite)
            drawCurve("mute", trackData.muteEnvelopePoints, root.showMute)
        }
    }

    // Compact "+ Key" pill so users can drop an envelope point at the playhead without blocking
    // normal clip clicks or drags anywhere on the track body.
    Rectangle {
        id: addPointChip
        visible: root.anyVisible && root.height >= 26 && (!trackData || trackData.locked !== true)
        x: Math.max(6, Math.min(root.width - width - 6,
                                (EditorState.playheadSeconds * (root.panel ? root.panel.pxPerSecond : 100)) + 8))
        y: 4
        width: addPointText.implicitWidth + 12
        height: 18
        radius: 9
        color: addPointMouse.containsMouse ? Qt.rgba(0.18, 0.22, 0.30, 0.92) : Qt.rgba(0.10, 0.12, 0.17, 0.80)
        border.color: root.strokeColor(root.primaryVisibleKind())
        border.width: 1
        z: 3

        Text {
            id: addPointText
            anchors.centerIn: parent
            text: qsTr("+ Key")
            color: "#e2e8f0"
            font.family: Theme.fontFamily
            font.pixelSize: 10
        }

        ThemedToolTip {
            visible: addPointMouse.containsMouse
            text: qsTr("Insert track envelope keyframe at playhead")
        }

        MouseArea {
            id: addPointMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                const kind = root.primaryVisibleKind()
                if (!kind)
                    return
                const defVal = kind === "pan" ? 0.0 : 1.0
                EditorState.addTrackEnvelopePoint(root.trackIndex, kind, EditorState.playheadSeconds, defVal)
            }
        }
    }

    Repeater {
        model: root.flattenedPoints ? root.flattenedPoints.length : 0

        Item {
            id: ptItem
            required property int index
            readonly property var pt: root.flattenedPoints[index] || ({ kind: "volume", time: 0, value: 1, hold: false, color: "#94a3b8" })
            readonly property string effectiveKind: ptMouse.dragging ? ptMouse.dragKind : pt.kind
            readonly property real effectiveTime: ptMouse.dragging ? ptMouse.dragTime : pt.time
            readonly property real effectiveValue: ptMouse.dragging ? ptMouse.dragValue : pt.value

            readonly property real pps: root.panel ? root.panel.pxPerSecond : 100
            width: 14
            height: 14
            x: Math.round(effectiveTime * pps - width / 2)
            y: Math.round(root.valueToY(effectiveKind, effectiveValue) - height / 2)
            z: ptMouse.dragging ? 20 : 5

            Rectangle {
                anchors.centerIn: parent
                width: ptMouse.containsMouse || ptMouse.dragging ? 10 : 8
                height: width
                radius: ptItem.effectiveKind === "mute" ? 1 : width / 2
                color: ptMouse.containsMouse || ptMouse.dragging ? "#ffffff" : root.strokeColor(ptItem.effectiveKind)
                border.color: "#0f172a"
                border.width: 1.5
            }

            ThemedToolTip {
                visible: ptMouse.containsMouse || ptMouse.dragging
                text: root.formatValue(ptItem.effectiveKind, ptItem.effectiveValue)
            }

            MouseArea {
                id: ptMouse
                anchors.fill: parent
                enabled: trackData.locked !== true
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.PointingHandCursor
                preventStealing: true

                property bool dragging: false
                property string dragKind: ""
                property real pressTime: 0
                property real dragTime: 0
                property real dragValue: 0

                onPressed: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        ptMenu.popup()
                        return
                    }
                    dragKind = ptItem.pt.kind
                    pressTime = ptItem.pt.time
                    dragTime = pressTime
                    dragValue = ptItem.pt.value
                    dragging = true
                    EditorState.beginPreviewDrag(qsTr("Edit track envelope"))
                }

                onPositionChanged: function(mouse) {
                    if (!dragging)
                        return
                    const inRoot = ptItem.mapToItem(root, mouse.x, mouse.y)
                    const nextT = Math.max(0.0, inRoot.x / Math.max(1.0, ptItem.pps))
                    const nextV = root.yToValue(dragKind, inRoot.y)
                    dragTime = nextT
                    dragValue = nextV
                    EditorState.previewMoveTrackEnvelopePoint(root.trackIndex, dragKind,
                                                              pressTime, nextT, nextV)
                    pressTime = nextT
                }

                onReleased: function(mouse) {
                    if (mouse.button === Qt.LeftButton && dragging) {
                        dragging = false
                        EditorState.commitMoveTrackEnvelopePoint(root.trackIndex, dragKind,
                                                                 pressTime, dragTime, dragValue)
                    }
                }

                onCanceled: {
                    if (dragging) {
                        dragging = false
                        EditorState.cancelPreviewDrag()
                    }
                }

                ThemedContextMenu {
                    id: ptMenu

                    ThemedMenuItem {
                        text: ptItem.pt.kind === "mute"
                              ? (ptItem.pt.value < 0.5 ? qsTr("Unmute here") : qsTr("Mute here"))
                              : qsTr("Reset to default")
                        onTriggered: {
                            const val = ptItem.pt.kind === "mute"
                                        ? (ptItem.pt.value < 0.5 ? 1.0 : 0.0)
                                        : (ptItem.pt.kind === "pan" ? 0.0 : 1.0)
                            EditorState.commitMoveTrackEnvelopePoint(root.trackIndex, ptItem.pt.kind,
                                                                     ptItem.pt.time, ptItem.pt.time, val)
                        }
                    }
                    ThemedMenuItem {
                        text: qsTr("Delete point")
                        icon.name: Theme.icons.trash
                        onTriggered: EditorState.removeTrackEnvelopePoint(root.trackIndex,
                                                                          ptItem.pt.kind,
                                                                          ptItem.pt.time)
                    }
                }
            }
        }
    }
}
