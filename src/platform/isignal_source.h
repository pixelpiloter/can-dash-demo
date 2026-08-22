// isignal_source.h — 信号源抽象：向 SharedState 发布已解包物理量
#pragma once

#include "config_loader.h"

#include "shared_state.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <string>

namespace platform {

// 每次发布一批信号前可选回调（Qt 延迟探测等）。
using SignalRxHook = std::function<void()>;

class ISignalSource {
public:
    virtual ~ISignalSource() = default;

    // 启动前准备（如加载 DecodeTable）。默认无操作。
    virtual bool prepare(const FrameworkConfig& fw, std::string& err) {
        (void)fw;
        (void)err;
        return true;
    }

    virtual void setRxHook(SignalRxHook hook) { m_rxHook = std::move(hook); }

    virtual void start(cluster::SharedState& shared,
                       std::chrono::steady_clock::time_point start,
                       std::atomic<bool>& stop) = 0;
    virtual void requestStop() = 0;
    virtual void join() = 0;
    virtual const char* name() const = 0;

protected:
    void notifyRx() const {
        if (m_rxHook) m_rxHook();
    }

private:
    SignalRxHook m_rxHook;
};

}  // namespace platform
