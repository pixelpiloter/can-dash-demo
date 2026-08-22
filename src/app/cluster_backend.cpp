#include "app/cluster_backend.h"

#include "platform/cluster_runtime.h"
#include "platform/data_store.h"
#include "platform/logging/file_logger.h"

#include <chrono>
#include <cstring>
#include <sstream>
#include <thread>

namespace cluster {

ClusterBackend::ClusterBackend() = default;

ClusterBackend::~ClusterBackend() {
    requestStop();
    join();
}

ClusterBackendOptions parseBackendArgs(int argc, char** argv) {
    ClusterBackendOptions opt;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--framework") == 0 && i + 1 < argc) {
            opt.framework = argv[++i];
        } else if (std::strcmp(argv[i], "--log-dir") == 0 && i + 1 < argc) {
            opt.log_dir = argv[++i];
        } else if (std::strcmp(argv[i], "--inject-demo") == 0) {
            opt.inject_demo = true;
        } else if (std::strcmp(argv[i], "--can-socket") == 0) {
            opt.can_socket = true;
        } else if (std::strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
            opt.replay_path = argv[++i];
        }
    }
    return opt;
}

bool ClusterBackend::start(const ClusterBackendOptions& opt,
                           platform::IDataBinder* binder,
                           DataStore* store) {
    if (m_running.load()) return true;
    m_opt = opt;
    m_stop.store(false);
    m_external_store = store;

    platform::ClusterRuntimeOptions ropt;
    ropt.framework_path = opt.framework;
    ropt.log_dir = opt.log_dir;
    if (!opt.replay_path.empty()) {
        ropt.source = platform::SignalSourceKind::Replay;
        ropt.replay_path = opt.replay_path;
    } else if (opt.inject_demo) {
        ropt.source = platform::SignalSourceKind::InjectDemo;
    } else if (opt.can_socket) {
        ropt.source = platform::SignalSourceKind::CanSocket;
    }

    m_runtime = std::make_unique<platform::ClusterRuntime>();
    std::string err;
    if (!m_runtime->init(ropt, err)) {
        CLK_LOG_ERROR("backend", std::string("FATAL: ") + err);
        return false;
    }

    if (binder) m_runtime->setBinder(binder);

    if (!m_runtime->start(err)) {
        CLK_LOG_ERROR("backend", std::string("FATAL start: ") + err);
        return false;
    }

    m_running.store(true);
    if (m_external_store) {
        m_mvp_thread = std::make_unique<std::thread>([this] { mvpLogLoop(); });
    }

    CLK_LOG_INFO("backend",
                 std::string("up kanzi-native source=") +
                     m_runtime->sourceName());
    platform::FileLogger::instance().flush();
    return true;
}

void ClusterBackend::mvpLogLoop() {
    int print_div = 0;
    while (!m_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        DataStore* st = store();
        if (!st) continue;
        if ((++print_div % 4) != 0) continue;
        std::ostringstream oss;
        oss << "health=" << st->health()
            << " speed=" << st->getNumber("cluster.speed")
            << " gear=" << st->getNumber("cluster.gear")
            << " turnL=" << st->getNumber("tt.turn_left")
            << " turnR=" << st->getNumber("tt.turn_right")
            << " hiBeam=" << st->getNumber("tt.hi_beam")
            << " src=" << (st->getBool("source_connected") ? 1 : 0)
            << " mode=kanzi-native";
        CLK_LOG_INFO("mvp", oss.str());
        platform::FileLogger::instance().flush();
    }
}

void ClusterBackend::requestStop() {
    m_stop.store(true);
    if (m_runtime) m_runtime->requestStop();
}

void ClusterBackend::join() {
    if (m_mvp_thread && m_mvp_thread->joinable()) m_mvp_thread->join();
    m_mvp_thread.reset();
    if (m_runtime) {
        m_runtime->join();
        m_runtime.reset();
    }
    m_running.store(false);
    CLK_LOG_INFO("backend", "shutdown");
    platform::FileLogger::instance().flush();
}

}  // namespace cluster
