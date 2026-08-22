#include "store_binder.h"

#include <climits>

namespace platform {

StoreBinder::StoreBinder(cluster::DataStore* store) : m_store(store) {}

void StoreBinder::onHealthChanged(HealthStatus new_health) {
    if (!m_store) return;
    m_store->setHealth(healthStatusStr(new_health));
}

void StoreBinder::onDataUpdated(const UiSnapshot& snapshot) {
    if (!m_store) return;

    // generation 门控：内容未变则只刷新健康/age 类元数据
    const bool content_changed = (snapshot.meta.generation != m_lastGeneration);
    m_lastGeneration = snapshot.meta.generation;

    m_store->setHealth(healthStatusStr(snapshot.health));
    m_store->setNumber("meta.data_age_ms",
                       static_cast<double>(snapshot.meta.data_age_ms));
    m_store->setNumber("meta.frame_seq",
                       static_cast<double>(snapshot.meta.frame_seq));
    m_store->setBool("source_connected", snapshot.source_connected);

    if (!content_changed) return;

    // 原始信号
    for (const auto& kv : snapshot.can_signals) {
        m_store->setNumber(std::string("signal.") + kv.first, kv.second);
    }

    // Logic display
    for (const auto& kv : snapshot.display_values) {
        const std::string key = std::string("display.") + kv.first;
        if (kv.second.type == "string") {
            m_store->setString(key, kv.second.str);
        } else {
            m_store->setNumber(key, kv.second.num);
        }
    }

    // 指示灯
    for (const auto& kv : snapshot.light_state) {
        m_store->setBool(std::string("light.") + kv.first + ".on", kv.second.on);
        m_store->setBool(std::string("light.") + kv.first + ".flash",
                         kv.second.flash);
    }

    // 报警列表
    std::vector<cluster::WarnItem> warns;
    warns.reserve(snapshot.warn_state.size());
    for (const auto& kv : snapshot.warn_state) {
        warns.push_back(cluster::WarnItem{kv.first, kv.second});
        m_store->setBool(std::string("warn.") + kv.first, kv.second);
    }
    m_store->setWarnList(std::move(warns));

    // MVP 便捷键（Kanzi 页面可直接绑）
    auto speed_it = snapshot.can_signals.find("vehicle_speed");
    if (speed_it != snapshot.can_signals.end()) {
        m_store->setNumber("mvp.speed_kmh", speed_it->second);
    }
    auto disp_speed = snapshot.display_values.find("vehicle_speed_disp");
    if (disp_speed != snapshot.display_values.end()) {
        if (disp_speed->second.type == "string") {
            m_store->setString("mvp.speed_text", disp_speed->second.str);
        } else {
            m_store->setNumber("mvp.speed_kmh", disp_speed->second.num);
        }
    }
}

}  // namespace platform
