// AlarmBannerItem.qml — AlarmBanner 专用、带自删除生命周期的单条代理
//
// 输入契约：父容器传入稳定 name、双语文案、颜色、字号，以及本地 ListModel/索引；
// 文案固定显示中文并以英文兜底。
// 动画时序固定为 200ms 入场、3000ms 停留、300ms 退出；退出时再次核对
// name 后按索引删除，避免同步/重排后误删。本组件不读取 warn/light/raw 数据。
// Timer 是单次活动项生命周期 Timer；没有可见性停机或超时占位概念。
import QtQuick 2.15

Rectangle {
    id: root

    property string mName: ""
    property string mTextZh: ""
    property string mTextEn: ""
    property string mColor: "#FF4400"
    property int mFontSize: 28
    property ListModel mModel: null
    property int mIndex: -1

    width: 620
    height: 56
    radius: 8
    color: "#DD000000"
    border.color: mColor
    border.width: 2

    // 初始状态：隐藏，等待进入动画
    y: -64
    opacity: 0

    // ─── 报警文字（根据语言自动切换）────────────────────────────────────────
    Row {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 12
        anchors.verticalCenter: parent.verticalCenter

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "⚠"
            color: mColor
            font.pixelSize: mFontSize + 4
            font.weight: Font.Bold
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: (mTextZh || mTextEn)
            color: mColor
            font.pixelSize: mFontSize
            font.weight: Font.Bold
            font.family: "sans-serif"
            verticalAlignment: Text.AlignVCenter
        }
    }

    // ─── 动画：进入(200ms) → 停留(3s) → 退出(300ms) → 从模型移除 ───────────

    // 阶段1：进入动画 - 滑动 + 淡入 (200ms ease-out)
    SequentialAnimation {
        id: enterSeq
        running: true
        ParallelAnimation {
            NumberAnimation {
                target: root
                property: "y"
                from: -64; to: 0
                duration: 200
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: root
                property: "opacity"
                from: 0; to: 1
                duration: 200
                easing.type: Easing.OutCubic
            }
        }
        onStopped: {
            // 进入动画结束 → 启动3秒计时器
            holdTimer.start()
        }
    }

    // 阶段2：停留3秒
    Timer {
        id: holdTimer
        interval: 3000
        repeat: false
        onTriggered: exitSeq.start()
    }

    // 阶段3：退出动画 - 淡出 (300ms)
    SequentialAnimation {
        id: exitSeq
        NumberAnimation {
            target: root
            property: "opacity"
            from: 1; to: 0
            duration: 300
            easing.type: Easing.InCubic
        }
        onStopped: {
            // 退出动画结束 → 从模型移除
            if (mModel && mIndex >= 0 && mIndex < mModel.count) {
                // 再次检查稳定 id，防止同步/重排后误删其他条目。
                if (mModel.get(mIndex) && mModel.get(mIndex).name === mName) {
                    mModel.remove(mIndex)
                }
            }
        }
    }
}
