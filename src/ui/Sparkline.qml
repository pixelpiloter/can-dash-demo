// Sparkline.qml — 轻量 raw 指标历史曲线（默认 200ms 采样、60s 窗口）
//
// 输入契约：sourceValue 与 samples 始终保存 canonical 数值；displayScale 只在
// 绘制和当前值文本处换算。这样单位切换不会混用两种单位的历史样本，也不必等待
// 下一次采样即可刷新当前值。组件不消费 *_disp，不解释 "--"/"---"。
// dashboard.displayData 是整张 QVariantMap；每次 map 替换都可能让 sourceValue
// 绑定重评，但本组件只在内部 Timer tick 时复制一次当前值并推进 samples。
// 默认 200ms × 300 点 = 60s。stableEpsilon 仅用于判定连续样本近似稳定；只有
// 缓冲已满且整窗都稳定时才停止数组复制和重绘，Timer 本身仍按可见性运行。
// Canvas 使用 FBO；隐藏、透明或零尺寸时采样 Timer 停机。dashboard 仅用于标题
// 字体回退，不是数据源的隐式依赖。
import QtQuick 2.15

Item {
    id: root
    width: 280
    height: 90

    // ─── 输入参数 ───
    property real sourceValue: 0
    property real displayScale: 1
    property real minValue: 0
    property real maxValue: 100
    property color lineColor: "#00FF88"
    property color fillColor: "#2200FF88"
    property color gridColor: "#22FFFFFF"
    property string title: ""
    property string unit: ""
    property int sampleIntervalMs: 200     // 200ms 采样 → 60s 窗口 300 点
    property int windowSeconds: 60
    property real stableEpsilon: 0.001     // 连续采样近似相同的容差

    // ─── 内部状态 ───
    property var samples: []               // 环形缓冲 [{t, v}, ...]
    readonly property real currentValue: sourceValue * displayScale
    property int _stableSampleCount: 0
    property real _stableReferenceValue: 0
    property bool _hasStableReference: false

    // 限长（保留 windowSeconds/sampleIntervalMs*1000 个点）
    property int maxSamples: Math.ceil(windowSeconds * 1000 / sampleIntervalMs)

    // ─── 采样定时器 ───
    Timer {
        id: sampleTimer
        interval: root.sampleIntervalMs
        running: root.visible && root.opacity > 0 && root.width > 0 && root.height > 0
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            var value = root.sourceValue

            var epsilon = Math.max(0, root.stableEpsilon)
            if (!root._hasStableReference
                    || Math.abs(value - root._stableReferenceValue) > epsilon) {
                root._stableReferenceValue = value
                root._stableSampleCount = 1
                root._hasStableReference = true
            } else {
                // 缓冲中已经有完整窗口的近似恒定值时，数组和画面都无需再推进。
                if (root.samples.length === root.maxSamples
                        && root._stableSampleCount >= root.maxSamples)
                    return
                root._stableSampleCount++
            }

            var s = root.samples.slice()
            s.push(value)
            while (s.length > root.maxSamples) s.shift()
            root.samples = s
            sparklineCanvas.requestPaint()
        }
    }

    // ─── 画布 ───
    Canvas {
        id: sparklineCanvas
        anchors.fill: parent
        antialiasing: true
        renderTarget: Canvas.FramebufferObject

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            var w = width
            var h = height
            var padTop = 4
            var padBottom = 18   // 留出标题/单位
            var padLeft = 4
            var padRight = 4
            var plotW = w - padLeft - padRight
            var plotH = h - padTop - padBottom

            // 网格线（横线 4 等分）
            ctx.strokeStyle = root.gridColor
            ctx.lineWidth = 1
            for (var i = 1; i < 4; i++) {
                var y = padTop + plotH * i / 4
                ctx.beginPath()
                ctx.moveTo(padLeft, y)
                ctx.lineTo(padLeft + plotW, y)
                ctx.stroke()
            }

            var s = root.samples
            if (!s || s.length < 2) return

            // 数值归一化
            var range = root.maxValue - root.minValue
            if (range <= 0) range = 1
            function norm(v) {
                var displayValue = v * root.displayScale
                var n = (displayValue - root.minValue) / range
                if (n < 0) n = 0
                if (n > 1) n = 1
                return n
            }

            var n = s.length
            var dx = plotW / (root.maxSamples - 1)

            // 填充区域
            ctx.beginPath()
            ctx.moveTo(padLeft, padTop + plotH)
            for (var k = 0; k < n; k++) {
                var px = padLeft + k * dx
                var py = padTop + plotH * (1 - norm(s[k]))
                ctx.lineTo(px, py)
            }
            ctx.lineTo(padLeft + (n - 1) * dx, padTop + plotH)
            ctx.closePath()
            ctx.fillStyle = root.fillColor
            ctx.fill()

            // 折线
            ctx.beginPath()
            for (var j = 0; j < n; j++) {
                var x2 = padLeft + j * dx
                var y2 = padTop + plotH * (1 - norm(s[j]))
                if (j === 0) ctx.moveTo(x2, y2)
                else ctx.lineTo(x2, y2)
            }
            ctx.strokeStyle = root.lineColor
            ctx.lineWidth = 1.6
            ctx.lineJoin = "round"
            ctx.stroke()

            // 当前值圆点
            var lastY = padTop + plotH * (1 - norm(s[n - 1]))
            var lastX = padLeft + (n - 1) * dx
            ctx.beginPath()
            ctx.arc(lastX, lastY, 2.5, 0, Math.PI * 2)
            ctx.fillStyle = root.lineColor
            ctx.fill()
        }
    }

    // ─── 标题 + 当前值 ───
    Text {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: 4
        anchors.bottomMargin: 2
        text: root.title
        color: "#888888"
        font.pixelSize: 11
        font.family: "sans-serif"
    }

    Text {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 4
        anchors.bottomMargin: 2
        text: root.currentValue.toFixed(0) + (root.unit ? " " + root.unit : "")
        color: root.lineColor
        font.pixelSize: 12
        font.weight: Font.Bold
        font.family: "Roboto Mono, monospace"
    }

    onDisplayScaleChanged: sparklineCanvas.requestPaint()
    onMinValueChanged: sparklineCanvas.requestPaint()
    onMaxValueChanged: sparklineCanvas.requestPaint()
}
