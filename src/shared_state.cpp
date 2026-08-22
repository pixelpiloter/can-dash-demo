#include "shared_state.h"

namespace cluster {

void publishSignals(SharedState& shared,
                    const std::unordered_map<std::string, double>& values,
                    int64_t now_ms,
                    bool source_online) {
    if (values.empty() && !source_online) {
        std::lock_guard<std::mutex> lk(shared.mtx);
        shared.source_online = false;
        return;
    }

    std::lock_guard<std::mutex> lk(shared.mtx);
    bool changed = false;
    for (const auto& kv : values) {
        auto it = shared.ctx.find(kv.first);
        if (it == shared.ctx.end() || it->second != kv.second) {
            shared.ctx[kv.first] = kv.second;
            changed = true;
        }
        shared.signal_update_ms[kv.first] = now_ms;
    }
    shared.last_update_ms = now_ms;
    shared.source_online = source_online;
    ++shared.rx_sequence;
    if (changed) {
        ++shared.generation;
    }
}

}  // namespace cluster
