#include "platform/cluster_runtime.h"

#include "platform/latency_probe.h"
#include "platform/logging/file_logger.h"
#include "platform/signal_sources/inject_signal_source.h"
#include "platform/signal_sources/replay_signal_source.h"
#include "platform/signal_sources/socket_signal_source.h"

#include <sstream>

namespace platform {

ClusterRuntime::ClusterRuntime() = default;

ClusterRuntime::~ClusterRuntime() {
    requestStop();
    join();
}

bool ClusterRuntime::setupSignalSource(const ClusterRuntimeOptions& opt,
                                       std::string& err) {
    m_sources.clear();
    switch (opt.source) {
        case SignalSourceKind::None:
            return true;
        case SignalSourceKind::InjectDemo:
            m_sources.push_back(std::make_unique<InjectSignalSource>());
            break;
        case SignalSourceKind::CanSocket:
            m_sources.push_back(std::make_unique<SocketSignalSource>());
            break;
        case SignalSourceKind::Replay:
            if (opt.replay_path.empty()) {
                err = "replay path required";
                return false;
            }
            m_sources.push_back(
                std::make_unique<ReplaySignalSource>(opt.replay_path));
            break;
    }
    for (auto& src : m_sources) {
        if (!src->prepare(m_bundle.framework, err)) return false;
        if (opt.rx_latency_probe) {
            src->setRxHook([]() {
                LatencyProbe::instance().onCanRx(monoUs());
            });
        }
    }
    return true;
}

void ClusterRuntime::wirePumpCallbacks() {
    if (!m_pump || m_pump_released || !m_binder) return;
    IDataBinder* binder = m_binder;
    m_pump->setUpdateCallback([binder](const UiSnapshot& snap) {
        binder->onDataUpdated(snap);
    });
    m_pump->setHealthCallback([binder](HealthStatus h) {
        binder->onHealthChanged(h);
    });
}

bool ClusterRuntime::init(const ClusterRuntimeOptions& opt, std::string& err) {
    m_opt = opt;
    m_stop.store(false);
    m_pump_released = false;

    if (!opt.log_dir.empty()) {
        FileLogger::instance().setLogDir(opt.log_dir);
    }

    if (!ConfigLoader::load(opt.framework_path, m_bundle, err)) {
        return false;
    }

    if (!m_engine.loadConfigs(m_bundle.framework.path_can_ids,
                              m_bundle.framework.path_logic) ||
        !m_engine.loadWarnYaml(m_bundle.framework.path_warn) ||
        !m_engine.loadLightYaml(m_bundle.framework.path_lights)) {
        err = "LogicEngine config load failed";
        return false;
    }
    m_engine.setVerbose(false);

    m_start = std::chrono::steady_clock::now();
    m_engine.setClock([start = m_start]() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start)
            .count();
    });

    if (!setupSignalSource(opt, err)) {
        return false;
    }

    m_pump = std::make_unique<SnapshotPump>(
        &m_shared, &m_engine, m_start, m_bundle.framework.ui_fps_hz,
        m_bundle.framework.tick_hz);
    if (!m_pump->loadWarnScheduler(m_bundle.framework.path_warn, err)) {
        return false;
    }

    wirePumpCallbacks();
    m_initialized = true;
    return true;
}

std::unique_ptr<IDataSource> ClusterRuntime::releaseDataSource() {
    if (!m_pump || m_pump_released) return nullptr;
    m_pump_released = true;
    return std::move(m_pump);
}

void ClusterRuntime::setBinder(IDataBinder* binder) {
    m_binder = binder;
    wirePumpCallbacks();
}

bool ClusterRuntime::start(std::string& err) {
    (void)err;
    if (!m_initialized || m_started) return m_initialized;

    if (!m_pump_released && m_pump) {
        m_pump->start();
    }

    for (auto& src : m_sources) {
        src->start(m_shared, m_start, m_stop);
    }

    m_started = true;
    return true;
}

void ClusterRuntime::requestStop() {
    m_stop.store(true);
    for (auto& src : m_sources) {
        src->requestStop();
    }
    if (m_pump && !m_pump_released) {
        m_pump->stop();
    }
}

void ClusterRuntime::join() {
    for (auto& src : m_sources) {
        src->join();
    }
    m_sources.clear();
    if (m_pump && !m_pump_released) {
        m_pump->stop();
    }
    m_started = false;
}

const char* ClusterRuntime::sourceName() const {
    if (m_sources.empty()) return "none";
    return m_sources.front()->name();
}

}  // namespace platform
