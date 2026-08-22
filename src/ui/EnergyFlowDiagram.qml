// EnergyFlowDiagram.qml — 混动能量流可视化，输入均由父级显式绑定
//
// 能量模式:
//   0 = 纯电模式: Battery → Motor
//   1 = 混动模式: Battery + Engine → Motor
//   2 = 发动机驱动: Engine → Motor + Generator
//   3 = 充能模式: External → Battery
//   4 = OFF: 无模式箭头（当前 switch 的 default 灰态）
//   brakeActive = true 时叠加制动回收 Wheel → Battery，不替代上述模式箭头。
//
// DashboardMain 当前以 380×140 实例化（组件默认 380×420），因此内部按实例高度
// 比例压缩；父级还会将 rpm/speed/SOC 等 raw 值量化后传入，减少 Canvas 重评。
// 制动契约的历史差异：父级实际用 raw brake > 30 计算 brakeActive；本组件公开的
// brakeThreshold=0.3 未在内部使用，也不会自行读取 brake。不要将两者视为同一量纲。
// SOC、转速和功率保持 %/rpm/kW；温度与车速输入保留 canonical °C/km/h，
// 仅用户可见文本按 units 换算。本组件没有超时占位，上游缺值回落到 0。
// 性能：共享 160ms Timer 仅在组件可见且部件运动时运行；不可见时动画/Timer
// 停机。状态 Canvas 仅在模式、活跃阈值、量化 SOC 或尺寸变化时 requestPaint。

import QtQuick 2.15

