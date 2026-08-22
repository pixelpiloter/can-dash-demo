// DashboardMain.qml — 1920×720 运行入口与整屏编排
//
// 上下文依赖（由 qt_main.cpp 注入）：
//   dashboard: QtBinder；displayData 合并 CAN 原始 double 与 logic.yaml 产出的
//              显示值，另暴露灯、告警与健康状态。
//   uiFpsHz: framework.yaml 的 ui_fps_hz；无效或缺失时按 60 Hz 运行平滑 Timer。
// 数据双轨：指针/阈值读取 canonical raw；展示边界由 UnitSystem(metric) 换算。
// 布局契约：固定像素定位面向 1920×720；主表均用 135°→405° 的 270° 弧。
import QtQuick 2.15
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3
import ClusterLogic 1.0

ApplicationWindow {
    id: root
    width: 1920
    height: 720
    visible: true
    title: "Cluster Logic 仪表盘"
    color: "#000000"

    UnitSystem {
        id: unitSystem
    }

    // ─── 平滑滤波状态 ───
    property real rawSpeed: 0
    property real rawRpm: 0
    property real displaySpeed: 0
    property real displayRpm: 0
    property bool speedTimedOut: false

    // ─── 平滑滤波周期: round(1000/uiFps) 且下限 1ms；缺省 60→17ms ───
    readonly property int uiFps: (typeof uiFpsHz !== "undefined" && uiFpsHz > 0) ? uiFpsHz : 60
    readonly property int frameMs: Math.max(1, Math.round(1000 / uiFps))
    property int _paintPhase: 0
    property bool _smoothingActive: false
    Timer {
        id: displayTimer
        interval: frameMs
        running: root._smoothingActive
        repeat: true
        onTriggered: {
            var alpha = 0.27
            var nextSpeed = displaySpeed + (rawSpeed - displaySpeed) * alpha
            var nextRpm = displayRpm + (rawRpm - displayRpm) * alpha
            if (Math.abs(rawSpeed - nextSpeed) < 0.05) nextSpeed = rawSpeed
            if (Math.abs(rawRpm - nextRpm) < 0.5) nextRpm = rawRpm
            displaySpeed = nextSpeed
            displayRpm = nextRpm
            var settled = nextSpeed === rawSpeed && nextRpm === rawRpm

            var rpmDelta = Math.abs(rpmGauge.value - displayRpm)
            _paintPhase = (_paintPhase + 1) % 2
            if (_paintPhase === 0 || rpmDelta > 40 || settled)
                rpmGauge.value = displayRpm
            if (settled)
                root._smoothingActive = false
        }
    }

    // ─── 监听合并快照：一次 displayDataChanged 同时覆盖 raw 与 logic 显示值 ───
    Connections {
        target: dashboard
        function onDisplayDataChanged() {
            var dd = dashboard.displayData
            var speedDisp = dd["cluster.speed"]
            if (speedDisp === "---") {
                root.speedTimedOut = true
                speedGauge.valueLabel = "---"
                rawSpeed = 0
            } else {
                root.speedTimedOut = false
                speedGauge.valueLabel = ""
                rawSpeed = Number(dd["cluster.speed"] || 0)
            }
            rawRpm = Number(dd["demo.motorRpm"] !== undefined ? dd["demo.motorRpm"] : 0)
            root._smoothingActive = true

            var voltDisp = dd["demo.batVolt"]
            var vRaw = Number(dd["demo.batVolt"] !== undefined ? dd["demo.batVolt"] : voltDisp)
            if (voltDisp === "--") {
                batVoltText.text = "-- V"
                batVoltText.color = "#888888"
            } else if (voltDisp === undefined) {
                batVoltText.text = "0.0 V"
                batVoltText.color = "#FF4400"
            } else {
                var v = Math.round(Number(voltDisp) * 10) / 10
                batVoltText.text = v.toFixed(1) + " V"
                batVoltText.color = vRaw > 380 ? "#00FF88" : vRaw > 320 ? "#FFAA00" : "#FF4400"
            }

            var powerDisp = dd["demo.batPower"]
            if (powerDisp === "--") {
                batPowerText.text = "-- kW"
                batPowerText.color = "#888888"
            } else if (powerDisp === undefined) {
                batPowerText.text = "0.0 kW"
                batPowerText.color = "#888888"
            } else {
                var p = Math.round(Number(powerDisp) * 10) / 10
                batPowerText.text = p.toFixed(1) + " kW"
                batPowerText.color = Math.abs(p) > 0.05 ? "#00AAFF" : "#666666"
            }

            // 20% SOC、380/320 V、0.05 kW 等是本 QML 的硬编码 UI 表现阈值，
            // 不是 logic.yaml 告警阈值。
            var soc = Math.round(dd["demo.batSoc"] || 0)
            socBar.width = batPanel.width * (soc / 100)
            socBar.color = soc < 20 ? "#FF2200" : "#00FF88"
            socText.text = "电量 " + soc + "%"

            // QML override 写入 indicatorStates。
            if (typeof dashboard.setIndicator === "function") {
                dashboard.setIndicator("park_brake_light", !dashboard.isMoving)
                dashboard.setIndicator("ready_go_light", dashboard.isMoving)
            }
        }
    }

    // ─── 背景 ───
    Rectangle {
        anchors.fill: parent
        color: "#050810"
    }

    // ─── 顶部指示灯条 ───
    Rectangle {
        id: indicatorBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 80
        color: "#CC000000"
        z: 5

        // 指示灯 = tt.*（与 Kanzi 属性同名）；demo 灯 = 业务 id（在 logic.yaml 里写）。
        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 300
            spacing: 14
            IndicatorLight { id: leftTurnLight;    width: 55; height: 55; symbol: "turn_left";     on: dashboard.lightOn("tt.turn_left");      flash: true;  flashHz: 1.5 }
            IndicatorLight { id: rightTurnLight;   width: 55; height: 55; symbol: "turn_right";    on: dashboard.lightOn("tt.turn_right");      flash: true;  flashHz: 1.5 }
            Rectangle { width: 2; height: 50; color: "#333333" }
            IndicatorLight { id: batWarnLight;     width: 55; height: 55; symbol: "bat";           on: dashboard.lightOn("bat_warn_light") || dashboard.warnOn("bat_overvolt") || dashboard.warnOn("bat_soc_low"); flash: true; flashHz: 2 }
            IndicatorLight { id: overspeedLight;   width: 55; height: 55; symbol: "check_engine"; on: dashboard.warnOn("overspeed"); flash: true; flashHz: 2 }
            IndicatorLight { id: parkBrakeLight;   width: 55; height: 55; symbol: "park";          on: dashboard.indicatorOn("park_brake_light");    flash: false }
            IndicatorLight { id: readyLight;       width: 55; height: 55; symbol: "ready";         on: dashboard.indicatorOn("ready_go_light");     flash: false }
            IndicatorLight { id: tireLight;        width: 55; height: 55; symbol: "tire";          on: dashboard.alarmActive && (dashboard.alarmMessageZh || "").indexOf("胎压") >= 0; flash: true; flashHz: 2 }
            IndicatorLight { id: engineLight;      width: 55; height: 55; symbol: "check_engine"; on: dashboard.lightOn("check_engine_light") || dashboard.warnOn("engine_fault"); flash: true;  flashHz: 1 }
            IndicatorLight { id: highVoltLight;    width: 55; height: 55; symbol: "high_volt";    on: dashboard.warnOn("bat_overvolt");  flash: false }
            IndicatorLight { id: fogLight;         width: 55; height: 55; symbol: "fog";           on: dashboard.indicatorOn("fog_light");          flash: false }
            IndicatorLight { id: seatbeltLight;     width: 55; height: 55; symbol: "seatbelt";      on: dashboard.warnOn("seatbelt");   flash: true;  flashHz: 2 }
            IndicatorLight { id: chargeLight;      width: 55; height: 55; symbol: "high_volt";    on: dashboard.warnOn("charge_fault") || (Number(dashboard.displayData["demo.isChargeView"] || 0) > 0.5); flash: dashboard.warnOn("charge_fault"); flashHz: 2 }
        }
    }

    // ─── 左侧：转速表 ───
    GaugeCanvas {
        id: rpmGauge
        x: 70; y: 130
        width: 340; height: 340
        minValue: 0; maxValue: 8000
        value: 0
        unit: "RPM"
        dialColor: "#1a3a5c"
        needleColorNormal: "#00AAFF"
        labelColor: "#88CCFF"
        majorTickCount: 8
        minorTicksPerMajor: 5
        startAngleDeg: 135
        endAngleDeg: 405
    }

    // ─── 中央：车速表 ───
    GaugeCanvas {
        id: speedGauge
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        y: -50
        width: 480; height: 480
        minValue: 0
        maxValue: unitSystem.speedFromKmh(260)
        warningValue: unitSystem.speedFromKmh(220)
        dangerValue: unitSystem.speedFromKmh(260)
        value: unitSystem.speedFromKmh(root.displaySpeed)
        unit: unitSystem.speedLabel
        dialColor: "#1a2a1a"
        needleColorNormal: "#00FF88"
        labelColor: "#88FF88"
        majorTickCount: 13
        minorTicksPerMajor: 5
        startAngleDeg: 135
        endAngleDeg: 405
    }

    // ─── 右侧：电池 + SOC + 行驶状态 + 温度 ───
    Column {
        x: 1500; y: 180
        spacing: 12

        Rectangle {
            width: 200; height: 62
            color: "#1a1a1a"; radius: 8
            border.color: "#333333"; border.width: 1
            Text {
                id: batVoltText
                anchors.centerIn: parent
                text: "-- V"
                color: "#00FF88"; font.pixelSize: 26; font.weight: Font.Bold
                font.family: "sans-serif"
            }
        }

        Rectangle {
            width: 200; height: 42
            color: "#1a1a1a"; radius: 8
            border.color: "#333333"; border.width: 1
            Text {
                id: batPowerText
                anchors.centerIn: parent
                text: "-- kW"
                color: "#00AAFF"; font.pixelSize: 20; font.weight: Font.Bold
                font.family: "sans-serif"
            }
        }

        Rectangle {
            id: batPanel
            width: 200; height: 26
            color: "#1a1a1a"; radius: 6
            border.color: "#333333"
            Rectangle {
                id: socBar
                anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                width: parent.width * ((Number(dashboard.displayData["demo.batSoc"] || 0)) / 100)
                radius: 6
                color: "#00FF88"
            }
            Text {
                id: socText
                anchors.centerIn: parent
                text: "电量 " + (Number(dashboard.displayData["demo.batSoc"] || 0)).toFixed(0) + "%"
                color: "#FFFFFF"; font.pixelSize: 12; font.weight: Font.Bold
            }
        }

        Rectangle {
            width: 200; height: 60
            color: "#1a1a1a"; radius: 8
            border.color: dashboard.isMoving ? "#00AA44" : "#333333"
            border.width: dashboard.isMoving ? 2 : 1
            Column { anchors.centerIn: parent; spacing: 1
                Text {
                    text: dashboard.isMoving ? "行驶中" : "停车"
                    color: dashboard.isMoving ? "#00FF88" : "#666666"; font.pixelSize: 20; font.weight: Font.Bold
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Text {
                    text: dashboard.isMoving ? ("正常 ⚡") : "待机 ◇"
                    color: "#888888"; font.pixelSize: 12
                    anchors.horizontalCenter: parent.horizontalCenter
                }
            }
        }

        Rectangle {
            width: 200; height: 50
            color: "#1a1a1a"; radius: 8; border.color: "#333333"
            Row { anchors.centerIn: parent; spacing: 6
                Text {
                    id: motorTempText
                    text: unitSystem.formatTemperature(
                              Number(dashboard.displayData["demo.motorTemp"] || 0), 0)
                          + unitSystem.temperatureLabel
                    color: "#FFAA00"; font.pixelSize: 22; font.weight: Font.Bold
                }
            }
        }
    }

    // ─── 底部中央：能量流图 + 历史曲线 ───
    EnergyFlowDiagram {
        id: energyFlow
        x: 60; y: 510
        width: 380; height: 140

        energyMode:    Math.round(Number(dashboard.displayData["demo.energyMode"]    || 0))
        batSoc:        Math.round(Number(dashboard.displayData["demo.batSoc"]        || 0))
        batteryTemp:   Math.round(Number(dashboard.displayData["demo.batteryTemp"]   || 0))
        engineRpm:     Math.round((Number(dashboard.displayData["demo.engineRpm"]    || 0)) / 50) * 50
        motorRpm:      Math.round((Number(dashboard.displayData["demo.motorRpm"]     || 0)) / 50) * 50
        vehicleSpeed:  Math.round(Number(dashboard.displayData["cluster.speed"] || 0))
        chargePower:   Math.round((Number(dashboard.displayData["demo.chargePowerText"] || 0)) * 10) / 10
        brakeActive:   (Number(dashboard.displayData["demo.brake"] || 0)) > 30
    }

    // 混动状态条: 档位 / 模式 / 续航 / 燃油 / 充电
    HybridStatusPanel {
        x: 750; y: 480
        width: 420; height: 100
    }

    // 速度历史曲线（60s 滑动窗口）
    Sparkline {
        x: 530; y: 595
        width: 240; height: 85
        title: "SPEED 60s"
        unit: unitSystem.speedLabel
        minValue: 0
        maxValue: unitSystem.speedFromKmh(260)
        displayScale: unitSystem.speedScale
        lineColor: "#00FF88"
        fillColor: "#2200FF88"
        sourceValue: Number(dashboard.displayData["cluster.speed"] || 0)
    }

    // RPM 历史曲线
    Sparkline {
        x: 785; y: 595
        width: 240; height: 85
        title: "MOTOR RPM 60s"
        unit: "rpm"
        minValue: 0; maxValue: 8000
        lineColor: "#00AAFF"
        fillColor: "#2200AAFF"
        sourceValue: Number(dashboard.displayData["demo.motorRpm"] || 0)
    }

    // 全量 active 告警列表
    WarningPanel {
        x: 1370; y: 595
        width: 320; height: 100
    }

    // ─── 报警横幅（最高层）───
    AlarmBanner {
        id: alarmBanner
    }

    // ─── 底部状态栏 ───
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 55
        color: "#AA000000"; border.color: "#333333"; border.width: 1

        // 数据健康指标 (FPS / age / frame seq / dropped) — 右侧
        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 20
            spacing: 16

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "FPS " + dashboard.dataFps.toFixed(1)
                color: dashboard.dataFps >= 50 ? "#00AA44" :
                       dashboard.dataFps >= 30 ? "#FFAA00" : "#FF4400"
                font.pixelSize: 13
                font.family: "Roboto Mono, monospace"
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "AGE " + dashboard.dataAgeMs + "ms"
                color: dashboard.dataAgeMs < 20  ? "#00AA44" :
                       dashboard.dataAgeMs < 40  ? "#FFAA00" : "#FF4400"
                font.pixelSize: 13
                font.family: "Roboto Mono, monospace"
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "SEQ " + dashboard.frameSeq
                color: "#88CCFF"
                font.pixelSize: 13
                font.family: "Roboto Mono, monospace"
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: dashboard.droppedFrames > 0
                text: "DROP " + dashboard.droppedFrames
                color: "#FF6600"
                font.pixelSize: 13
                font.weight: Font.Bold
                font.family: "Roboto Mono, monospace"
            }
        }

        Row { anchors.fill: parent; anchors.margins: 10; spacing: 40
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "⏱ v1.0"
                color: "#666666"; font.pixelSize: 14
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Cluster Logic"
                color: "#444444"; font.pixelSize: 14
            }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 10; height: 10; radius: 5
                color: dashboard.alarmActive ? "#FF4400" : "#00AA44"
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: dashboard.alarmActive ? ("⚠ " + dashboard.alarmMessageZh) : "系统正常"
                color: dashboard.alarmActive ? "#FF4400" : "#00AA44"
                font.pixelSize: 16; font.weight: Font.Bold
            }
        }
    }

    Component.onCompleted: {
        console.log("DashboardMain.qml loaded - 1920x720")
    }
}
