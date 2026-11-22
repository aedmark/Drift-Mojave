import QtQuick
import Drift

// Dockable Video Scopes HUD (Histogram, Luma Waveform, RGB Parade, Vectorscope).
Rectangle {
    id: root

    visible: EditorState.preview.videoScopesVisible
    anchors.bottom: parent ? parent.bottom : undefined
    anchors.right: parent ? parent.right : undefined
    anchors.margins: Theme.spacingMd
    width: 280
    height: 176
    z: 12
    radius: Theme.radiusSm
    color: Qt.rgba(0.05, 0.06, 0.09, 0.90)
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, 0.14)

    property var scopeData: ({ valid: false })

    function refreshScopes() {
        if (!root.visible)
            return
        scopeData = EditorState.preview.computeVideoScopes(48, 64)
        scopeCanvas.requestPaint()
    }

    onVisibleChanged: if (visible) refreshScopes()

    Connections {
        target: EditorState
        function onPlayheadSecondsChanged() { refreshTimer.restart() }
        function onTracksRevisionChanged() { refreshTimer.restart() }
    }
    Connections {
        target: EditorState.preview
        function onVideoScopeModeChanged() { root.refreshScopes() }
    }

    Timer {
        id: refreshTimer
        interval: 60
        repeat: false
        onTriggered: root.refreshScopes()
    }

    Row {
        id: modeBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 6
        spacing: 4
        height: 22

        Repeater {
            model: [
                { id: "histogram", label: qsTr("Hist") },
                { id: "waveform", label: qsTr("Wave") },
                { id: "parade", label: qsTr("Parade") },
                { id: "vectorscope", label: qsTr("Vector") }
            ]

            delegate: Rectangle {
                required property var modelData
                readonly property bool selected: EditorState.preview.videoScopeMode === modelData.id
                width: chipLabel.implicitWidth + 12
                height: 20
                radius: 4
                color: selected ? Theme.accent : Qt.rgba(1, 1, 1, 0.08)

                Text {
                    id: chipLabel
                    anchors.centerIn: parent
                    text: modelData.label
                    color: selected ? "#0b0f19" : "#e2e8f0"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeTiny
                    font.weight: Font.DemiBold
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: EditorState.preview.videoScopeMode = modelData.id
                }
            }
        }

        Item { width: Math.max(4, parent.width - 210); height: 1 }

        IconButton {
            width: 20
            height: 20
            glyph: Theme.icons.x
            variant: "ghost"
            tooltip: qsTr("Close Video Scopes")
            onClicked: EditorState.preview.videoScopesVisible = false
        }
    }

    Canvas {
        id: scopeCanvas
        anchors.top: modeBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statsRow.top
        anchors.margins: 6

        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (width <= 4 || height <= 4)
                return

            const data = root.scopeData
            // Subtle IRE graticule lines
            ctx.save()
            ctx.strokeStyle = "rgba(255, 255, 255, 0.08)"
            ctx.lineWidth = 1
            for (let g = 1; g < 4; ++g) {
                const gy = (height * g) / 4.0
                ctx.beginPath()
                ctx.moveTo(0, gy)
                ctx.lineTo(width, gy)
                ctx.stroke()
            }
            ctx.restore()

            if (!data || !data.valid)
                return

            const mode = EditorState.preview.videoScopeMode
            if (mode === "histogram") {
                const drawHist = (bins, stroke) => {
                    if (!bins || bins.length === 0)
                        return
                    ctx.save()
                    ctx.strokeStyle = stroke
                    ctx.lineWidth = 1.5
                    ctx.beginPath()
                    for (let i = 0; i < bins.length; ++i) {
                        const x = (i / Math.max(1, bins.length - 1)) * width
                        const y = height - Math.max(0, Math.min(1, bins[i])) * (height - 2)
                        if (i === 0) ctx.moveTo(x, y)
                        else ctx.lineTo(x, y)
                    }
                    ctx.stroke()
                    ctx.restore()
                }
                drawHist(data.histR, "rgba(248, 113, 113, 0.85)")
                drawHist(data.histG, "rgba(74, 222, 128, 0.85)")
                drawHist(data.histB, "rgba(96, 165, 250, 0.85)")
                drawHist(data.histLuma, "rgba(241, 245, 249, 0.9)")
            } else if (mode === "waveform") {
                const cols = data.waveformLuma || []
                const step = width / Math.max(1, cols.length)
                ctx.save()
                for (let i = 0; i < cols.length; ++i) {
                    const c = cols[i]
                    const x = i * step
                    const yTop = height - c.max * (height - 2)
                    const yBot = height - c.min * (height - 2)
                    const yAvg = height - c.avg * (height - 2)
                    ctx.fillStyle = "rgba(56, 189, 248, 0.25)"
                    ctx.fillRect(x, yTop, Math.max(1, step - 0.5), Math.max(1, yBot - yTop))
                    ctx.fillStyle = "rgba(224, 242, 254, 0.95)"
                    ctx.fillRect(x, yAvg - 1, Math.max(1, step - 0.5), 2)
                }
                ctx.restore()
            } else if (mode === "parade") {
                const drawChannel = (cols, xOffset, bandW, fill, peakFill) => {
                    if (!cols || cols.length === 0)
                        return
                    const step = bandW / Math.max(1, cols.length)
                    for (let i = 0; i < cols.length; ++i) {
                        const c = cols[i]
                        const x = xOffset + i * step
                        const yTop = height - c.max * (height - 2)
                        const yBot = height - c.min * (height - 2)
                        const yAvg = height - c.avg * (height - 2)
                        ctx.fillStyle = fill
                        ctx.fillRect(x, yTop, Math.max(1, step - 0.5), Math.max(1, yBot - yTop))
                        ctx.fillStyle = peakFill
                        ctx.fillRect(x, yAvg - 1, Math.max(1, step - 0.5), 2)
                    }
                }
                const bandW = (width - 6) / 3.0
                drawChannel(data.paradeR, 0, bandW, "rgba(248, 113, 113, 0.30)", "rgba(254, 202, 202, 0.95)")
                drawChannel(data.paradeG, bandW + 3, bandW, "rgba(74, 222, 128, 0.30)", "rgba(187, 247, 208, 0.95)")
                drawChannel(data.paradeB, (bandW + 3) * 2, bandW, "rgba(96, 165, 250, 0.30)", "rgba(191, 219, 254, 0.95)")
            } else if (mode === "vectorscope") {
                const cx = width * 0.5
                const cy = height * 0.5
                const rad = Math.min(width, height) * 0.46
                ctx.save()
                ctx.strokeStyle = "rgba(255, 255, 255, 0.16)"
                ctx.lineWidth = 1
                ctx.beginPath()
                ctx.arc(cx, cy, rad, 0, Math.PI * 2)
                ctx.moveTo(cx - rad, cy)
                ctx.lineTo(cx + rad, cy)
                ctx.moveTo(cx, cy - rad)
                ctx.lineTo(cx, cy + rad)
                ctx.stroke()

                const pts = data.vectorscopePoints || []
                ctx.fillStyle = "rgba(56, 189, 248, 0.72)"
                for (let i = 0; i < pts.length; ++i) {
                    const px = cx + (pts[i].cb * 2.0) * rad
                    const py = cy - (pts[i].cr * 2.0) * rad
                    ctx.fillRect(px - 1, py - 1, 2.5, 2.5)
                }
                ctx.restore()
            }
        }
    }

    Row {
        id: statsRow
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 6
        spacing: Theme.spacingMd

        Text {
            text: root.scopeData && root.scopeData.valid
                  ? qsTr("Avg Y: %1%  Clip: %2%")
                        .arg(Math.round((root.scopeData.lumaAvg || 0) * 100))
                        .arg(Number(root.scopeData.clippingPct || 0).toFixed(1))
                  : qsTr("No frame")
            color: "#94a3b8"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeTiny
        }
    }
}
