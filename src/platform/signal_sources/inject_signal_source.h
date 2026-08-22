#pragma once

#include "platform/isignal_source.h"

#include <memory>
#include <thread>

namespace platform {

// 内置演示：周期性发布合成 DBC 字段，无需外部 CAN。
class InjectSignalSource : public ISignalSource {
public:
    void start(cluster::SharedState& shared,
               std::chrono::steady_clock::time_point start,
               std::atomic<bool>& stop) override;
    void requestStop() override;
    void join() override;
    const char* name() const override { return "inject"; }

private:
    cluster::SharedState* m_shared = nullptr;
    std::chrono::steady_clock::time_point m_start{};
    std::atomic<bool>* m_stop = nullptr;
    std::unique_ptr<std::thread> m_thread;
};

}  // namespace platform
