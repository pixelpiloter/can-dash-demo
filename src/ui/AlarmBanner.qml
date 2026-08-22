// AlarmBanner.qml — dashboard.alarmList 的短时横幅容器
//
// 数据契约：alarmList 由 QtBinder 直接从 LogicEngine warn_state 与 ConfigCatalog
// 组装；元素提供 name/text_zh/text_en/color/font_size/priority_rank。本组件监听
// alarmActiveChanged，将本地模型同步到上游按 priority_rank、name 排好的前 3 条。
// 已解除或跌出前 3 的条目立即移除，不等待旧生命周期 Timer。
// 每个新 AlarmBannerItem 仍只执行一次 200ms 入场→3000ms 停留→300ms 淡出后
// 删除自己的生命周期。单位仅为毫秒，无超时占位。
// z=9999 是 DashboardMain 最高视觉层；组件无轮询 Timer，只有活动项的单次 Timer。
import QtQuick 2.15
import QtQuick.Controls 2.5

Item {
    id: root
    anchors.horizontalCenter: parent.horizontalCenter
    y: 88
    z: 9999
    width: 620
    height: implicitHeight
    clip: false

    readonly property int maxAlarms: 3

    // 报警数据模型
    ListModel {
        id: alarmModel
    }

    // ─── alarmActiveChanged 同时承载 active 列表内容变化 ─────────────────────
    Connections {
        target: dashboard
        function onAlarmActiveChanged() {
            syncAlarmsFromList()
        }
    }

    function desiredContainsName(list, count, name) {
        for (var i = 0; i < count; i++) {
            if ((list[i].name || "") === name)
                return true
        }
        return false
    }

    function modelIndexForName(name) {
        for (var i = 0; i < alarmModel.count; i++) {
            if (alarmModel.get(i).name === name)
                return i
        }
        return -1
    }

    // 与 backend 当前列表增量同步：清 stale、保留仍 active 项的单次 Timer、添加新项。
    function syncAlarmsFromList() {
        var list = dashboard.alarmList
        var desiredCount = list ? Math.min(list.length, maxAlarms) : 0

        for (var staleIndex = alarmModel.count - 1; staleIndex >= 0; staleIndex--) {
            if (!desiredContainsName(list, desiredCount, alarmModel.get(staleIndex).name))
                alarmModel.remove(staleIndex)
        }

        for (var i = 0; i < desiredCount; i++) {
            var alarm = list[i]
            var name = alarm.name || ""
            var existingIndex = modelIndexForName(name)
            if (existingIndex < 0) {
                alarmModel.insert(i, {
                    name: name,
                    text_zh: alarm.text_zh || "",
                    text_en: alarm.text_en || "",
                    color: alarm.color || "#FF4400",
                    font_size: alarm.font_size !== undefined ? alarm.font_size : 28
                })
            } else {
                alarmModel.setProperty(existingIndex, "text_zh", alarm.text_zh || "")
                alarmModel.setProperty(existingIndex, "text_en", alarm.text_en || "")
                alarmModel.setProperty(existingIndex, "color", alarm.color || "#FF4400")
                alarmModel.setProperty(existingIndex, "font_size",
                                       alarm.font_size !== undefined ? alarm.font_size : 28)
                if (existingIndex !== i)
                    alarmModel.move(existingIndex, i, 1)
            }
        }
    }

    Component.onCompleted: syncAlarmsFromList()

    // ─── ListView ──────────────────────────────────────────────────────────────
    Column {
        id: column
        anchors.fill: parent
        spacing: 8

        Repeater {
            model: alarmModel
            delegate: AlarmBannerItem {
                mName: model.name
                mTextZh: model.text_zh
                mTextEn: model.text_en
                mColor: model.color
                mFontSize: model.font_size
                mModel: alarmModel
                mIndex: index
                width: 620
            }
        }
    }

    // 无报警时不可见
    visible: alarmModel.count > 0
    implicitHeight: visible ? (alarmModel.count * 64 + (Math.max(0, alarmModel.count - 1) * 8)) : 0
}
