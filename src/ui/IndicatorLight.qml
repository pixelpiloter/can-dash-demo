// IndicatorLight.qml — 无数据源依赖的通用 Canvas 指示灯
//
// 输入契约：on 是总使能；flash=false 时持续亮，flash=true 时按 flashHz 驱动
// flashPhase；symbol 只选择图形/默认颜色，不代表任何 CAN、light 或 warn key。
// 调用方（当前为 DashboardMain）负责把 dashboard.indicatorOn/lightOn/warnOn
// 等状态映射到 on；本组件自身不直接依赖 dashboard。
// on=false 时 flashTimer 停止且 lit 恒为 false，Canvas 仅保留暗态轮廓；on/flash
// 变化均把闪烁相位重置为亮，避免重新启用时继承旧相位。
// 单位：flashHz 为 Hz；调用方应提供正值。本组件没有超时/占位文本。
import QtQuick 2.15

Item {
    id: root

    // 指示灯状态
    property bool on: false
    property bool flash: false
    property real flashHz: 2.0   // Hz

    // 符号类型: "turn_left" | "turn_right" | "high_beam" | "check_engine"
    //            "fog" | "reverse" | "park" | "tire" | "ready" | "high_volt"
    //            "bat" | "seatbelt"
    property string symbol: "bat"

    // 颜色（可按类型预设，也可在外部覆盖）
    property color onColor: colorForSymbol(symbol)
    property color offColor: offColorForSymbol(symbol)

    width: 60
    height: 60

    // 闪烁时序：每半周期离散翻转一次，避免连续动画逐帧触发 Canvas 重画
    Timer {
        id: flashTimer
        interval: 500 / flashHz
        running: flash && root.on
        repeat: true
        onTriggered: {
            root.flashPhase = root.flashPhase === 0 ? 1 : 0
            canvas.requestPaint()
        }
    }

    property int flashPhase: 0   // 0=亮, 1=暗
    readonly property bool lit: on && (flash ? flashPhase === 0 : true)

    function resetFlashPhase() {
        flashPhase = 0
        canvas.requestPaint()
    }

    // ─── 颜色映射 ───
    function colorForSymbol(sym) {
        switch (sym) {
            case "turn_left": case "turn_right":   return "#FFAA00"
            case "high_beam":                      return "#00AAFF"
            case "check_engine":                   return "#FFAA00"
            case "fog":                            return "#FFAA00"
            case "reverse":                        return "#FFFFFF"
            case "park":                           return "#FF2200"
            case "tire":                           return "#FFAA00"
            case "ready":                          return "#00FF88"
            case "high_volt":                      return "#FFAA00"
            case "bat":                            return "#FF2200"
            case "seatbelt":                       return "#FF2200"
            default:                               return "#FF2200"
        }
    }

    function offColorForSymbol(sym) {
        switch (sym) {
            case "turn_left": case "turn_right":   return "#3a2800"
            case "high_beam":                      return "#001a33"
            case "check_engine":                   return "#332800"
            case "fog":                            return "#1a1a1a"
            case "reverse":                        return "#222222"
            case "park":                           return "#330011"
            case "tire":                           return "#332800"
            case "ready":                          return "#001a0d"
            case "high_volt":                      return "#332800"
            case "bat":                            return "#330011"
            default:                               return "#330011"
        }
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        antialiasing: true

        onPaint: {
            var ctx = getContext("2d")
            var cx = width / 2
            var cy = height / 2
            var r = Math.min(cx, cy) - 3

            ctx.clearRect(0, 0, width, height)

            var col  = lit ? root.onColor   : root.offColor
            var alpha = lit ? 1.0 : 0.0     // inactive 灯完全透明（只看到背景 indicatorBar）

            // 外圈发光（仅 active 时）
            if (lit) {
                var glow = ctx.createRadialGradient(cx, cy, r * 0.7, cx, cy, r * 1.3)
                glow.addColorStop(0, "rgba(" + _hex(col) + ",0.35)")
                glow.addColorStop(1, "rgba(0,0,0,0)")
                ctx.beginPath()
                ctx.arc(cx, cy, r * 1.3, 0, 2 * Math.PI)
                ctx.fillStyle = glow
                ctx.fill()
            }

            if (lit) {
                // ─── active: 实心彩色圆 + 发光（取代原金属边框+镜头玻璃）───
                ctx.beginPath()
                ctx.arc(cx, cy, r * 0.92, 0, 2 * Math.PI)
                var fillGrad = ctx.createRadialGradient(cx - r * 0.2, cy - r * 0.2, 0, cx, cy, r * 0.92)
                fillGrad.addColorStop(0, "rgba(" + _hex(col) + ",0.95)")
                fillGrad.addColorStop(1, "rgba(" + _hex(col) + ",0.45)")
                ctx.fillStyle = fillGrad
                ctx.fill()
            } else {
                // ─── inactive: 极简描边（直径 -1px 细线 + 4px 圆点）───
                ctx.strokeStyle = "rgba(80,80,80,0.55)"
                ctx.lineWidth = 1
                ctx.beginPath()
                ctx.arc(cx, cy, r * 0.78, 0, 2 * Math.PI)
                ctx.stroke()
            }

            // 画符号
            ctx.fillStyle = lit ? "rgba(255,255,255,0.95)" : "rgba(120,120,120,0.7)"
            ctx.textAlign = "center"
            ctx.textBaseline = "middle"
            if (lit) {
                ctx.shadowColor = "rgba(" + _hex(col) + ",0.8)"
                ctx.shadowBlur = 6
            }
            _drawSymbol(ctx, cx, cy, r * 0.65, symbol)
            ctx.shadowBlur = 0
        }

        // 辅助：将 Qt color 转成 "r,g,b" 字符串
        function _hex(c) {
            return Math.round(c.r * 255) + "," + Math.round(c.g * 255) + "," + Math.round(c.b * 255)
        }

        // 画符号
        function _drawSymbol(ctx, cx, cy, size, sym) {
            switch (sym) {
                case "turn_left":
                    // 左箭头
                    ctx.beginPath()
                    ctx.moveTo(cx + size * 0.5, cy)
                    ctx.lineTo(cx - size * 0.2, cy - size * 0.5)
                    ctx.lineTo(cx - size * 0.2, cy - size * 0.15)
                    ctx.lineTo(cx - size * 0.5, cy - size * 0.15)
                    ctx.lineTo(cx - size * 0.5, cy + size * 0.15)
                    ctx.lineTo(cx - size * 0.2, cy + size * 0.15)
                    ctx.lineTo(cx - size * 0.2, cy + size * 0.5)
                    ctx.closePath()
                    ctx.fill()
                    break
                case "turn_right":
                    // 右箭头（镜像）
                    ctx.beginPath()
                    ctx.moveTo(cx - size * 0.5, cy)
                    ctx.lineTo(cx + size * 0.2, cy - size * 0.5)
                    ctx.lineTo(cx + size * 0.2, cy - size * 0.15)
                    ctx.lineTo(cx + size * 0.5, cy - size * 0.15)
                    ctx.lineTo(cx + size * 0.5, cy + size * 0.15)
                    ctx.lineTo(cx + size * 0.2, cy + size * 0.15)
                    ctx.lineTo(cx + size * 0.2, cy + size * 0.5)
                    ctx.closePath()
                    ctx.fill()
                    break
                case "high_beam":
                    // 远光灯：三道光束
                    ctx.beginPath()
                    ctx.moveTo(cx - size * 0.4, cy + size * 0.5)
                    ctx.lineTo(cx - size * 0.1, cy + size * 0.1)
                    ctx.lineTo(cx - size * 0.25, cy + size * 0.1)
                    ctx.lineTo(cx + size * 0.25, cy - size * 0.5)
                    ctx.lineTo(cx + size * 0.1, cy - size * 0.1)
                    ctx.lineTo(cx + size * 0.25, cy - size * 0.1)
                    ctx.closePath()
                    ctx.fill()
                    break
                case "check_engine":
                    // 发动机：简单梯形+曲轴
                    ctx.font = "bold " + Math.round(size * 1.3) + "px sans-serif"
                    ctx.fillText("⚙", cx, cy + 1)
                    break
                case "fog":
                    // 雾灯：三层波浪线
                    for (var i = 0; i < 3; i++) {
                        ctx.beginPath()
                        var yoff = (i - 1) * size * 0.35
                        ctx.moveTo(cx - size * 0.5, cy + yoff)
                        ctx.quadraticCurveTo(cx - size * 0.25, cy + yoff - size * 0.2,
                                              cx, cy + yoff)
                        ctx.quadraticCurveTo(cx + size * 0.25, cy + yoff + size * 0.2,
                                              cx + size * 0.5, cy + yoff)
                        ctx.lineWidth = size * 0.12
                        ctx.strokeStyle = ctx.fillStyle
                        ctx.stroke()
                    }
                    break
                case "reverse":
                    // 倒车档：R
                    ctx.font = "bold " + Math.round(size * 1.4) + "px sans-serif"
                    ctx.fillText("R", cx, cy + 1)
                    break
                case "park":
                    // 驻车制动：P
                    ctx.font = "bold " + Math.round(size * 1.4) + "px sans-serif"
                    ctx.fillText("P", cx, cy + 1)
                    break
                case "tire":
                    // 胎压：感叹号
                    ctx.font = "bold " + Math.round(size * 1.5) + "px sans-serif"
                    ctx.fillText("!", cx, cy + 1)
                    break
                case "ready":
                    // READY 文字
                    ctx.font = "bold " + Math.round(size * 0.55) + "px sans-serif"
                    ctx.fillText("RDY", cx, cy + 1)
                    break
                case "high_volt":
                    // 高压：闪电符号
                    ctx.beginPath()
                    ctx.moveTo(cx - size * 0.1, cy - size * 0.5)
                    ctx.lineTo(cx + size * 0.3, cy - size * 0.05)
                    ctx.lineTo(cx - size * 0.05, cy - size * 0.05)
                    ctx.lineTo(cx + size * 0.15, cy + size * 0.5)
                    ctx.lineTo(cx - size * 0.2, cy + size * 0.05)
                    ctx.lineTo(cx + size * 0.15, cy + size * 0.05)
                    ctx.closePath()
                    ctx.fill()
                    break
                case "seatbelt":
                    // 安全带：斜向条带 + 卡扣
                    ctx.lineWidth = size * 0.18
                    ctx.lineCap = "round"
                    // 斜向带（从左上到右下）
                    ctx.beginPath()
                    ctx.moveTo(cx - size * 0.45, cy - size * 0.4)
                    ctx.lineTo(cx + size * 0.1, cy + size * 0.4)
                    ctx.stroke()
                    // 横带（水平）
                    ctx.beginPath()
                    ctx.moveTo(cx - size * 0.1, cy - size * 0.1)
                    ctx.lineTo(cx + size * 0.45, cy - size * 0.1)
                    ctx.stroke()
                    // 卡扣圆
                    ctx.beginPath()
                    ctx.arc(cx - size * 0.05, cy + size * 0.05, size * 0.12, 0, Math.PI * 2)
                    ctx.fill()
                    break
                case "bat":
                    // 电池：默认感叹号
                    ctx.font = "bold " + Math.round(size * 1.5) + "px sans-serif"
                    ctx.fillText("!", cx, cy + 1)
                    break
                default:
                    ctx.font = "bold " + Math.round(size * 1.5) + "px sans-serif"
                    ctx.fillText("!", cx, cy + 1)
                    break
            }
        }
    }

    onOnChanged: resetFlashPhase()
    onFlashChanged: resetFlashPhase()
    onSymbolChanged: canvas.requestPaint()
    Component.onCompleted: canvas.requestPaint()
}
