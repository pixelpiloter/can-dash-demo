// GaugeCanvas.qml — DashboardMain 当前使用的通用圆形表盘（非 SpeedGauge 基类）
//
// 组件契约：调用方提供 value/minValue/maxValue、unit、刻度数与起止角；
// DashboardMain 的速度/RPM 实例均采用 135°→405°，value 会钳制到该扫掠范围。
// valueLabel 非空时只覆盖中央数字（例如 logic 超时 "---"），不改变 value、
// 指针位置或单位；空字符串恢复 value.toFixed(0)。调用方负责占位语义。
// 三层分工：
//   bgCanvas 绘制表盘面、刻度、可选红区与底色外圈；只在创建/尺寸变化时重画。
//   needleCanvas 绘制朝右的静态针形；连续 value 仅改变 needlePivot.rotation，
//   只有针画布尺寸或指针颜色带变化才 requestPaint。
//   outerRingCanvas 绘制独立彩色外环并覆盖 bgCanvas 的 #0d0d0d 底色外圈。
// Text 由绑定更新；范围/刻度属性变化会重画静态盘面，支持运行时切换展示单位。
// 本组件不依赖 dashboard，也不判定数据超时。
//
// 警戒区/指针颜色支持：
//   redZoneStartValue  > 0 时：绘制红色弧形警戒区
//   warningValue/dangerValue：动态指针颜色（绿→黄→红）
import QtQuick 2.15

