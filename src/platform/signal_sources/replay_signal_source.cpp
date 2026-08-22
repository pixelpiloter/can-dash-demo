#include "platform/signal_sources/replay_signal_source.h"

#include "shared_state.h"

#include <yaml-cpp/yaml.h>

#include <chrono>
#include <thread>

namespace platform {

ReplaySignalSource::ReplaySignalSource(std::string replay_yaml_path)
    : m_path(std::move(replay_yaml_path)) {}

bool ReplaySignalSource::loadYaml(const std::string& path,
                                  std::vector<ReplayStep>& steps_out,
                                  bool& loop_out,
                                  std::string& err) {
    try {
        YAML::Node root = YAML::LoadFile(path);
        loop_out = root["loop"] && root["loop"].as<bool>();
        if (!root["steps"] || !root["steps"].IsSequence()) {
            err = path + ": missing steps[]";
            return false;
        }
        steps_out.clear();
        for (const auto& step : root["steps"]) {
            if (!step["at_ms"] || !step["signals"] || !step["signals"].IsMap()) {
                err = path + ": step requires at_ms and signals map";
                return false;
            }
            ReplayStep rs;
            rs.at_ms = step["at_ms"].as<int64_t>();
            for (const auto& kv : step["signals"]) {
                rs.signals[kv.first.as<std::string>()] = kv.second.as<double>();
            }
            steps_out.push_back(std::move(rs));
        }
        if (steps_out.empty()) {
            err = path + ": empty steps[]";
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        err = path + ": " + ex.what();
        return false;
    }
}

bool ReplaySignalSource::prepare(const FrameworkConfig& fw, std::string& err) {
    (void)fw;
    return loadYaml(m_path, m_steps, m_loop, err);
}

void ReplaySignalSource::start(cluster::SharedState& shared,
                               std::chrono::steady_clock::time_point start,
                               std::atomic<bool>& stop) {
    m_shared = &shared;
    m_start = start;
    m_stop = &stop;
    const auto steps = m_steps;
    const bool loop = m_loop;
    m_thread = std::make_unique<std::thread>([this, steps, loop]() {
        do {
            for (const auto& step : steps) {
                while (!m_stop->load()) {
                    const auto now_ms = std::chrono::duration_cast<
                                            std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now() -
                                            m_start)
                                            .count();
                    if (now_ms >= step.at_ms) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                if (m_stop->load()) return;
                notifyRx();
                const auto now_ms = std::chrono::duration_cast<
                                        std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() -
                                        m_start)
                                        .count();
                cluster::publishSignals(*m_shared, step.signals, now_ms, true);
            }
        } while (loop && !m_stop->load());
    });
}

void ReplaySignalSource::requestStop() {
    if (m_stop) m_stop->store(true);
}

void ReplaySignalSource::join() {
    if (m_thread && m_thread->joinable()) m_thread->join();
    m_thread.reset();
}

}  // namespace platform