Item {
    id: root
    width: 380
    height: 420
    property int units: 0

    UnitSystem {
        id: unitSystem
        units: root.units
    }

    // ─── 输入信号 ───
    property int energyMode: 0        // 0=EV, 1=Hybrid, 2=Engine, 3=Charging, 4=Off
    property real batSoc: 0           // 0-100 %
    property real batteryTemp: 0      // °C
    property real engineRpm: 0        // rpm
    property real motorRpm: 0         // rpm
    property real vehicleSpeed: 0      // km/h
    property real chargePower: 0       // kW

    // ─── 制动回收输入 ───
    property real brakeThreshold: 0.3  // 保留 API；当前实现不读取它
    property bool brakeActive: false   // 唯一生效输入；阈值/量纲由外部决定

    // ─── 颜色定义 ───
    readonly property color colorCharge: "#00FF88"   // 绿色: 充电/回收
    readonly property color colorDischarge: "#00AAFF" // 蓝色: 放电
    readonly property color colorHybrid: "#FFAA00"    // 橙色: 混合
    readonly property color colorEngine: "#FF6600"    // 橙色: 发动机
    readonly property color colorInactive: "#333333"  // 灰色: 未激活

    // ─── 内部状态 ───
    property int prevEnergyMode: -1
    property real arrowOpacity: 1.0
    property real engineScale: 1.0
    property real engineRotation: 0.0
    property bool engineRunning: false

    // ─── 箭头 Canvas ───
    Canvas {
        id: arrowCanvas
        anchors.fill: parent
        antialiasing: true

        // 300ms 淡入淡出
        NumberAnimation {
            id: arrowFadeAnim
            target: root
            property: "arrowOpacity"
            from: 0
            to: 1
            duration: 300
            easing.type: Easing.InOutQuad
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            var w = width
            var h = height
            var cx = w / 2

            // 节点位置
            var batY   = h * 0.18   // 电池包 顶部
            var engY   = h * 0.18   // 发动机 顶部右侧
            var motY   = h * 0.52   // 电机    中部
            var whlY   = h * 0.82   // 车轮    底部

            var batX   = w * 0.28   // 电池 左中
            var engX   = w * 0.72   // 发动机 右中
            var motX   = cx          // 电机 居中
            var whlLX  = w * 0.28   // 左轮
            var whlRX  = w * 0.72   // 右轮

            // 当前箭头颜色
            var arrowCol = getArrowColor()
            var alpha = root.arrowOpacity

            ctx.save()
            ctx.globalAlpha = alpha
            ctx.strokeStyle = arrowCol
            ctx.fillStyle   = arrowCol
            ctx.lineWidth   = 3.5
            ctx.lineCap     = "round"
            ctx.lineJoin    = "round"

            // 绘制主箭头路径
            drawArrows(ctx, cx, batX, batY, engX, engY, motX, motY, whlLX, whlRX, whlY)

            ctx.restore()
        }
    }

    // ─── 电池包 ───
    Item {
        id: batteryItem
        x: parent.width * 0.06
        y: parent.height * 0.02
        width: parent.width * 0.42
        height: parent.height * 0.30

        // 电池包外框
        Rectangle {
            id: batBody
            anchors.centerIn: parent
            width: parent.width * 0.9
            height: parent.height * 0.75
            color: "#1a1a1a"
            radius: 10
            border.color: getBatteryBorderColor()
            border.width: 2

            // 充电动画（缩放脉冲）
            SequentialAnimation on scale {
                running: root.visible && root.opacity > 0
                         && (root.energyMode === 3 || root.brakeActive) && root.batSoc < 100
                loops: Animation.Infinite
                NumberAnimation { from: 1.0; to: 1.06; duration: 600; easing.type: Easing.InOutQuad }
                NumberAnimation { from: 1.06; to: 1.0; duration: 600; easing.type: Easing.InOutQuad }
            }

            Column {
                anchors.centerIn: parent
                spacing: 4

                // 电池图形
                Canvas {
                    id: batIconCanvas
                    width: 60
                    height: 36
                    antialiasing: true
                    anchors.horizontalCenter: parent.horizontalCenter

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)

                        var isCharging = (root.energyMode === 3 || root.brakeActive)
                        var isDischarging = (root.energyMode === 0 || root.energyMode === 1)
                        var fillColor = isCharging ? colorCharge
                                    : isDischarging ? colorDischarge
                                    : colorInactive

                        // 电池体外框
                        var bx = 0, by = 4, bw = width * 0.82, bh = height - 8, br = 4
                        ctx.beginPath()
                        ctx.moveTo(bx + br, by)
                        ctx.lineTo(bx + bw - br, by)
                        ctx.arcTo(bx + bw, by, bx + bw, by + br, br)
                        ctx.lineTo(bx + bw, by + bh - br)
                        ctx.arcTo(bx + bw, by + bh, bx + bw - br, by + bh, br)
                        ctx.lineTo(bx + br, by + bh)
                        ctx.arcTo(bx, by + bh, bx, by + bh - br, br)
                        ctx.lineTo(bx, by + br)
                        ctx.arcTo(bx, by, bx + br, by, br)
                        ctx.closePath()
                        ctx.fillStyle = "#222222"
                        ctx.fill()
                        ctx.strokeStyle = fillColor
                        ctx.lineWidth = 2
                        ctx.stroke()

                        // 电池正极
                        var px = width * 0.82, py = height * 0.25, pw = width * 0.18, ph = height * 0.5, pr2 = 2
                        ctx.beginPath()
                        ctx.moveTo(px + pr2, py)
                        ctx.lineTo(px + pw - pr2, py)
                        ctx.arcTo(px + pw, py, px + pw, py + pr2, pr2)
                        ctx.lineTo(px + pw, py + ph - pr2)
                        ctx.arcTo(px + pw, py + ph, px + pw - pr2, py + ph, pr2)
                        ctx.lineTo(px + pr2, py + ph)
                        ctx.arcTo(px, py + ph, px, py + ph - pr2, pr2)
                        ctx.lineTo(px, py + pr2)
                        ctx.arcTo(px, py, px + pr2, py, pr2)
                        ctx.closePath()
                        ctx.fillStyle = "#444444"
                        ctx.fill()

                        // 电量填充
                        var fillH = (height - 12) * (root.batSoc / 100.0)
                        var fillY = (height - 8) - fillH
                        var fx = 3, fy = fillY, fw = (width * 0.82) - 6, fh = fillH, fr = 2
                        ctx.beginPath()
                        ctx.moveTo(fx + fr, fy)
                        ctx.lineTo(fx + fw - fr, fy)
                        ctx.arcTo(fx + fw, fy, fx + fw, fy + fr, fr)
                        ctx.lineTo(fx + fw, fy + fh - fr)
                        ctx.arcTo(fx + fw, fy + fh, fx + fw - fr, fy + fh, fr)
                        ctx.lineTo(fx + fr, fy + fh)
                        ctx.arcTo(fx, fy + fh, fx, fy + fh - fr, fr)
                        ctx.lineTo(fx, fy + fr)
                        ctx.arcTo(fx, fy, fx + fr, fy, fr)
                        ctx.closePath()
                        // 电量填充（fillH=0 时跳过渐变，避免 createLinearGradient 零高度参数非法）
                        var grad = null
                        if (fillH > 0.5) {
                            grad = ctx.createLinearGradient(0, fillY, 0, fillY + fillH)
                            grad.addColorStop(0, colorCss(fillColor))
                            grad.addColorStop(1, colorWithAlpha(fillColor, "88"))
                        }
                        ctx.fillStyle = grad !== null ? grad : colorCss(fillColor)
                        ctx.fill()

                        // 充放电指示闪电
                        if (isCharging) {
                            ctx.fillStyle = "#FFFFFF"
                            ctx.font = "bold 14px sans-serif"
                            ctx.textAlign = "center"
                            ctx.textBaseline = "middle"
                            ctx.fillText("⚡", width * 0.45, height * 0.5)
                        } else if (isDischarging && root.batSoc > 5) {
                            ctx.fillStyle = "#FFFFFF"
                            ctx.font = "bold 10px sans-serif"
                            ctx.textAlign = "center"
                            ctx.textBaseline = "middle"
                            ctx.fillText("↯", width * 0.45, height * 0.5)
                        }
                    }
                }

                // SOC 数值
                Text {
                    id: socText
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.batSoc.toFixed(0) + "%"
                    color: root.batSoc < 20 ? "#FF4400" : root.batSoc < 50 ? "#FFAA00" : "#00FF88"
                    font.pixelSize: 22
                    font.weight: Font.Bold
                    font.family: "Roboto Mono, monospace"
                }

                // 温度
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: unitSystem.formatTemperature(root.batteryTemp, 0)
                          + unitSystem.temperatureLabel
                    color: root.batteryTemp > 50 ? "#FF4400" : root.batteryTemp > 40 ? "#FFAA00" : "#888888"
                    font.pixelSize: 13
                    font.family: "Roboto Mono, monospace"
                }

                // 功率
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: (root.chargePower >= 0 ? "+" : "") + root.chargePower.toFixed(1) + " kW"
                    color: root.chargePower >= 0 ? colorCharge : colorDischarge
                    font.pixelSize: 12
                    font.family: "Roboto Mono, monospace"
                }
            }
        }

        // 标签
        Text {
            anchors.horizontalCenter: batBody.horizontalCenter
            anchors.top: batBody.bottom
            anchors.topMargin: 4
            text: "BATTERY"
            color: "#666666"
            font.pixelSize: 11
            font.family: "Roboto Mono, monospace"
        }
    }

    // ─── 发动机 ───
    Item {
        id: engineItem
        x: parent.width * 0.52
        y: parent.height * 0.02
        width: parent.width * 0.42
        height: parent.height * 0.30

        Rectangle {
            id: engBody
            anchors.centerIn: parent
            width: parent.width * 0.9
            height: parent.height * 0.75
            color: "#1a1a1a"
            radius: 10
            border.color: engineRunning ? colorEngine : "#333333"
            border.width: engineRunning ? 2 : 1

            // 发动机启动/停机动画（缩放+旋转）
            SequentialAnimation {
                id: engineAnim
                running: false
                loops: 1

                // 启动：从缩放0.8 + 旋转0° → 正常
                NumberAnimation {
                    target: engineItem
                    property: "scale"
                    from: 0.82
                    to: 1.0
                    duration: 400
                    easing.type: Easing.OutBack
                }
                NumberAnimation {
                    target: engineItem
                    property: "rotation"
                    from: -15
                    to: 0
                    duration: 400
                    easing.type: Easing.OutBack
                }
            }

            Column {
                anchors.centerIn: parent
                spacing: 4

                // 发动机图标 (Canvas 绘制)
                Canvas {
                    id: engIconCanvas
                    width: 56
                    height: 40
                    antialiasing: true
                    anchors.horizontalCenter: parent.horizontalCenter

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)

                        var running = root.engineRunning
                        var col = running ? colorEngine : colorInactive

                        // 发动机气缸
                        var ex = width * 0.1, ey = height * 0.2, ew = width * 0.35, eh = height * 0.6, er = 3
                        ctx.beginPath()
                        ctx.moveTo(ex + er, ey); ctx.lineTo(ex + ew - er, ey)
                        ctx.arcTo(ex + ew, ey, ex + ew, ey + er, er)
                        ctx.lineTo(ex + ew, ey + eh - er); ctx.arcTo(ex + ew, ey + eh, ex + ew - er, ey + eh, er)
                        ctx.lineTo(ex + er, ey + eh); ctx.arcTo(ex, ey + eh, ex, ey + eh - er, er)
                        ctx.lineTo(ex, ey + er); ctx.arcTo(ex, ey, ex + er, ey, er)
                        ctx.closePath()
                        ctx.fillStyle = "#222222"; ctx.fill()
                        ctx.strokeStyle = col; ctx.lineWidth = 2; ctx.stroke()

                        // 曲轴
                        var cx2 = width * 0.48, cy2 = height * 0.35, cw = width * 0.42, ch = height * 0.3, cr = 2
                        ctx.beginPath()
                        ctx.moveTo(cx2 + cr, cy2); ctx.lineTo(cx2 + cw - cr, cy2)
                        ctx.arcTo(cx2 + cw, cy2, cx2 + cw, cy2 + cr, cr)
                        ctx.lineTo(cx2 + cw, cy2 + ch - cr); ctx.arcTo(cx2 + cw, cy2 + ch, cx2 + cw - cr, cy2 + ch, cr)
                        ctx.lineTo(cx2 + cr, cy2 + ch); ctx.arcTo(cx2, cy2 + ch, cx2, cy2 + ch - cr, cr)
                        ctx.lineTo(cx2, cy2 + cr); ctx.arcTo(cx2, cy2, cx2 + cr, cy2, cr)
                        ctx.closePath()
                        ctx.fillStyle = "#222222"; ctx.fill()
                        ctx.strokeStyle = col; ctx.lineWidth = 1.5; ctx.stroke()

                        // 活塞
                        var pistonY = running
                            ? height * 0.25 + Math.sin(Date.now() / 100) * height * 0.2
                            : height * 0.35
                        var pistX = width * 0.17, pistW = width * 0.22, pistH = height * 0.18, pistR = 2
                        ctx.beginPath()
                        ctx.moveTo(pistX + pistR, pistonY); ctx.lineTo(pistX + pistW - pistR, pistonY)
                        ctx.arcTo(pistX + pistW, pistonY, pistX + pistW, pistonY + pistR, pistR)
                        ctx.lineTo(pistX + pistW, pistonY + pistH - pistR); ctx.arcTo(pistX + pistW, pistonY + pistH, pistX + pistW - pistR, pistonY + pistH, pistR)
                        ctx.lineTo(pistX + pistR, pistonY + pistH); ctx.arcTo(pistX, pistonY + pistH, pistX, pistonY + pistH - pistR, pistR)
                        ctx.lineTo(pistX, pistonY + pistR); ctx.arcTo(pistX, pistonY, pistX + pistR, pistonY, pistR)
                        ctx.closePath()
                        ctx.fillStyle = colorWithAlpha(col, "88"); ctx.fill()

                        // ⚙ 齿轮
                        ctx.fillStyle = col
                        ctx.font = "bold 12px sans-serif"
                        ctx.textAlign = "center"
                        ctx.textBaseline = "middle"
                        ctx.fillText("⚙", width * 0.7, height * 0.5)
                    }

                }

                // RPM
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: (root.engineRpm / 1000).toFixed(1) + "k"
                    color: engineRunning ? "#FFAA00" : "#444444"
                    font.pixelSize: 20
                    font.weight: Font.Bold
                    font.family: "Roboto Mono, monospace"
                }

                // 状态
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: engineRunning ? "ON" : "OFF"
                    color: engineRunning ? colorEngine : "#444444"
                    font.pixelSize: 13
                    font.family: "Roboto Mono, monospace"
                }
            }
        }

        // 标签
        Text {
            anchors.horizontalCenter: engBody.horizontalCenter
            anchors.top: engBody.bottom
            anchors.topMargin: 4
            text: "ENGINE"
            color: "#666666"
            font.pixelSize: 11
            font.family: "Roboto Mono, monospace"
        }
    }

    // ─── 电机 ───
    Item {
        id: motorItem
        x: parent.width * 0.28
        y: parent.height * 0.37
        width: parent.width * 0.44
        height: parent.height * 0.28

        Rectangle {
            id: motBody
            anchors.centerIn: parent
            width: parent.width * 0.9
            height: parent.height * 0.88
            color: "#1a1a1a"
            radius: 10
            border.color: root.motorRpm > 100 ? colorDischarge : "#333333"
            border.width: root.motorRpm > 100 ? 2 : 1

            Column {
                anchors.centerIn: parent
                spacing: 2

                // 电机图形
                Canvas {
                    id: motIconCanvas
                    width: 50
                    height: 50
                    antialiasing: true
                    anchors.horizontalCenter: parent.horizontalCenter

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)

                        var active = root.motorRpm > 100
                        var col = active ? colorDischarge : colorInactive
                        var cx = width / 2
                        var cy = height / 2
                        var r = Math.min(cx, cy) - 4

                        // 转子
                        ctx.beginPath()
                        ctx.arc(cx, cy, r * 0.55, 0, 2 * Math.PI)
                        ctx.fillStyle = "#1a1a1a"
                        ctx.fill()
                        ctx.strokeStyle = col
                        ctx.lineWidth = 2
                        ctx.stroke()

                        // 线圈（3组）
                        for (var i = 0; i < 3; i++) {
                            var angle = (i * 120 + (active ? Date.now() / 20 : 0)) * Math.PI / 180
                            ctx.beginPath()
                            ctx.arc(cx, cy, r * 0.75, angle, angle + Math.PI * 0.4)
                            ctx.strokeStyle = col
                            ctx.lineWidth = 3
                            ctx.stroke()
                        }

                        // 中心轴
                        ctx.beginPath()
                        ctx.arc(cx, cy, r * 0.18, 0, 2 * Math.PI)
                        ctx.fillStyle = col
                        ctx.fill()
                    }

                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: (root.motorRpm / 1000).toFixed(1) + "k"
                    color: root.motorRpm > 100 ? colorDischarge : "#444444"
                    font.pixelSize: 18
                    font.weight: Font.Bold
                    font.family: "Roboto Mono, monospace"
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "RPM"
                    color: "#444444"
                    font.pixelSize: 10
                    font.family: "Roboto Mono, monospace"
                }
            }
        }

        // 标签
        Text {
            anchors.horizontalCenter: motBody.horizontalCenter
            anchors.top: motBody.bottom
            anchors.topMargin: 2
            text: "MOTOR"
            color: "#666666"
            font.pixelSize: 10
            font.family: "Roboto Mono, monospace"
        }
    }

    // ─── 车轮 ───
    Item {
        id: wheelItem
        x: parent.width * 0.15
        y: parent.height * 0.67
        width: parent.width * 0.70
        height: parent.height * 0.30

        Row {
            anchors.centerIn: parent
            spacing: 20

            // 左轮
            Item {
                width: 60
                height: 70

                Rectangle {
                    id: whlLBody
                    anchors.centerIn: parent
                    width: 56
                    height: 56
                    color: "#1a1a1a"
                    radius: width / 2
                    border.color: root.vehicleSpeed > 1 ? colorDischarge : "#333333"
                    border.width: root.vehicleSpeed > 1 ? 2 : 1

                    Canvas {
                        id: whlLCanvas
                        anchors.fill: parent
                        antialiasing: true

                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)

                            var active = root.vehicleSpeed > 1
                            var col = active ? colorDischarge : colorInactive
                            var cx = width / 2
                            var cy = height / 2
                            var r = Math.min(cx, cy) - 4

                            // 轮毂
                            ctx.beginPath()
                            ctx.arc(cx, cy, r, 0, 2 * Math.PI)
                            ctx.fillStyle = "#111111"
                            ctx.fill()
                            ctx.strokeStyle = col
                            ctx.lineWidth = 2
                            ctx.stroke()

                            // 轮辐（旋转）
                            var spokes = 5
                            var rotAngle = active ? Date.now() / (root.vehicleSpeed * 2 + 50) : 0
                            for (var i = 0; i < spokes; i++) {
                                var a = (i * 360 / spokes + rotAngle) * Math.PI / 180
                                ctx.beginPath()
                                ctx.moveTo(cx + Math.cos(a) * r * 0.2, cy + Math.sin(a) * r * 0.2)
                                ctx.lineTo(cx + Math.cos(a) * r * 0.85, cy + Math.sin(a) * r * 0.85)
                                ctx.strokeStyle = colorWithAlpha(col, "aa")
                                ctx.lineWidth = 3
                                ctx.stroke()
                            }

                            // 中心
                            ctx.beginPath()
                            ctx.arc(cx, cy, r * 0.2, 0, 2 * Math.PI)
                            ctx.fillStyle = col
                            ctx.fill()
                        }

                    }
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: whlLBody.bottom
                    anchors.topMargin: 2
                    text: unitSystem.formatSpeed(root.vehicleSpeed, 0)
                    color: root.vehicleSpeed > 1 ? "#00AAFF" : "#444444"
                    font.pixelSize: 14
                    font.weight: Font.Bold
                    font.family: "Roboto Mono, monospace"
                }
            }

            // 右轮
            Item {
                width: 60
                height: 70

                Rectangle {
                    id: whlRBody
                    anchors.centerIn: parent
                    width: 56
                    height: 56
                    color: "#1a1a1a"
                    radius: width / 2
                    border.color: root.vehicleSpeed > 1 ? colorDischarge : "#333333"
                    border.width: root.vehicleSpeed > 1 ? 2 : 1

                    Canvas {
                        id: whlRCanvas
                        anchors.fill: parent
                        antialiasing: true

                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)

                            var active = root.vehicleSpeed > 1
                            var col = active ? colorDischarge : colorInactive
                            var cx = width / 2
                            var cy = height / 2
                            var r = Math.min(cx, cy) - 4

                            ctx.beginPath()
                            ctx.arc(cx, cy, r, 0, 2 * Math.PI)
                            ctx.fillStyle = "#111111"
                            ctx.fill()
                            ctx.strokeStyle = col
                            ctx.lineWidth = 2
                            ctx.stroke()

                            var spokes = 5
                            var rotAngle = active ? Date.now() / (root.vehicleSpeed * 2 + 50) : 0
                            for (var i = 0; i < spokes; i++) {
                                var a = (i * 360 / spokes + rotAngle) * Math.PI / 180
                                ctx.beginPath()
                                ctx.moveTo(cx + Math.cos(a) * r * 0.2, cy + Math.sin(a) * r * 0.2)
                                ctx.lineTo(cx + Math.cos(a) * r * 0.85, cy + Math.sin(a) * r * 0.85)
                                ctx.strokeStyle = colorWithAlpha(col, "aa")
                                ctx.lineWidth = 3
                                ctx.stroke()
                            }

                            ctx.beginPath()
                            ctx.arc(cx, cy, r * 0.2, 0, 2 * Math.PI)
                            ctx.fillStyle = col
                            ctx.fill()
                        }

                    }
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: whlRBody.bottom
                    anchors.topMargin: 2
                    text: unitSystem.speedLabel
                    color: "#444444"
                    font.pixelSize: 10
                    font.family: "Roboto Mono, monospace"
                }
            }
        }
    }

    // ─── 模式标签 ───
    Rectangle {
        id: modeBadge
        anchors.top: parent.top
        anchors.right: parent.right
        width: 80
        height: 28
        color: "#AA000000"
        radius: 6
        border.color: getModeColor()
        border.width: 1

        Text {
            anchors.centerIn: parent
            text: getModeText()
            color: getModeColor()
            font.pixelSize: 11
            font.weight: Font.Bold
            font.family: "Roboto Mono, monospace"
        }
    }

    // ─── 辅助函数 ───

    // Canvas 需要 CSS 色字符串；color 对象直接 + "88" 在部分 Qt 上会拼出非法值
    function colorCss(c) {
        var r = Math.round(c.r * 255)
        var g = Math.round(c.g * 255)
        var b = Math.round(c.b * 255)
        return "rgb(" + r + "," + g + "," + b + ")"
    }
    function colorWithAlpha(c, aHex) {
        var r = Math.round(c.r * 255)
        var g = Math.round(c.g * 255)
        var b = Math.round(c.b * 255)
        var a = parseInt(aHex, 16) / 255.0
        return "rgba(" + r + "," + g + "," + b + "," + a + ")"
    }

    function getArrowColor() {
        switch (root.energyMode) {
            case 0: return colorDischarge   // 纯电放电
            case 1: return colorHybrid      // 混合
            case 2: return colorEngine      // 发动机驱动
            case 3: return colorCharge      // 外部充电
            default: return colorInactive
        }
    }

    function getModeColor() {
        if (root.brakeActive) return colorCharge
        switch (root.energyMode) {
            case 0: return colorDischarge
            case 1: return colorHybrid
            case 2: return colorEngine
            case 3: return colorCharge
            default: return colorInactive
        }
    }

    function getModeText() {
        if (root.brakeActive) return "REGEN"
        switch (root.energyMode) {
            case 0: return "EV"
            case 1: return "HYBRID"
            case 2: return "ENGINE"
            case 3: return "CHARGE"
            default: return "---"
        }
    }

    function getBatteryBorderColor() {
        if (root.energyMode === 3 || root.brakeActive) return colorCharge
        if (root.energyMode === 0 || root.energyMode === 1) return colorDischarge
        return "#333333"
    }

    function drawArrows(ctx, cx, batX, batY, engX, engY, motX, motY, whlLX, whlRX, whlY) {
        // 能量流方向根据模式
        // 流向绘制：起点 → 终点，绘制箭头

        if (root.brakeActive) {
            // 制动回收: Wheel → Battery
            drawArrow(ctx, whlLX, whlY, batX, batY, colorCharge, 3, true)
            drawArrow(ctx, whlRX, whlY, batX, batY, colorCharge, 3, true)
        }

        switch (root.energyMode) {
            case 0: // 纯电: Battery → Motor
                drawArrow(ctx, batX, batY + 30, motX, motY - 30, colorDischarge, 4, true)
                // Motor → Wheels
                drawArrow(ctx, whlLX - 40, motY + 10, whlLX, whlY - 30, colorDischarge, 3, false)
                drawArrow(ctx, whlRX + 40, motY + 10, whlRX, whlY - 30, colorDischarge, 3, false)
                break

            case 1: // 混动: Battery + Engine → Motor
                drawArrow(ctx, batX, batY + 30, motX, motY - 30, colorDischarge, 3, true)
                drawArrow(ctx, engX, engY + 30, motX - 20, motY - 20, colorHybrid, 3, true)
                // Motor → Wheels
                drawArrow(ctx, whlLX - 40, motY + 10, whlLX, whlY - 30, colorDischarge, 3, false)
                drawArrow(ctx, whlRX + 40, motY + 10, whlRX, whlY - 30, colorDischarge, 3, false)
                break

            case 2: // 发动机驱动: Engine → Motor + Generator
                // Engine → Motor
                drawArrow(ctx, engX, engY + 30, motX + 20, motY - 20, colorEngine, 4, true)
                // Engine → Generator (右箭头)
                drawArrow(ctx, engX + 20, engY + 50, engX + 60, engY + 50, colorEngine, 2, false)
                // Motor → Wheels
                drawArrow(ctx, whlLX - 40, motY + 10, whlLX, whlY - 30, colorDischarge, 3, false)
                drawArrow(ctx, whlRX + 40, motY + 10, whlRX, whlY - 30, colorDischarge, 3, false)
                break

            case 3: // 充能: External → Battery
                // 外部电源 → 电池
                drawArrow(ctx, cx - 80, batY - 20, batX, batY + 20, colorCharge, 4, true)
                break
        }
    }

    function drawArrow(ctx, x1, y1, x2, y2, color, lineWidth, glow) {
        var angle = Math.atan2(y2 - y1, x2 - x1)
        var len   = Math.sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1))
        var headLen = Math.min(14, len * 0.35)
        var tailW   = Math.max(2, lineWidth * 0.6)

        ctx.save()
        ctx.strokeStyle = color
        ctx.fillStyle   = color
        ctx.lineWidth   = lineWidth

        if (glow) {
            ctx.shadowColor = color
            ctx.shadowBlur  = 8
        }

        // 线身
        ctx.beginPath()
        ctx.moveTo(x1, y1)
        ctx.lineTo(x2 - Math.cos(angle) * headLen, y2 - Math.sin(angle) * headLen)
        ctx.stroke()

        // 箭头
        ctx.beginPath()
        ctx.moveTo(x2, y2)
        ctx.lineTo(
            x2 - headLen * Math.cos(angle - Math.PI / 7),
            y2 - headLen * Math.sin(angle - Math.PI / 7)
        )
        ctx.lineTo(
            x2 - headLen * Math.cos(angle + Math.PI / 7),
            y2 - headLen * Math.sin(angle + Math.PI / 7)
        )
        ctx.closePath()
        ctx.fill()

        ctx.restore()
    }

    // ─── 共享动画 Timer（合并原 eng/mot/whl×2 四个 Timer，避免 Timer 风暴）───
    // ~6.25fps 足够活塞/转子/轮辐观感；隐藏或无运动部件时完全停机
    Timer {
        id: animTimer
        interval: 160
        running: root.visible && root.opacity > 0
                 && (root.engineRunning || root.motorRpm > 100 || root.vehicleSpeed > 1)
        repeat: true
        onTriggered: {
            if (root.engineRunning)
                engIconCanvas.requestPaint()
            if (root.motorRpm > 100)
                motIconCanvas.requestPaint()
            if (root.vehicleSpeed > 1) {
                whlLCanvas.requestPaint()
                whlRCanvas.requestPaint()
            }
        }
    }

    // ─── 信号更新处理（仅在视觉状态真正变化时 requestPaint）───
    property int _lastBatSocPainted: -999
    property bool _motWasActive: false
    property bool _whlWasActive: false
    property real _lastArrowAlphaPainted: -1

    onEngineRpmChanged: {
        var wasRunning = engineRunning
        engineRunning = (engineRpm > 50)
        if (engineRunning && !wasRunning) {
            engineAnim.start()
            engIconCanvas.requestPaint()
        } else if (!engineRunning && wasRunning) {
            engIconCanvas.requestPaint()
        }
    }

    onBatSocChanged: {
        var s = Math.round(batSoc)
        if (s !== _lastBatSocPainted) {
            _lastBatSocPainted = s
            batIconCanvas.requestPaint()
        }
    }
    // batteryTemp / chargePower 仅驱动 Text 绑定，无需 Canvas 重绘

    onMotorRpmChanged: {
        var active = motorRpm > 100
        if (active !== _motWasActive) {
            _motWasActive = active
            motIconCanvas.requestPaint()
        }
    }
    onVehicleSpeedChanged: {
        var active = vehicleSpeed > 1
        if (active !== _whlWasActive) {
            _whlWasActive = active
            whlLCanvas.requestPaint()
            whlRCanvas.requestPaint()
        }
    }
    onBrakeActiveChanged: { arrowCanvas.requestPaint(); batIconCanvas.requestPaint() }

    // 淡入动画：量化 opacity，避免每帧都 requestPaint
    onArrowOpacityChanged: {
        if (arrowOpacity !== 0 && arrowOpacity !== 1
            && Math.abs(arrowOpacity - _lastArrowAlphaPainted) < 0.05)
            return
        _lastArrowAlphaPainted = arrowOpacity
        arrowCanvas.requestPaint()
    }

    onEnergyModeChanged: {
        if (prevEnergyMode !== energyMode) {
            arrowOpacity = 0
            arrowFadeAnim.from = 0
            arrowFadeAnim.to = 1
            arrowFadeAnim.start()
            prevEnergyMode = energyMode
            batIconCanvas.requestPaint()
            arrowCanvas.requestPaint()
        }
    }

    onWidthChanged:  arrowCanvas.requestPaint()
    onHeightChanged: arrowCanvas.requestPaint()

    Component.onCompleted: {
        _motWasActive = motorRpm > 100
        _whlWasActive = vehicleSpeed > 1
        _lastBatSocPainted = Math.round(batSoc)
        batIconCanvas.requestPaint()
        engIconCanvas.requestPaint()
        motIconCanvas.requestPaint()
        whlLCanvas.requestPaint()
        whlRCanvas.requestPaint()
        arrowCanvas.requestPaint()
    }
}
