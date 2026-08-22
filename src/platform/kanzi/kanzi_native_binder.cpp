#include "platform/kanzi/kanzi_native_binder.h"

#include "platform/logging/file_logger.h"

#include <cmath>
#include <string>

#if defined(CLUSTER_LOGIC_KANZI_UI)
#include <kanzi/kanzi.hpp>
#endif

namespace cluster {

KanziNativeBinder::KanziNativeBinder(DataStore* store) : m_store(store) {}

void KanziNativeBinder::onHealthChanged(platform::HealthStatus new_health) {
    if (m_store) m_store->setHealth(platform::healthStatusStr(new_health));
}

void KanziNativeBinder::onDataUpdated(const platform::UiSnapshot& snapshot) {
    std::unordered_map<std::string, int32_t> next_int;
    std::unordered_map<std::string, std::string> next_str;

    auto isKanziKey = [](const std::string& k) {
        return k.find('.') != std::string::npos;
    };

    for (const auto& kv : snapshot.display_values) {
        if (!isKanziKey(kv.first)) continue;
        if (kv.second.type == "string") {
            next_str[kv.first] = kv.second.str;
        } else {
            next_int[kv.first] =
                static_cast<int32_t>(std::lround(kv.second.num));
        }
    }

    for (const auto& kv : snapshot.light_state) {
        if (!isKanziKey(kv.first)) continue;
        next_int[kv.first] = (kv.second.on || kv.second.flash) ? 1 : 0;
    }

    if (m_store) {
        for (const auto& kv : next_int) {
            m_store->setNumber(kv.first, static_cast<double>(kv.second));
        }
        m_store->setHealth(platform::healthStatusStr(snapshot.health));
        m_store->setBool("source_connected", snapshot.source_connected);
    }

    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_pending_int = std::move(next_int);
        m_pending_str = std::move(next_str);
        m_dirty = true;
    }
}

void KanziNativeBinder::flushToDataSource(kanzi::Node* data_source_node) {
#if !defined(CLUSTER_LOGIC_KANZI_UI)
    (void)data_source_node;
    return;
#else
    if (!data_source_node) return;

    std::unordered_map<std::string, int32_t> pending_int;
    std::unordered_map<std::string, std::string> pending_str;
    bool dirty = false;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        dirty = m_dirty;
        pending_int = m_pending_int;
        pending_str = m_pending_str;
        m_dirty = false;
    }
    if (!dirty) return;

    using kanzi::DynamicPropertyType;
    for (const auto& kv : pending_int) {
        auto it = m_last_int.find(kv.first);
        if (it != m_last_int.end() && it->second == kv.second) continue;
        m_last_int[kv.first] = kv.second;
        data_source_node->setProperty(DynamicPropertyType<int>(kv.first),
                                      kv.second);
        CLK_LOG_INFO("KanziNative",
                     std::string("set ") + kv.first + "=" +
                         std::to_string(kv.second));
    }
    for (const auto& kv : pending_str) {
        auto it = m_last_str.find(kv.first);
        if (it != m_last_str.end() && it->second == kv.second) continue;
        m_last_str[kv.first] = kv.second;
        data_source_node->setProperty(
            DynamicPropertyType<kanzi::string>(kv.first),
            kanzi::string(kv.second.c_str()));
        CLK_LOG_INFO("KanziNative",
                     std::string("set ") + kv.first + "=\"" + kv.second + "\"");
    }
#endif
}

}  // namespace cluster
