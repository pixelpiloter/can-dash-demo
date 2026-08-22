// frame_rate.h
// 层: platform
// 职责: UI 帧率编译期默认值。framework.yaml 的有效 ui_fps_hz 会在启动组装时分别
// 覆盖 SnapshotPump 与 QML Timer；缺省配置才落到这里。毫秒周期取最接近整数
// （等同 Math.round(1000/fps)）且下限 1ms，实际频率是近似值而非硬实时保证。
#pragma once

namespace platform {

constexpr int UI_FPS_HZ  = 60;
constexpr int UI_TICK_MS =
    (1000 + UI_FPS_HZ / 2) / UI_FPS_HZ;  // round(1000/60) = 17ms

}  // namespace platform
