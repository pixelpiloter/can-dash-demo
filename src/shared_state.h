// shared_state.h
// 进程内共享态：CAN Ingress 写 ctx；Logic tick / SnapshotPump 读。
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace cluster {

struct SharedState {
    std::mutex mtx;
    std::unordered_map<std::string, double> ctx;
    std::unordered_map<std::string, int64_t> signal_update_ms;
    int64_t last_update_ms = 0;
    uint64_t generation = 0;
    uint64_t rx_sequence = 0;
    bool source_online = false;
};

// 发布一组已解包信号（物理值）。now_ms 为 steady_clock 相对进程起点的毫秒。
void publishSignals(SharedState& shared,
                    const std::unordered_map<std::string, double>& values,
                    int64_t now_ms,
                    bool source_online = true);

}  // namespace cluster
