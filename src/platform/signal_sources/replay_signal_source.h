#pragma once

#include "platform/isignal_source.h"

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace platform {

struct ReplayStep {
    int64_t at_ms = 0;
    std::unordered_map<std::string, double> signals;
};

// 从 YAML 时间线离线回放信号（无需 CAN 硬件）。
// 格式见 tests/fixtures/replay_basic.yaml
class ReplaySignalSource : public ISignalSource {
public:
    explicit ReplaySignalSource(std::string replay_yaml_path);

    bool prepare(const FrameworkConfig& fw, std::string& err) override;

    void start(cluster::SharedState& shared,
               std::chrono::steady_clock::time_point start,
               std::atomic<bool>& stop) override;
    void requestStop() override;
    void join() override;
    const char* name() const override { return "replay"; }

    const std::vector<ReplayStep>& steps() const { return m_steps; }
    bool loop() const { return m_loop; }

    static bool loadYaml(const std::string& path,
                         std::vector<ReplayStep>& steps_out,
                         bool& loop_out,
                         std::string& err);

private:
    std::string m_path;
    std::vector<ReplayStep> m_steps;
    bool m_loop = false;
    cluster::SharedState* m_shared = nullptr;
    std::chrono::steady_clock::time_point m_start{};
    std::atomic<bool>* m_stop = nullptr;
    std::unique_ptr<std::thread> m_thread;
};

}  // namespace platform
