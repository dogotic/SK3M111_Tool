import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    visible: true
    width: 520
    height: 400
    minimumWidth: 420
    minimumHeight: 340
    title: "SK3M111 Viewer"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 6

        // ── Port selection ───────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Label { text: "Port:" }

            ComboBox {
                id: portCombo
                Layout.fillWidth: true
                model: serialHandler.availablePorts
                enabled: !serialHandler.connected
            }

            Button {
                text: "Refresh"
                enabled: !serialHandler.connected
                onClicked: serialHandler.refreshPorts()
            }

            Button {
                text: serialHandler.connected ? "Disconnect" : "Connect"
                enabled: serialHandler.connected || portCombo.count > 0
                highlighted: !serialHandler.connected
                onClicked: {
                    if (serialHandler.connected)
                        serialHandler.disconnectFromPort()
                    else
                        serialHandler.connectToPort(portCombo.currentText)
                }
            }
        }

        // ── Sensor configuration ─────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            enabled: serialHandler.connected

            Label { text: "Max gate:" }

            SpinBox {
                id: maxGateBox
                from: 1; to: 15; value: 8
            }

            Label {
                text: "(" + (maxGateBox.value * 0.70).toFixed(2) + " m)"
                color: "#666666"
            }

            Item { Layout.fillWidth: true }

            Label { text: "Absence:" }

            SpinBox {
                id: absenceBox
                from: 0; to: 65535; value: 10
            }

            Label { text: "s" }

            Button {
                text: "Configure"
                onClicked: serialHandler.sendConfig(maxGateBox.value, absenceBox.value)
            }
        }

        // ── Mode + firmware ──────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            enabled: serialHandler.connected

            Label { text: "Mode:" }

            ComboBox {
                id: modeCombo
                model: ["Normal", "Report", "Debug"]
            }

            Button {
                text: "Set Mode"
                onClicked: serialHandler.setMode(modeCombo.currentIndex)
            }

            Item { Layout.fillWidth: true }

            Button {
                text: "Read Firmware"
                onClicked: serialHandler.readFirmwareVersion()
            }
        }

        // ── Radar fan display ────────────────────────────────────────────
        Canvas {
            id: radarCanvas
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Bind to sensor data — each change triggers a repaint
            readonly property int    maxG:    maxGateBox.value
            readonly property double distM:   serialHandler.distanceM
            readonly property bool   active:  serialHandler.isActive
            readonly property bool   online:  serialHandler.connected
            readonly property string rawData: serialHandler.receivedData

            onMaxGChanged:    requestPaint()
            onDistMChanged:   requestPaint()
            onActiveChanged:  requestPaint()
            onOnlineChanged:  requestPaint()
            onWidthChanged:   requestPaint()
            onHeightChanged:  requestPaint()

            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                if (width < 10 || height < 10) return

                // ── Geometry ──────────────────────────────────────────────
                var cx      = width / 2
                var sy      = 32                              // sensor Y (pixels from top)
                var availH  = height - sy - 24               // usable height below sensor
                var scale   = availH / (maxG * 0.70)         // px per metre
                var startA  = Math.PI / 6                    // 30° — right fan edge
                var endA    = 5 * Math.PI / 6               // 150° — left fan edge

                // ── Background ────────────────────────────────────────────
                ctx.fillStyle = "#12121f"
                ctx.fillRect(0, 0, width, height)

                // ── Gate sectors (outer → inner so inner paints on top) ───
                for (var g = maxG; g >= 1; g--) {
                    var r       = g * 0.70 * scale
                    var gateNear = (g - 1) * 0.70   // near edge of this gate in metres
                    var gateFar  = g * 0.70          // far edge

                    var hit = active && distM >= 0 &&
                              distM > gateNear && distM <= gateFar

                    if (hit) {
                        // Detected gate — bright green
                        ctx.fillStyle = "rgba(76,175,80,0.90)"
                    } else if (active && distM >= 0 && g * 0.70 <= distM) {
                        // Closer gates — dim green trail
                        ctx.fillStyle = "rgba(46,125,50,0.30)"
                    } else if (!active && online) {
                        // OFF — faint red wash, brighter on outer gate
                        ctx.fillStyle = g === maxG
                            ? "rgba(198,40,40,0.45)"
                            : "rgba(120,20,20,0.18)"
                    } else {
                        // Not connected / waiting
                        ctx.fillStyle = "rgba(40,40,80,0.55)"
                    }

                    ctx.beginPath()
                    ctx.moveTo(cx, sy)
                    ctx.arc(cx, sy, r, startA, endA)
                    ctx.closePath()
                    ctx.fill()
                }

                // ── Gate arc outlines + distance labels ───────────────────
                for (var g2 = 1; g2 <= maxG; g2++) {
                    var r2 = g2 * 0.70 * scale

                    ctx.strokeStyle = "rgba(100,120,200,0.40)"
                    ctx.lineWidth   = 1
                    ctx.setLineDash([4, 4])
                    ctx.beginPath()
                    ctx.arc(cx, sy, r2, startA, endA)
                    ctx.stroke()
                    ctx.setLineDash([])

                    // Label just right of the right-edge line
                    var lx = cx + r2 * Math.cos(startA) + 4
                    var ly = sy + r2 * Math.sin(startA) + 3
                    ctx.fillStyle  = "rgba(160,180,230,0.75)"
                    ctx.font       = "10px sans-serif"
                    ctx.textAlign  = "left"
                    ctx.fillText((g2 * 0.70).toFixed(1) + " m", lx, ly)
                }

                // ── Outer boundary lines + arc ────────────────────────────
                var outerR  = maxG * 0.70 * scale
                var edgeCol = active ? "#4caf50" : (online ? "#ef5350" : "#5c6bc0")
                ctx.strokeStyle = edgeCol
                ctx.lineWidth   = 2
                ctx.setLineDash([])

                ctx.beginPath()
                ctx.moveTo(cx, sy)
                ctx.lineTo(cx + outerR * Math.cos(startA), sy + outerR * Math.sin(startA))
                ctx.stroke()

                ctx.beginPath()
                ctx.moveTo(cx, sy)
                ctx.lineTo(cx + outerR * Math.cos(endA), sy + outerR * Math.sin(endA))
                ctx.stroke()

                ctx.beginPath()
                ctx.arc(cx, sy, outerR, startA, endA)
                ctx.stroke()

                // Exact-distance arc (report mode has sub-gate precision)
                if (active && distM >= 0 && distM < maxG * 0.70) {
                    var detR = distM * scale
                    ctx.strokeStyle = "#ffffff"
                    ctx.lineWidth   = 2
                    ctx.beginPath()
                    ctx.arc(cx, sy, detR, startA, endA)
                    ctx.stroke()
                }

                // ── Detected object marker ───────────────────────────────
                if (active && distM >= 0 && distM <= maxG * 0.70) {
                    var dotX = cx
                    var dotY = sy + distM * scale

                    // Radial glow
                    var grd = ctx.createRadialGradient(dotX, dotY, 0, dotX, dotY, 26)
                    grd.addColorStop(0.00, "rgba(255,255,255,0.90)")
                    grd.addColorStop(0.20, "rgba(160,255,160,0.60)")
                    grd.addColorStop(1.00, "rgba(76,175,80,0.00)")
                    ctx.fillStyle = grd
                    ctx.beginPath()
                    ctx.arc(dotX, dotY, 26, 0, Math.PI * 2)
                    ctx.fill()

                    // Outer targeting ring
                    ctx.strokeStyle = "rgba(255,255,255,0.55)"
                    ctx.lineWidth   = 1.5
                    ctx.setLineDash([4, 3])
                    ctx.beginPath()
                    ctx.arc(dotX, dotY, 13, 0, Math.PI * 2)
                    ctx.stroke()
                    ctx.setLineDash([])

                    // Cross-hair ticks (horizontal, aligned to fan arc)
                    ctx.strokeStyle = "rgba(255,255,255,0.50)"
                    ctx.lineWidth   = 1
                    ctx.beginPath()
                    ctx.moveTo(dotX - 22, dotY)
                    ctx.lineTo(dotX - 14, dotY)
                    ctx.moveTo(dotX + 14, dotY)
                    ctx.lineTo(dotX + 22, dotY)
                    ctx.moveTo(dotX, dotY - 22)
                    ctx.lineTo(dotX, dotY - 14)
                    ctx.moveTo(dotX, dotY + 14)
                    ctx.lineTo(dotX, dotY + 22)
                    ctx.stroke()

                    // Solid centre dot
                    ctx.fillStyle = "#ffffff"
                    ctx.beginPath()
                    ctx.arc(dotX, dotY, 5, 0, Math.PI * 2)
                    ctx.fill()
                }

                // ── Ceiling (hatch lines above sensor) ───────────────────
                ctx.strokeStyle = "rgba(180,180,210,0.55)"
                ctx.lineWidth   = 1
                ctx.beginPath()
                ctx.moveTo(0, sy - 8)
                ctx.lineTo(width, sy - 8)
                ctx.stroke()

                ctx.strokeStyle = "rgba(140,140,170,0.45)"
                for (var hx = 0; hx < width + 16; hx += 12) {
                    ctx.beginPath()
                    ctx.moveTo(hx, sy - 8)
                    ctx.lineTo(hx - 10, sy - 18)
                    ctx.stroke()
                }

                // ── Sensor square ─────────────────────────────────────────
                ctx.fillStyle = "#1565c0"
                ctx.fillRect(cx - 7, sy - 8, 14, 14)
                ctx.strokeStyle = "#90caf9"
                ctx.lineWidth   = 1
                ctx.strokeRect(cx - 7, sy - 8, 14, 14)

                ctx.fillStyle  = "rgba(144,202,249,0.80)"
                ctx.font       = "9px sans-serif"
                ctx.textAlign  = "center"
                ctx.fillText("mmWave", cx, sy - 12)

                // ── Status / distance text (centred in fan) ───────────────
                var textY = sy + outerR * 0.50

                var label = ""
                if (!online)       label = "Disconnected"
                else if (rawData === "") label = "Waiting…"
                else if (!active)  label = "OFF"
                else if (distM >= 0) label = distM.toFixed(2) + " m"
                else               label = rawData

                ctx.font      = "bold 32px sans-serif"
                ctx.textAlign = "center"

                // Drop shadow
                ctx.fillStyle = "rgba(0,0,0,0.65)"
                ctx.fillText(label, cx + 1, textY + 1)

                ctx.fillStyle = active  ? "#ffffff"
                              : online  ? "#ef9a9a"
                              :           "#78909c"
                ctx.fillText(label, cx, textY)
            }
        }

        // ── Status bar ───────────────────────────────────────────────────
        Label {
            Layout.fillWidth: true
            text: serialHandler.statusMessage
            font.pixelSize: 11
            color: serialHandler.connected ? "#2e7d32" : "#888888"
            elide: Text.ElideRight
        }
    }
}
