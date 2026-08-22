// warn_scheduler.cpp
#include "platform/warn_scheduler.h"

#include <algorithm>
#include <yaml-cpp/yaml.h>

namespace platform {
namespace {

uint32_t parseFuncId(const YAML::Node& n) {
    if (!n) return 0;
    if (n.IsScalar()) {
        const std::string s = n.as<std::string>();
        if (s.size() > 2 && (s[0] == '0') && (s[1] == 'x' || s[1] == 'X')) {
            return static_cast<uint32_t>(std::stoul(s, nullptr, 16));
        }
        return static_cast<uint32_t>(n.as<unsigned long long>());
    }
    return 0;
}

}  // namespace

bool WarnScheduler::loadWarnYaml(const std::string& path, std::string& error_out) {
    try {
        YAML::Node root = YAML::LoadFile(path);
        if (!root["warns"] || !root["warns"].IsSequence()) {
            error_out = path + ": missing warns[]";
            return false;
        }
        m_entries.clear();
        m_by_name.clear();
        m_display_queue.clear();
        m_current = -1;

        for (const auto& node : root["warns"]) {
            if (!node["name"]) continue;
            WarnEntry e;
            e.name = node["name"].as<std::string>();
            if (node["feature_id"]) e.feature_id = parseFuncId(node["feature_id"]);
            if (node["window_id"]) e.window_id = node["window_id"].as<int>();
            if (node["proi"]) e.proi = node["proi"].as<int>();
            if (node["type"]) e.type = node["type"].as<int>();
            if (node["press_ok"]) e.press_ok = node["press_ok"].as<int>();
            if (node["work_mode"]) e.work_mode = node["work_mode"].as<int>();
            if (node["icons"] && node["icons"].IsSequence()) {
                for (const auto& ic : node["icons"]) {
                    e.icons.push_back(ic.as<std::string>());
                }
            }
            if (m_by_name.count(e.name)) {
                error_out = path + ": duplicate warn name " + e.name;
                return false;
            }
            m_by_name[e.name] = static_cast<int>(m_entries.size());
            m_entries.push_back(std::move(e));
        }
        return true;
    } catch (const std::exception& ex) {
        error_out = path + ": " + ex.what();
        return false;
    }
}

const WarnEntry* WarnScheduler::findByName(const std::string& name) const {
    auto it = m_by_name.find(name);
    if (it == m_by_name.end()) return nullptr;
    return &m_entries[it->second];
}

int WarnScheduler::pickHigher(int a, int b) const {
    // 优先级比较：type 相同时取 proi 更小者
    if (a < 0) return b;
    if (b < 0) return a;
    const WarnEntry& la = m_entries[a];
    const WarnEntry& lb = m_entries[b];
    if (la.type == lb.type) {
        if (lb.proi < la.proi) return b;
        return a;
    }
    // type 3/C、5/osd 可打断
    if (la.type == 3 || la.type == 5) return a;
    if (lb.type == 3 || lb.type == 5) return b;
    // 否则 A(1) > B(2) > D(4)
    if (la.type == 1) return a;
    if (lb.type == 1) return b;
    if (la.type == 2) return a;
    if (lb.type == 2) return b;
    if (la.type == 4) return a;
    return b;
}

int WarnScheduler::findHighest(const std::vector<int>& queue) const {
    int best = -1;
    for (int idx : queue) {
        if (idx < 0 || idx >= static_cast<int>(m_entries.size())) continue;
        const WarnEntry& e = m_entries[idx];
        // work_mode=1 仅 D1；当前若视为 D1(m_work_mode==1) 则 work_mode==2 的也可显示
        // 简化：当前 mode 为 1 时只显示 work_mode==1 或 ==2；mode 为 2 时只显示 work_mode==2
        if (m_work_mode == 1) {
            if (e.work_mode != 1 && e.work_mode != 2) continue;
        } else {
            if (e.work_mode != 2) continue;
        }
        best = pickHigher(best, idx);
    }
    return best;
}

WarnUiOut WarnScheduler::apply(
    const std::unordered_map<std::string, bool>& warn_state) {
    // 1) 同步 display 队列：active → 入队；inactive → 出队
    std::vector<char> active(m_entries.size(), 0);
    for (const auto& kv : warn_state) {
        auto it = m_by_name.find(kv.first);
        if (it == m_by_name.end()) continue;
        if (kv.second) active[static_cast<size_t>(it->second)] = 1;
    }

    m_display_queue.erase(
        std::remove_if(m_display_queue.begin(), m_display_queue.end(),
                       [&](int idx) {
                           return idx < 0 ||
                                  idx >= static_cast<int>(m_entries.size()) ||
                                  !active[static_cast<size_t>(idx)];
                       }),
        m_display_queue.end());

    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        if (!active[static_cast<size_t>(i)]) continue;
        // type 3/5 为可打断类型，一并进 display，靠优先级抢占
        if (std::find(m_display_queue.begin(), m_display_queue.end(), i) ==
            m_display_queue.end()) {
            m_display_queue.push_back(i);
        }
    }

    // 2) 选当前最高优先
    const int next = findHighest(m_display_queue);
    m_current = next;

    WarnUiOut out;
    // 先清所有声明过的 icons
    for (const auto& e : m_entries) {
        for (const auto& ic : e.icons) out.icons[ic] = 0;
    }
    // active 的 icons 置 1（与窗口抢占无关，对齐「报警条件成立」）
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        if (!active[static_cast<size_t>(i)]) continue;
        for (const auto& ic : m_entries[static_cast<size_t>(i)].icons) {
            out.icons[ic] = 1;
        }
    }

    if (m_current < 0) {
        out.warning_window = 0;
        out.warning_window_id = 0;
        out.warning_window_ok = 0;
        out.warning_num_window = "---";
        return out;
    }

    const WarnEntry& cur = m_entries[static_cast<size_t>(m_current)];
    out.active_name = cur.name;
    out.warning_window = 1;
    out.warning_window_id = cur.window_id;
    out.warning_window_ok =
        (cur.press_ok == 1 && m_work_mode == 2) ? 1 : 0;
    out.warning_num_window = "---";
    return out;
}

}  // namespace platform
