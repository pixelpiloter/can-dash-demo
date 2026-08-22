// ui_snapshot.h — 跨层只读快照（无 Qt）
#pragma once

#include "logic/logic_engine.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace platform {

enum class HealthStatus {
    Disconnected = 0,
    Waiting,
    Stale,
    Ok
};

inline const char* healthStatusStr(HealthStatus h) {
    switch (h) {
        case HealthStatus::Ok:           return "ok";
        case HealthStatus::Waiting:      return "waiting";
        case HealthStatus::Stale:        return "stale";
        case HealthStatus::Disconnected: return "disconnected";
    }
    return "unknown";
}

struct FrameMeta {
    uint64_t timestamp_ms = 0;
    uint64_t sample_time_ms = 0;
    uint64_t frame_seq = 0;
    uint64_t generation = 0;
    int64_t  data_age_ms = 0;
    uint64_t dropped_frames = 0;
};

struct UiSnapshot {
    std::unordered_map<std::string, double> can_signals;
    std::unordered_map<std::string, clk::LogicEngine::DisplayEntry> display_values;
    std::unordered_map<std::string, bool> warn_state;
    std::unordered_map<std::string, clk::LogicEngine::LightState> light_state;
    std::unordered_set<std::string> stale_signals;
    FrameMeta    meta;
    HealthStatus health = HealthStatus::Disconnected;
    bool         source_connected = false;
};

}  // namespace platform
