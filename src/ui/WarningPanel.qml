// WarningPanel.qml — LogicEngine 活动告警只读列表
//
// 数据流：LogicEngine Snapshot.warn_state
//   → UiSnapshot → QtBinder::buildAlarmList（ConfigCatalog 补文案/
//   颜色/统一 severity/priority_rank）→ dashboard.warningActiveList/count/hasCritical。
// warningActiveList 是 alarmList 的全量 active 别名；当前 warn_state 按 id 唯一，
// 所以 dedup_count 显式固定为 1。
//
// 面板元素 (4 段):
//   1. 顶部状态点: 灰 (0 active) / 黄 (WARNING) / 红 (CRITICAL)
//   2. 顶部标题 "WARN" + 右侧 count badge (≥1 时显示数字, ≥1 critical 时变红)
//   3. 中部列表: 视觉上最多 3 条；当前 delegate 固定显示 text_zh
//      严重度色条 (左) + 文本 + dedup_count 角标 (>1 时显示 "xN")
//   4. 底部统计: "N total, K critical" — count 跨行方便扫读
//
// 本面板只读，不提供 acknowledge。severity 由统一 priority 分类保证可用；
// 不在 QML 端重排或重新判定告警。无 Timer/Canvas。
import QtQuick 2.15
import QtQuick.Controls 2.15
import ClusterLogic 1.0

Item {
    id: root
    width: 320
    height: 100

    // ─── 背景 (边框色随 hasCritical 变化) ───
    Rectangle {
        anchors.fill: parent
        radius: 6
        color: "#1a1a1a"
        border.color: dashboard.hasCritical ? "#D32F2F" :
                       dashboard.warningCount > 0 ? "#FFC107" : "#333333"
        border.width: dashboard.hasCritical ? 2 : 1
    }

    // ─── 标题栏 (左上) ───
    Row {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 8
        spacing: 6

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "WARN"
            color: "#888888"
            font.pixelSize: 11
            font.letterSpacing: 1
            font.family: "Roboto Mono, monospace"
        }

        // 状态点 (紧跟标题)
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 8; height: 8; radius: 4
            color: dashboard.hasCritical ? "#D32F2F" :
                   dashboard.warningCount > 0 ? "#FFC107" : "#00AA44"
        }
    }

    // ─── count badge (右上) ───
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
        width: countText.width + 12
        height: 18
        radius: 3
        visible: dashboard.warningCount > 0
        color: dashboard.hasCritical ? "#D32F2F" : "#FFC107"

        Text {
            id: countText
            anchors.centerIn: parent
            text: "x" + dashboard.warningCount
            color: "#000000"
            font.pixelSize: 10
            font.weight: Font.Bold
            font.family: "Roboto Mono, monospace"
        }
    }

    // ─── 告警列表 (中部) ───
    // 用 ListView 显示最多 3 条, 多的滚出可视区避免布局抖动
    ListView {
        id: warnList
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 28
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 4
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        clip: true
        interactive: false
        spacing: 2
        model: dashboard.warningActiveList
        // 仅显示前 3 条；本组件不排序，沿用 QtBinder 列表顺序。
        property int maxRows: 3

        delegate: Rectangle {
            width: warnList.width
            height: 18
            color: "transparent"
            visible: index < warnList.maxRows

            // 严重度色条 (左侧 3px)
            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 3
                radius: 1
                color: modelData.severity === 2 ? "#D32F2F" :
                       modelData.severity === 1 ? "#FFC107" : "#888888"
            }

            // 当前固定 text_zh。
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.right: countBadge.left
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                text: modelData.text_zh
                color: modelData.severity === 2 ? "#FFAAAA" :
                       modelData.severity === 1 ? "#FFE699" : "#CCCCCC"
                font.pixelSize: 10
                font.family: "Noto Sans CJK SC, sans-serif"
            }

            // dedup_count 角标 (右侧, >1 时显示)
            Rectangle {
                id: countBadge
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: badgeText.width + 6
                height: 12
                radius: 2
                visible: modelData.dedup_count > 1
                color: "#444444"
                Text {
                    id: badgeText
                    anchors.centerIn: parent
                    text: "x" + modelData.dedup_count
                    color: "#CCCCCC"
                    font.pixelSize: 8
                    font.family: "Roboto Mono, monospace"
                }
            }
        }
    }

    // ─── 空态提示 (count=0 时覆盖列表) ───
    Text {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 8
        visible: dashboard.warningCount === 0
        text: "无告警"
        color: "#00AA44"
        font.pixelSize: 11
        font.family: "Noto Sans CJK SC, sans-serif"
    }
}
