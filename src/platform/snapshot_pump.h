// snapshot_pump.h
// 无 Qt：独立线程按 ui_fps_hz 采样 SharedState + LogicEngine → UiSnapshot
#pragma once

#include "idata_source.h"
#include "logic/logic_engine.h"
#include "shared_state.h"
#include "warn_scheduler.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace platform {

class SnapshotPump : public IDataSource {
public:
    using ClockPoint = std::chrono::steady_clock::time_point;

    SnapshotPump(cluster::SharedState* shared,
                 clk::LogicEngine* engine,
                 ClockPoint start_time,
                 int ui_fps_hz = 60,
                 int tick_hz = 1);
    ~SnapshotPump() override;

    bool start() override;
    void stop() override;
    bool isRunning() const override { return m_running.load(); }
    UiSnapshot snapshot() const override;
    HealthStatus health() const override;
    void setUpdateCallback(UpdateCallback cb) override;
    void setHealthCallback(HealthCallback cb) override;
    void setUiFpsHz(int fps_hz);

    // 加载报警表（warn.yaml）；失败返回 false
    bool loadWarnScheduler(const std::string& warn_yaml_path, std::string& err);

private:
    void threadMain();
    void sampleOnce();
    void applyWarnUi(UiSnapshot& snap);

    cluster::SharedState* m_shared = nullptr;
    clk::LogicEngine* m_engine = nullptr;
    WarnScheduler m_warnSched;
    bool m_warnSchedReady = false;
    ClockPoint m_startTime{};
    std::atomic<int> m_intervalMs{16};
    std::atomic<int> m_logicIntervalMs{1000};
    std::atomic<bool> m_running{false};
    std::thread m_thread;
    int64_t m_lastLogicTickMs = -1;

    mutable std::mutex m_cbMtx;
    UpdateCallback m_updateCb;
    HealthCallback m_healthCb;

    mutable std::mutex m_snapMtx;
    UiSnapshot m_snapshot;
    HealthStatus m_lastHealth = HealthStatus::Disconnected;
    uint64_t m_snapshotGeneration = 0;
};

}  // namespace platform
