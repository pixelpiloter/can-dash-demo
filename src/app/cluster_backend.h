// cluster_backend.h — Kanzi 宿主用的薄封装（内部走 ClusterRuntime）
#pragma once

#include "platform/idata_binder.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace cluster {

class DataStore;

struct ClusterBackendOptions {
    std::string framework = "config/framework.yaml";
    std::string log_dir;
    bool inject_demo = false;
    bool can_socket = false;
    std::string replay_path;
};

class ClusterBackend {
public:
    ClusterBackend();
    ~ClusterBackend();

    bool start(const ClusterBackendOptions& opt, platform::IDataBinder* binder,
               DataStore* store);
    void requestStop();
    void join();

    DataStore* store() { return m_external_store; }

private:
    void mvpLogLoop();

    ClusterBackendOptions m_opt;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stop{false};

    std::unique_ptr<platform::ClusterRuntime> m_runtime;
    DataStore* m_external_store = nullptr;
    std::unique_ptr<std::thread> m_mvp_thread;
};

ClusterBackendOptions parseBackendArgs(int argc, char** argv);

}  // namespace cluster
