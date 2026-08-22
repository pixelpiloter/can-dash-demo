// HybridStatusPanel.qml — 混动状态条：档位 / 能量模式 / 续航 / 燃油 / 充电
//
// 数据链：SnapshotPump 采样 SharedState 与 LogicEngine 输出，QtBinder 将其作为
// dashboard.displayData 暴露；充电视图由 logic.yaml 的 demo.isChargeView
// 规则派生。
// 文本优先消费 logic.yaml 的 gear/energy/range/charge 显示值，以保留 "--" 超时；
// range *_disp 与 raw range 都是 canonical km，仅在本展示边界换算为 km/mi。
// energy_mode_disp 契约为 EV/HEV/ENG/CHG/OFF，超时为 "--"；面板不读 limpHome*。
// 无 Timer/Canvas；仅随 dashboard 属性绑定重评，隐藏时没有独立后台工作。
import QtQuick 2.15

Item {
    id: root
    width: 420
    height: 110
    property int units: 0

    UnitSystem {
        id: unitSystem
        units: root.units
    }

    readonly property var dd: (typeof dashboard !== "undefined" && dashboard.displayData)
                              ? dashboard.displayData : ({})
    readonly property bool chargeView: (Number(dd["demo.isChargeView"] || 0)) > 0.5
    // 档位统一读 cluster.gear（DBC gear_status 0P/1R/2N/3D 已在规则中转成 1P/2R/3N/4D）
    readonly property string gearText: {
        var g = dd["cluster.gear"]
        var n = (g !== undefined && g !== null && g !== "" && g !== "--") ? Number(g) : 0
        return n === 1 ? "P" : n === 2 ? "R" : n === 3 ? "N" : "D"
    }
    readonly property string modeText: {
        var m = dd["demo.energyModeText"]
        if (m !== undefined && m !== null && m !== "") return "" + m
        return "--"
    }
    readonly property real fuelPct: {
        var f = dd["demo.fuelLevel"]
        if (f === "--" || f === undefined || f === null) return 0
        return Number(f)
    }
    readonly property string evRangeText: {
        var v = dd["demo.evRange"]
        if (v === "--") return "--"
        if (v !== undefined && v !== null)
            return unitSystem.formatDistance(Number(v), 0) + " " + unitSystem.distanceLabel
        return "--"
    }
    readonly property string fuelRangeText: {
        var v = dd["demo.fuelRange"]
        if (v === "--") return "--"
        if (v !== undefined && v !== null)
            return unitSystem.formatDistance(Number(v), 0) + " " + unitSystem.distanceLabel
        return "--"
    }
    readonly property string totalRangeText: {
        var v = dd["demo.totalRange"]
        if (v === "--") return "--"
        if (v !== undefined && v !== null)
            return unitSystem.formatDistance(Number(v), 0) + " " + unitSystem.distanceLabel
        return "--"
    }
    readonly property string chargePowerText: {
        var v = dd["demo.chargePowerText"]
        if (v === "--") return "-- kW"
        if (v !== undefined && v !== null) return Number(v).toFixed(1) + " kW"
        return "0.0 kW"
    }

    Rectangle {
        anchors.fill: parent
        color: root.chargeView ? "#0a1a12" : "#1a1a1a"
        radius: 8
        border.color: root.chargeView ? "#00FF88" : "#333333"
        border.width: root.chargeView ? 2 : 1

        Row {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 10

            // 档位
            Rectangle {
                width: 56; height: parent.height
                color: "#111111"; radius: 6
                border.color: "#00AAFF"; border.width: 1
                Text {
                    anchors.centerIn: parent
                    text: root.gearText
                    color: "#00AAFF"
                    font.pixelSize: 36
                    font.weight: Font.Bold
                    font.family: "sans-serif"
                }
            }

            // 能量模式
            Column {
                width: 70
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                Text {
                    text: "MODE"
                    color: "#666666"
                    font.pixelSize: 10
                    font.family: "Roboto Mono, monospace"
                }
                Text {
                    text: root.modeText
                    color: root.modeText === "EV" ? "#00FF88"
                         : root.modeText === "HEV" ? "#FFAA00"
                         : root.modeText === "ENG" ? "#FF6600"
                         : root.modeText === "CHG" ? "#00AAFF"
                         : "#888888"
                    font.pixelSize: 22
                    font.weight: Font.Bold
                    font.family: "sans-serif"
                }
            }

            // 续航
            Column {
                width: 120
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    text: "RANGE  EV / FUEL / TOTAL"
                    color: "#666666"
                    font.pixelSize: 9
                    font.family: "Roboto Mono, monospace"
                }
                Text {
                    text: root.evRangeText + " / " + root.fuelRangeText
                    color: "#88CCFF"
                    font.pixelSize: 14
                    font.family: "Roboto Mono, monospace"
                }
                Text {
                    text: "Σ " + root.totalRangeText
                    color: "#FFFFFF"
                    font.pixelSize: 16
                    font.weight: Font.Bold
                    font.family: "sans-serif"
                }
            }

            // 燃油条
            Column {
                width: 90
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                Text {
                    text: "FUEL " + Math.round(root.fuelPct) + "%"
                    color: root.fuelPct < 10 ? "#FF4400" : "#FFAA00"
                    font.pixelSize: 11
                    font.family: "Roboto Mono, monospace"
                }
                Rectangle {
                    width: parent.width; height: 10
                    color: "#222222"; radius: 4
                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, root.fuelPct / 100))
                        height: parent.height
                        radius: 4
                        color: root.fuelPct < 10 ? "#FF4400" : "#FFAA00"
                    }
                }
            }

            // 充电功率 (充电视图高亮)
            Column {
                width: 80
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4
                visible: root.chargeView || Number(dd["demo.chargePowerText"] || 0) > 0.1
                Text {
                    text: "CHARGE"
                    color: "#00FF88"
                    font.pixelSize: 10
                    font.family: "Roboto Mono, monospace"
                }
                Text {
                    text: root.chargePowerText
                    color: "#00FF88"
                    font.pixelSize: 16
                    font.weight: Font.Bold
                    font.family: "sans-serif"
                }
            }
        }
    }
}
