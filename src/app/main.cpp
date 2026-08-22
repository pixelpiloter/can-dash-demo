// main.cpp — cluster-logic 控制台/MVP 宿主（无 UI）

#include "app/runtime_options.h"
#include "platform/cluster_runtime.h"
#include "platform/data_store.h"
#include "platform/logging/file_logger.h"
#include "platform/store_binder.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <memory>
#include <sstream>
#include <thread>

namespace {

std::atomic<bool> g_stop{false};

void onSignal(int) {
    g_stop.store(true);
}

const char* kHelp =
    "usage: cluster-logic [options]\n"
    "  --framework PATH        framework.yaml (default: config/framework.yaml)\n"
    "  --log-dir PATH          file logger output directory\n"
    "  --can-socket            Unix socket 接 CAN 帧\n"
    "  --inject-demo           内置 DBC 字段演示\n"
    "  --replay PATH           离线信号时间线 YAML\n"
    "  --help\n";

}  // namespace

int main(int argc, char** argv) {
    const auto cli = cluster::parseRuntimeCli(argc, argv, kHelp);
    if (cli.show_help) return 0;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    platform::ClusterRuntime runtime;
    std::string err;
    if (!runtime.init(cli.runtime, err)) {
        CLK_LOG_ERROR("main", std::string("FATAL: ") + err);
        platform::FileLogger::instance().flush();
        return 1;
    }

    cluster::DataStore store;
    platform::StoreBinder binder(&store);
    runtime.setBinder(&binder);

    if (!runtime.start(err)) {
        CLK_LOG_ERROR("main", std::string("FATAL start: ") + err);
        platform::FileLogger::instance().flush();
        return 1;
    }

    cluster::printRuntimeStartupLog("main", runtime, cli.runtime);

    int print_div = 0;
    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if ((++print_div % 4) == 0) {
            std::ostringstream oss;
            oss << "health=" << store.health()
                << " speed=" << store.getNumber("display.cluster.speed")
                << " gear=" << store.getNumber("display.cluster.gear")
                << " turnL=" << (store.getBool("light.tt.turn_left.on") ? 1 : 0)
                << " turnR=" << (store.getBool("light.tt.turn_right.on") ? 1 : 0)
                << " src=" << (store.getBool("source_connected") ? 1 : 0)
                << " mode=standalone";
            CLK_LOG_INFO("mvp", oss.str());
            platform::FileLogger::instance().flush();
        }
    }

    runtime.requestStop();
    runtime.join();
    CLK_LOG_INFO("main", "shutdown");
    platform::FileLogger::instance().flush();
    return 0;
}