Item {
    id: root
    property real value: 0
    property real minValue: 0
    property real maxValue: 260
    property string unit: "km/h"
    property int majorTickCount: 13
    property int minorTicksPerMajor: 5
    property real startAngleDeg: 135
    property real endAngleDeg: 405
    property string dialColor: "#1a2a1a"
    property string labelColor: "#88FF88"

    // ─── 指针颜色配置 ───
    property string needleColorNormal:  "#00FF88"
    property string needleColorWarning: "#FFAA00"
    property string needleColorDanger:  "#FF2200"
    property real warningValue: 220      // 黄→红分界
    property real dangerValue:  260      // 红区上限（用于指针颜色）

    // ─── 红色警戒区配置 ───
    // NaN = 不绘制红色区；有效值 = 警戒区起始值
    property real redZoneStartValue: NaN

    // 非空时覆盖中央数字显示（如 logic 超时 "---"）
    property string valueLabel: ""

    width: 280
    height: 280

    // 根据当前 value 计算指针颜色
    function computeNeedleColor(v) {
        if (v >= warningValue) return needleColorDanger
        if (v >= minValue + (maxValue - minValue) * 0.7) return needleColorWarning
        return needleColorNormal
    }
    // Canvas：勿 hex+"aa"（部分 Qt 拼出非法色）；统一 rgba
    function colorWithAlpha(hex, aHex) {
        var h = "" + hex
        if (h.length < 7 || h.charAt(0) !== "#")
            return "rgba(0,0,0," + (parseInt(aHex, 16) / 255.0) + ")"
        var r = parseInt(h.substr(1, 2), 16)
        var g = parseInt(h.substr(3, 2), 16)
        var b = parseInt(h.substr(5, 2), 16)
        return "rgba(" + r + "," + g + "," + b + "," + (parseInt(aHex, 16) / 255.0) + ")"
    }

    // 动态指针颜色（随 value 变化）
    property string currentNeedleColor: computeNeedleColor(value)

    // ─── 底层：静态盘面；尺寸变化由文件尾 handler 补画 ───
    Canvas {
        id: bgCanvas
        anchors.fill: parent
        antialiasing: true
        smooth: true

        Component.onCompleted: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            var w = width
            var h = height
            var cx = w / 2
            var cy = h / 2
            var outerR = Math.min(cx, cy) - 4

            ctx.clearRect(0, 0, w, h)

            // 外环发光
            var glow = ctx.createRadialGradient(cx, cy, outerR - 10, cx, cy, outerR + 15)
            glow.addColorStop(0, colorWithAlpha(needleColorNormal, "22"))
            glow.addColorStop(1, "transparent")
            ctx.beginPath()
            ctx.arc(cx, cy, outerR + 15, 0, 2 * Math.PI)
            ctx.fillStyle = glow
            ctx.fill()

            // 表盘外圈
            ctx.beginPath()
            ctx.arc(cx, cy, outerR, 0, 2 * Math.PI)
            ctx.fillStyle = "#0d0d0d"
            ctx.fill()

            // 表盘主体
            var bgGrad = ctx.createRadialGradient(cx, cy - outerR * 0.3, outerR * 0.1, cx, cy, outerR)
            bgGrad.addColorStop(0, dialColor)
            bgGrad.addColorStop(0.7, "#0a0a0a")
            bgGrad.addColorStop(1, "#050505")
            ctx.beginPath()
            ctx.arc(cx, cy, outerR - 2, 0, 2 * Math.PI)
            ctx.fillStyle = bgGrad
            ctx.fill()

            // 内圈
            var innerBgGrad = ctx.createRadialGradient(cx, cy, 0, cx, cy, outerR * 0.72)
            innerBgGrad.addColorStop(0, "#111111")
            innerBgGrad.addColorStop(1, "#0a0a0a")
            ctx.beginPath()
            ctx.arc(cx, cy, outerR * 0.72, 0, 2 * Math.PI)
            ctx.fillStyle = innerBgGrad
            ctx.fill()

            // 刻度线 + 数字
            var startAngle = startAngleDeg * Math.PI / 180
            var endAngle = endAngleDeg * Math.PI / 180
            var angleRange = endAngle - startAngle
            var totalMinorTicks = majorTickCount * minorTicksPerMajor

            for (var i = 0; i <= totalMinorTicks; i++) {
                var t = i / totalMinorTicks
                var angle = startAngle + t * angleRange
                var isMajor = (i % minorTicksPerMajor === 0)
                var tickOuter = outerR - 6
                var tickInner = tickOuter - (isMajor ? 18 : 10)
                var tickW = isMajor ? 2.0 : 1.0

                var cos = Math.cos(angle)
                var sin = Math.sin(angle)

                ctx.beginPath()
                ctx.moveTo(cx + tickInner * cos, cy + tickInner * sin)
                ctx.lineTo(cx + tickOuter * cos, cy + tickOuter * sin)
                ctx.strokeStyle = isMajor ? "#FFFFFF" : "#555555"
                ctx.lineWidth = tickW
                ctx.lineCap = "round"
                ctx.stroke()

                if (isMajor) {
                    var majorIndex = i / minorTicksPerMajor
                    var labelValue = minValue + (majorIndex / (majorTickCount)) * (maxValue - minValue)
                    var labelR = tickInner - 14
                    var lx = cx + labelR * cos
                    var ly = cy + labelR * sin + 4

                    ctx.font = "bold " + Math.round(outerR * 0.07) + "px Roboto Mono, monospace"
                    ctx.fillStyle = labelColor
                    ctx.textAlign = "center"
                    ctx.textBaseline = "middle"
                    ctx.fillText(labelValue.toFixed(0), lx, ly)
                }
            }

            // ─── 红色警戒区弧形（仅当 redZoneStartValue 有效时绘制）───
            if (!isNaN(redZoneStartValue) && redZoneStartValue > minValue && redZoneStartValue < maxValue) {
                var tStart = (redZoneStartValue - minValue) / (maxValue - minValue)
                var tEnd   = 1.0
                var arcStart = startAngleDeg + tStart * angleRange
                var arcEnd   = startAngleDeg + tEnd   * angleRange

                // 警戒区弧形（靠近外圈内侧）
                var arcR = outerR - 30
                ctx.beginPath()
                ctx.arc(cx, cy, arcR, arcStart * Math.PI / 180, arcEnd * Math.PI / 180)
                ctx.strokeStyle = "#FF220088"
                ctx.lineWidth = 6
                ctx.lineCap = "round"
                ctx.stroke()

                // 填充半透明红色扇形
                ctx.beginPath()
                ctx.moveTo(cx, cy)
                ctx.arc(cx, cy, arcR + 6, arcStart * Math.PI / 180, arcEnd * Math.PI / 180)
                ctx.closePath()
                var redFill = ctx.createRadialGradient(cx, cy, arcR - 5, cx, cy, arcR + 10)
                redFill.addColorStop(0, "#FF220015")
                redFill.addColorStop(1, "#FF220000")
                ctx.fillStyle = redFill
                ctx.fill()
            }

            // 中心圆帽
            ctx.beginPath()
            ctx.arc(cx, cy, outerR * 0.055, 0, 2 * Math.PI)
            var capGrad = ctx.createRadialGradient(cx - 2, cy - 2, 0, cx, cy, outerR * 0.055)
            capGrad.addColorStop(0, "#444444")
            capGrad.addColorStop(1, "#111111")
            ctx.fillStyle = capGrad
            ctx.fill()

            // 中心点
            ctx.beginPath()
            ctx.arc(cx, cy, outerR * 0.022, 0, 2 * Math.PI)
            ctx.fillStyle = needleColorNormal
            ctx.fill()
        }
    }

    // ─── 中层：静态针形；rotation 变化不触发 Canvas 像素重建 ───
    Item {
        id: needlePivot
        anchors.centerIn: parent
        width: 0
        height: 0

        property real needleLength: Math.max(0, Math.min(root.width, root.height) / 2 - 39)
        property real normalizedValue: {
            var span = root.maxValue - root.minValue
            if (Math.abs(span) < 1e-6)
                return 0
            return Math.max(0, Math.min(1, (root.value - root.minValue) / span))
        }

        rotation: root.startAngleDeg
                  + normalizedValue * (root.endAngleDeg - root.startAngleDeg)

        Canvas {
            id: needleCanvas
            x: -20
            y: -20
            width: needlePivot.needleLength + 40
            height: 40
            antialiasing: true
            smooth: true
            property string needleColor: root.currentNeedleColor

            Component.onCompleted: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onNeedleColorChanged: requestPaint()

            onPaint: {
                var ctx = getContext("2d")
                var pivotX = -x
                var pivotY = -y
                var needleCol = needleColor

                ctx.clearRect(0, 0, width, height)
                ctx.save()
                ctx.translate(pivotX, pivotY)
                ctx.shadowColor = needleCol
                ctx.shadowBlur = 10

                // 指针三角形：静态顶点朝右，由父 pivot 的 rotation 指向刻度
                ctx.beginPath()
                ctx.moveTo(needlePivot.needleLength, 0)
                ctx.lineTo(-8, -5)
                ctx.lineTo(-8,  5)
                ctx.closePath()

                var needleGrad = ctx.createLinearGradient(0, 0, -8, 0)
                needleGrad.addColorStop(0, needleCol)
                needleGrad.addColorStop(1, root.colorWithAlpha(needleCol, "aa"))
                ctx.fillStyle = needleGrad
                ctx.fill()
                ctx.restore()
            }
        }
    }

    // ─── 顶层：外环彩色渐变条（独立 Canvas，z-order 最上层）───
    // 画在表盘外圈位置（r=outerR-2），覆盖原 #0d0d0d 外圈，让 conic gradient 当新外圈
    Canvas {
        id: outerRingCanvas
        x: -20
        y: -20
        width: parent.width + 40
        height: parent.height + 40
        antialiasing: true
        smooth: true

        Component.onCompleted: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            var w = width
            var h = height
            var cx = w / 2
            var cy = h / 2
            // 用 parent 的 size 算 outerR（不要用 w/h，因为画布扩大了）
            var outerR = Math.min(parent.width, parent.height) / 2 - 4
            var startAngle = root.startAngleDeg * Math.PI / 180
            var endAngle = root.endAngleDeg * Math.PI / 180
            var angleRange = endAngle - startAngle
            var gradR = outerR - 2   // 画在表盘外圈上，覆盖原 #0d0d0d

            ctx.clearRect(0, 0, w, h)

            var gradColors = ['#00AAFF', '#00FF88', '#FFAA00', '#FF2200']
            var gradSegs = 60
            for (var g = 0; g < gradSegs; g++) {
                var gt1 = g / gradSegs
                var gt2 = (g + 1) / gradSegs
                var ga1 = startAngle + gt1 * angleRange
                var ga2 = startAngle + gt2 * angleRange
                // 4 段颜色之间插值
                var segT = gt1 * 4
                var idx = Math.floor(segT)
                var frac = segT - idx
                if (idx >= 4) { idx = 3; frac = 1 }
                var c1 = gradColors[idx]
                var c2 = gradColors[Math.min(idx + 1, 3)]
                var r1 = parseInt(c1.substr(1, 2), 16)
                var gn1 = parseInt(c1.substr(3, 2), 16)
                var b1 = parseInt(c1.substr(5, 2), 16)
                var r2 = parseInt(c2.substr(1, 2), 16)
                var gn2 = parseInt(c2.substr(3, 2), 16)
                var b2 = parseInt(c2.substr(5, 2), 16)
                var rr = Math.round(r1 + (r2 - r1) * frac)
                var gg = Math.round(gn1 + (gn2 - gn1) * frac)
                var bb = Math.round(b1 + (b2 - b1) * frac)
                var col = 'rgb(' + rr + ',' + gg + ',' + bb + ')'
                // 发光层（先画半透明粗线作 glow）
                ctx.save()
                ctx.shadowColor = col
                ctx.shadowBlur = 10
                ctx.beginPath()
                ctx.arc(cx, cy, gradR, ga1, ga2 + 0.004)
                ctx.strokeStyle = col
                ctx.lineWidth = 10
                ctx.lineCap = "butt"
                ctx.stroke()
                ctx.restore()
                // 主体（细亮线）
                ctx.beginPath()
                ctx.arc(cx, cy, gradR, ga1, ga2 + 0.004)
                ctx.strokeStyle = col
                ctx.lineWidth = 4
                ctx.lineCap = "butt"
                ctx.stroke()
            }
        }
    }

    // ─── 顶层：数值 + 单位（独立 Text，value 变化时更新）───
    // 指针底部在 cy+5，文字放在 cy+outerR*0.52 以下，避让指针
    Column {
        id: valueDisplay
        anchors.centerIn: parent
        anchors.verticalCenterOffset: root.width * 0.16
        Text {
            id: valueText
            anchors.horizontalCenter: parent.horizontalCenter
            // valueLabel 是调用方覆盖文本的唯一契约；本组件不存在继承型 SpeedGauge。
            text: root.valueLabel !== "" ? root.valueLabel : root.value.toFixed(0)
            color: root.valueLabel === "---" ? "#666666" : "#FFFFFF"
            font.family: "Roboto Mono, monospace"
            font.pixelSize: Math.round(root.width * 0.20)
            font.weight: Font.Bold
        }
        Text {
            id: unitText
            anchors.horizontalCenter: parent.horizontalCenter
            text: unit
            color: "#888888"
            font.family: "Roboto Mono, monospace"
            font.pixelSize: Math.round(root.width * 0.08)
            font.weight: Font.Bold
        }
    }

    onWidthChanged: bgCanvas.requestPaint()
    onHeightChanged: bgCanvas.requestPaint()
    onMinValueChanged: bgCanvas.requestPaint()
    onMaxValueChanged: bgCanvas.requestPaint()
    onMajorTickCountChanged: bgCanvas.requestPaint()
    onMinorTicksPerMajorChanged: bgCanvas.requestPaint()
    onStartAngleDegChanged: {
        bgCanvas.requestPaint()
        outerRingCanvas.requestPaint()
    }
    onEndAngleDegChanged: {
        bgCanvas.requestPaint()
        outerRingCanvas.requestPaint()
    }
    onRedZoneStartValueChanged: bgCanvas.requestPaint()
}
