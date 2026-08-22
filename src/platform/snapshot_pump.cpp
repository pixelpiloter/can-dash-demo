#include "snapshot_pump.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace platform {
namespace {

int64_t elapsedMs(SnapshotPump::ClockPoint start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start)
        .count();
}

}  // namespace

SnapshotPump::SnapshotPump(cluster::SharedState* shared,
                           clk::LogicEngine* engine,
                           ClockPoint start_time,
                           int ui_fps_hz,
                           int tick_hz)
    : m_shared(shared)
    , m_engine(engine)
    , m_startTime(start_time) {
    setUiFpsHz(ui_fps_hz);
    if (tick_hz > 0) {
        m_logicIntervalMs.store(
            std::max(1, static_cast<int>(std::lround(1000.0 / tick_hz))));
    }
    m_snapshot.health = HealthStatus::Disconnected;
}

SnapshotPump::~SnapshotPump() {
    stop();
}

void SnapshotPump::setUiFpsHz(int fps_hz) {
    if (fps_hz <= 0) return;
    m_intervalMs.store(std::max(1, static_cast<int>(std::lround(1000.0 / fps_hz))));
}

bool SnapshotPump::loadWarnScheduler(const std::string& warn_yaml_path,
                                     std::string& err) {
    m_warnSchedReady = m_warnSched.loadWarnYaml(warn_yaml_path, err);
    return m_warnSchedReady;
}

void SnapshotPump::applyWarnUi(UiSnapshot& snap) {
    if (!m_warnSchedReady) return;
    const WarnUiOut w = m_warnSched.apply(snap.warn_state);

    auto putNum = [&](const std::string& key, double v) {
        clk::LogicEngine::DisplayEntry e;
        e.type = "float";
        e.num = v;
        snap.display_values[key] = e;
    };
    auto putStr = [&](const std::string& key, const std::string& s) {
        clk::LogicEngine::DisplayEntry e;
        e.type = "string";
        e.str = s;
        snap.display_values[key] = e;
    };

    // 单字段（部分绑定直接读这些）
    putNum("cluster.warningWindow", static_cast<double>(w.warning_window));
    putNum("cluster.warningWindowID",
           static_cast<double>(w.warning_window_id));
    putNum("cluster.warningWindowOK",
           static_cast<double>(w.warning_window_ok));
    putStr("cluster.warningNum_window", w.warning_num_window);

    // 报警窗还写一份聚合串 windowData（warningWindow,warningWindowID,windowNum,windowOK），
    // 便于整体绑定；单项字段供需要逐个读取的绑定使用。
    putStr("cluster.windowData",
           std::to_string(w.warning_window) + "," +
               std::to_string(w.warning_window_id) + "," +
               w.warning_num_window + "," +
               std::to_string(w.warning_window_ok));

    for (const auto& ic : w.icons) {
        putNum(ic.first, static_cast<double>(ic.second));
    }
}

void SnapshotPump::setUpdateCallback(UpdateCallback cb) {
    std::lock_guard<std::mutex> lk(m_cbMtx);
    m_updateCb = std::move(cb);
}

void SnapshotPump::setHealthCallback(HealthCallback cb) {
    std::lock_guard<std::mutex> lk(m_cbMtx);
    m_healthCb = std::move(cb);
}

UiSnapshot SnapshotPump::snapshot() const {
    std::lock_guard<std::mutex> lk(m_snapMtx);
    return m_snapshot;
}

HealthStatus SnapshotPump::health() const {
    std::lock_guard<std::mutex> lk(m_snapMtx);
    return m_snapshot.health;
}

bool SnapshotPump::start() {
    bool expected = false;
    if (!m_running.compare_exchange_strong(expected, true)) return true;
    m_thread = std::thread([this] { threadMain(); });
    return true;
}

void SnapshotPump::stop() {
    if (!m_running.exchange(false)) return;
    if (m_thread.joinable()) m_thread.join();
    {
        std::lock_guard<std::mutex> lk(m_cbMtx);
        m_updateCb = nullptr;
        m_healthCb = nullptr;
    }
    std::lock_guard<std::mutex> lk(m_snapMtx);
    m_snapshot.health = HealthStatus::Disconnected;
    m_lastHealth = HealthStatus::Disconnected;
}

void SnapshotPump::threadMain() {
    while (m_running.load()) {
        sampleOnce();
        std::this_thread::sleep_for(std::chrono::milliseconds(m_intervalMs.load()));
    }
}

void SnapshotPump::sampleOnce() {
    if (!m_shared || !m_engine) return;

    std::unordered_map<std::string, double> ctx;
    std::unordered_map<std::string, int64_t> signal_update_ms;
    int64_t last_upd = 0;
    uint64_t generation = 0;
    uint64_t rx_sequence = 0;
    bool source_online = false;
    {
        std::lock_guard<std::mutex> lk(m_shared->mtx);
        ctx = m_shared->ctx;
        signal_update_ms = m_shared->signal_update_ms;
        last_upd = m_shared->last_update_ms;
        generation = m_shared->generation;
        rx_sequence = m_shared->rx_sequence;
        source_online = m_shared->source_online;
    }

    const int64_t now_ms = elapsedMs(m_startTime);
    const int logic_iv = m_logicIntervalMs.load();
    if (m_lastLogicTickMs < 0 || (now_ms - m_lastLogicTickMs) >= logic_iv) {
        m_engine->tick(ctx, signal_update_ms);
        m_lastLogicTickMs = now_ms;
    }
    const auto logic = m_engine->snapshot();

    UiSnapshot snap;
    snap.can_signals = std::move(ctx);
    snap.display_values = logic.display_values;
    snap.warn_state = logic.warn_state;
    snap.light_state = logic.light_state;
    snap.stale_signals = logic.stale_signals;
    snap.source_connected = source_online;
    applyWarnUi(snap);
    snap.meta.timestamp_ms = static_cast<uint64_t>(std::max<int64_t>(0, last_upd));
    snap.meta.sample_time_ms = static_cast<uint64_t>(std::max<int64_t>(0, now_ms));
    snap.meta.frame_seq = rx_sequence;
    snap.meta.generation = generation + logic.generation;
    snap.meta.data_age_ms = (last_upd > 0) ? (now_ms - last_upd) : -1;

    HealthStatus h = HealthStatus::Disconnected;
    if (!source_online && last_upd <= 0) {
        h = HealthStatus::Disconnected;
    } else if (!source_online) {
        h = HealthStatus::Waiting;
    } else if (snap.meta.data_age_ms > 2000) {
        h = HealthStatus::Stale;
    } else {
        h = HealthStatus::Ok;
    }
    snap.health = h;

    UpdateCallback updateCb;
    HealthCallback healthCb;
    {
        std::lock_guard<std::mutex> lk(m_cbMtx);
        updateCb = m_updateCb;
        healthCb = m_healthCb;
    }

    {
        std::lock_guard<std::mutex> lk(m_snapMtx);
        if (h != m_lastHealth) {
            m_lastHealth = h;
            if (healthCb) healthCb(h);
        }
        m_snapshot = snap;
        ++m_snapshotGeneration;
    }

    if (updateCb) updateCb(snap);
}

}  // namespace platform
