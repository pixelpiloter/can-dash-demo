#pragma once

#include "platform/isignal_source.h"

#include <memory>
#include <string>
#include <thread>

namespace platform {

// Unix socket CAN 帧接收 → DecodeTable → publishSignals。
class SocketSignalSource : public ISignalSource {
public:
    bool prepare(const FrameworkConfig& fw, std::string& err) override;

    void start(cluster::SharedState& shared,
               std::chrono::steady_clock::time_point start,
               std::atomic<bool>& stop) override;
    void requestStop() override;
    void join() override;
    const char* name() const override { return "can_socket"; }

private:
    std::string m_socket_path;
    cluster::SharedState* m_shared = nullptr;
    std::chrono::steady_clock::time_point m_start{};
    std::atomic<bool>* m_stop = nullptr;
    std::unique_ptr<std::thread> m_thread;
};

}  // namespace platform
