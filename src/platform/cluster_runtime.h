// cluster_runtime.h — 统一运行时：Config → Engine → Pump → Binder + SignalSources
#pragma once

#include "config_loader.h"
#include "idata_binder.h"
#include "isignal_source.h"
#include "logic/logic_engine.h"
#include "shared_state.h"
#include "snapshot_pump.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace platform {

enum class SignalSourceKind {
    None,
    InjectDemo,
    CanSocket,
    Replay,
};

struct ClusterRuntimeOptions {
    std::string framework_path = "config/framework.yaml";
    std::string log_dir;
    SignalSourceKind source = SignalSourceKind::None;
    std::string replay_path;
    bool rx_latency_probe = false;
};

class ClusterRuntime {
public:
    ClusterRuntime();
    ~ClusterRuntime();

    ClusterRuntime(const ClusterRuntime&) = delete;
    ClusterRuntime& operator=(const ClusterRuntime&) = delete;

    // 加载配置与 LogicEngine；创建 SnapshotPump（尚未 start）。
    bool init(const ClusterRuntimeOptions& opt, std::string& err);

    // 将 SnapshotPump 所有权交给调用方（Qt 路径）。
    std::unique_ptr<IDataSource> releaseDataSource();

    void setBinder(IDataBinder* binder);

    // 启动 Pump 与信号源线程。
    bool start(std::string& err);

    void requestStop();
    void join();

    const ConfigBundle& bundle() const { return m_bundle; }
    clk::LogicEngine& engine() { return m_engine; }
    const clk::LogicEngine& engine() const { return m_engine; }
    cluster::SharedState& sharedState() { return m_shared; }
    SnapshotPump* pump() { return m_pump.get(); }

    const char* sourceName() const;

private:
    bool setupSignalSource(const ClusterRuntimeOptions& opt, std::string& err);
    void wirePumpCallbacks();

    ClusterRuntimeOptions m_opt;
    ConfigBundle m_bundle;
    clk::LogicEngine m_engine;
    cluster::SharedState m_shared;
    std::unique_ptr<SnapshotPump> m_pump;
    IDataBinder* m_binder = nullptr;
    std::chrono::steady_clock::time_point m_start{};

    std::vector<std::unique_ptr<ISignalSource>> m_sources;
    std::atomic<bool> m_stop{false};
    bool m_initialized = false;
    bool m_started = false;
    bool m_pump_released = false;
};

}  // namespace platform
