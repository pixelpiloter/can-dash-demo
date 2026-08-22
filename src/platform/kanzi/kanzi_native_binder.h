// kanzi_native_binder.h
//
// 同进程 Kanzi 上屏出口（CLUSTER_LOGIC_KANZI_UI）。
// display_values / light_state → #DataSource（int + string）。
#pragma once

#include "idata_binder.h"
#include "../data_store.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace kanzi {
class Node;
}

namespace cluster {

class KanziNativeBinder : public platform::IDataBinder {
public:
    explicit KanziNativeBinder(DataStore* store);

    void onDataUpdated(const platform::UiSnapshot& snapshot) override;
    void onHealthChanged(platform::HealthStatus new_health) override;

    void flushToDataSource(kanzi::Node* data_source_node);

private:
    DataStore* m_store = nullptr;
    mutable std::mutex m_mtx;
    std::unordered_map<std::string, int32_t> m_pending_int;
    std::unordered_map<std::string, std::string> m_pending_str;
    std::unordered_map<std::string, int32_t> m_last_int;
    std::unordered_map<std::string, std::string> m_last_str;
    bool m_dirty = false;
};

}  // namespace cluster
