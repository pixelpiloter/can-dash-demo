// warn_scheduler.h — 报警窗仲裁：多条告警同时成立时，按类型/优先级选出唯一上屏项
//
// logic 的 setwarnon/off(name) 驱动 → 报警队列仲裁 → 计算上屏内容
//
// 上屏属性：
//   cluster.warningWindow / warningWindowID / warningWindowOK / warningNum_window
//   另：warn.yaml icons[] 在该报警 active 时置 1（如 overspeed）
#pragma once

#include "logic/logic_engine.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace platform {

struct WarnEntry {
    std::string name;
    uint32_t feature_id = 0;   // 告警功能码（集成方 HMI 告警表提供）
    int32_t window_id = 0;   // 上屏告警窗口 id
    int32_t proi = 100;     // 优先级，越小越高
    int32_t type = 1;       // 1=A 2=B 3=C 4=D 5=osd
    int32_t press_ok = 0;
    int32_t work_mode = 1;  // 1=D1 2=D1/D2
    std::vector<std::string> icons;  // active 时写 1 的 Kanzi int 属性
};

struct WarnUiOut {
    int32_t warning_window = 0;
    int32_t warning_window_id = 0;
    int32_t warning_window_ok = 0;
    std::string warning_num_window = "---";
    std::string active_name;  // 当前上屏 warn name，空=无
    std::unordered_map<std::string, int32_t> icons;  // 全表 icons 清 0 后对 active 置 1
};

class WarnScheduler {
public:
    bool loadWarnYaml(const std::string& path, std::string& error_out);

    // work_mode: 1=D1, 2=D2（默认 D1，与超速 work_mode=1 一致）
    void setWorkMode(int mode) { m_work_mode = mode; }

    // 每帧：用 LogicEngine warn_state 同步队列并算出上屏
    WarnUiOut apply(const std::unordered_map<std::string, bool>& warn_state);

    const WarnEntry* findByName(const std::string& name) const;

private:
    int pickHigher(int a, int b) const;  // 返回更高优先条目下标，-1 无效
    int findHighest(const std::vector<int>& queue) const;

    std::vector<WarnEntry> m_entries;
    std::unordered_map<std::string, int> m_by_name;
    std::vector<int> m_display_queue;  // entry indices
    int m_current = -1;
    int m_work_mode = 1;
};

}  // namespace platform
