import QtQuick
import Drift

// Interactive per-clip audio gain envelope overlay (VEGAS Pro parity, Tier 5A).
// Double-click to add a gain node, drag nodes horizontally (time) and vertically (gain 0..2),
// or right-click a node to remove it.
Item {
    id: root

    required property var owner
    anchors.fill: parent

    readonly property bool carriesAudio: owner.clipData.kind === "audio"
                                      || (owner.clipData.kind === "video" && !!owner.clipData.hasEmbeddedAudio)
    readonly property bool active: !!owner.clipData.volumeKeyframed
                                && carriesAudio
                                && owner.clipData.locked !== true

    visible: active

    readonly property real topMargin: 4.0
    readonly property real bottomMargin: 4.0
    readonly property real availHeight: Math.max(1, height - topMargin - bottomMargin)
    readonly property real durationSec: Math.max(0.01, owner.effectiveDuration || owner.clipData.duration || 0.01)

    function gainToY(v) {
        const clamped = Math.max(0.0, Math.min(2.0, v))
        const norm = clamped / 2.0
        return height - bottomMargin - norm * availHeight
    }

    function yToGain(y) {
        const norm = Math.max(0.0, Math.min(1.0, (height - bottomMargin - y) / availHeight))
        let g = norm * 2.0
        // Magnetic detent near unity (1.0 / 0 dB) and silence (0.0)
        if (Math.abs(g - 1.0) < 0.06)
            g = 1.0
        else if (g < 0.04)
            g = 0.0
        return Math.max(0.0, Math.min(2.0, g))
    }

    Canvas {
        id: curveCanvas
        anchors.fill: parent
        z: 1

        Connections {
            target: root
            function onWidthChanged() { curveCanvas.requestPaint() }
            function onHeightChanged() { curveCanvas.requestPaint() }
            function onVisibleChanged() { if (root.visible) curveCanvas.requestPaint() }
        }
        Connections {
            target: owner.clipData
            function onVolumeKeysChanged() { curveCanvas.requestPaint() }
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (!root.visible || width <= 2 || height <= 2)
                return

            const pts = owner.clipData.volumeKeys || []
            if (pts.length === 0)
                return

            // Unity 0 dB dashed guide
            const unityY = root.gainToY(1.0)
            ctx.save()
            ctx.strokeStyle = "rgba(255, 255, 255, 0.22)"
            ctx.lineWidth = 1
            ctx.setLineDash([4, 4])
            ctx.beginPath()
            ctx.moveTo(0, unityY)
            ctx.lineTo(width, unityY)
            ctx.stroke()
            ctx.restore()

            ctx.save()
            ctx.strokeStyle = "#38bdf8"
            ctx.lineWidth = 2
            ctx.beginPath()
            const firstY = root.gainToY(pts[0].v !== undefined ? pts[0].v : 1.0)
            ctx.moveTo(0, firstY)
            const firstPx = Math.max(0, Math.min(width, (pts[0].t / root.durationSec) * width))
            ctx.lineTo(firstPx, firstY)
            for (let i = 1; i < pts.length; ++i) {
                const prev = pts[i - 1]
                const cur = pts[i]
                const px = Math.max(0, Math.min(width, (cur.t / root.durationSec) * width))
                const py = root.gainToY(cur.v !== undefined ? cur.v : 1.0)
                if (prev.hold) {
                    const prevY = root.gainToY(prev.v !== undefined ? prev.v : 1.0)
                    ctx.lineTo(px, prevY)
                    ctx.lineTo(px, py)
                } else if ((prev.curved || cur.curved) && cur.t > prev.t + 0.001) {
                    const steps = 12
                    for (let s = 1; s <= steps; ++s) {
                        const frac = s / steps
                        const sampleT = prev.t + (cur.t - prev.t) * frac
                        const sampleGain = EditorState.evaluateClipVolumeEnvelopeAt(owner.trackIndex, owner.clipIndex, sampleT)
                        const sx = Math.max(0, Math.min(width, (sampleT / root.durationSec) * width))
                        ctx.lineTo(sx, root.gainToY(sampleGain))
                    }
                } else {
                    ctx.lineTo(px, py)
                }
            }
            const lastY = root.gainToY(pts[pts.length - 1].v !== undefined ? pts[pts.length - 1].v : 1.0)
            ctx.lineTo(width, lastY)
            ctx.stroke()
            ctx.restore()
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        z: 1
        onDoubleClicked: function(mouse) {
            if (root.width <= 0)
                return
            const sec = Math.max(0.0, Math.min(root.durationSec, (mouse.x / root.width) * root.durationSec))
            const g = root.yToGain(mouse.y)
            EditorState.addClipVolumeEnvelopePoint(owner.trackIndex, owner.clipIndex, sec, g)
        }
    }

    Repeater {
        id: nodesRepeater
        model: owner.clipData.volumeKeys || []

        Item {
            id: nodeItem
            required property var modelData
            required property int index

            property bool dragging: false
            property real dragSec: modelData.t || 0
            property real dragGain: modelData.v !== undefined ? modelData.v : 1.0

            readonly property real displaySec: dragging ? dragSec : (modelData.t || 0)
            readonly property real displayGain: dragging ? dragGain : (modelData.v !== undefined ? modelData.v : 1.0)

            width: 14
            height: 14
            x: Math.round((displaySec / root.durationSec) * root.width - width / 2)
            y: Math.round(root.gainToY(displayGain) - height / 2)
            z: nodeMouse.pressed ? 10 : 3

            Rectangle {
                anchors.centerIn: parent
                width: 8
                height: 8
                radius: 4
                color: nodeMouse.containsMouse || nodeMouse.pressed ? "#ffffff" : "#bae6fd"
                border.color: "#0284c7"
                border.width: 1.5
            }

            ThemedToolTip {
                visible: nodeMouse.containsMouse || nodeMouse.pressed
                text: nodeItem.displayGain <= 0.001
                      ? qsTr("Clip Gain: -inf dB (0%)")
                      : qsTr("Clip Gain: %1% (%2 dB)")
                            .arg(Math.round(nodeItem.displayGain * 100))
                            .arg((20.0 * Math.log10(nodeItem.displayGain)).toFixed(1))
            }

            MouseArea {
                id: nodeMouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.PointingHandCursor
                preventStealing: true

                onPressed: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        nodeMenu.popup()
                        return
                    }
                    nodeItem.dragSec = nodeItem.modelData.t || 0
                    nodeItem.dragGain = nodeItem.modelData.v !== undefined ? nodeItem.modelData.v : 1.0
                    nodeItem.dragging = true
                }

                ThemedContextMenu {
                    id: nodeMenu

                    Repeater {
                        model: [
                            { id: "linear", label: qsTr("Curve: Linear") },
                            { id: "smooth", label: qsTr("Curve: Smooth (Spline)") },
                            { id: "fast", label: qsTr("Curve: Fast") },
                            { id: "slow", label: qsTr("Curve: Slow") },
                            { id: "sharp", label: qsTr("Curve: Sharp") },
                            { id: "hold", label: qsTr("Curve: Hold (Step)") }
                        ]
                        delegate: ThemedMenuItem {
                            required property var modelData
                            text: modelData.label
                            onTriggered: EditorState.setClipVolumeEnvelopePointCurve(
                                             owner.trackIndex, owner.clipIndex, nodeItem.index, modelData.id)
                        }
                    }
                    ThemedMenuSeparator { }
                    ThemedMenuItem {
                        text: qsTr("Delete gain node")
                        icon.name: Theme.icons.trash
                        onTriggered: EditorState.removeClipVolumeEnvelopePoint(owner.trackIndex, owner.clipIndex, nodeItem.index)
                    }
                }

                onPositionChanged: function(mouse) {
                    if (!pressed || !nodeItem.dragging || root.width <= 0)
                        return
                    const localX = nodeItem.x + mouse.x + nodeItem.width / 2
                    const localY = nodeItem.y + mouse.y + nodeItem.height / 2
                    nodeItem.dragSec = Math.max(0.0, Math.min(root.durationSec, (localX / root.width) * root.durationSec))
                    nodeItem.dragGain = root.yToGain(localY)
                }

                onReleased: function(mouse) {
                    if (mouse.button === Qt.LeftButton && nodeItem.dragging) {
                        nodeItem.dragging = false
                        EditorState.moveClipVolumeEnvelopePoint(
                            owner.trackIndex, owner.clipIndex, nodeItem.index,
                            nodeItem.dragSec, nodeItem.dragGain)
                    }
                }
            }
        }
    }
}
