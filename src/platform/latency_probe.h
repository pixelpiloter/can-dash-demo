// latency_probe.h
// 端到端延迟探针: CAN 帧到达 → QtBinder 推送到 QML
// 调用点:
//   - CAN 接收线程: onCanRx
//   - QtBinder::onDataUpdated: onQmlPush
//   - 采样主线程 tick: maybeLogP95（运行中偶发 p95）
//   - 退出时: printReport
#pragma once

#include "frame_rate.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <vector>

namespace platform {

inline int64_t monoUs() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::microseconds>(
        clock::now().time_since_epoch()).count();
}

class LatencyProbe {
public:
    static constexpr int kMaxSamples = 20000;

    struct Stats {
        bool valid = false;
        int count = 0;
        int64_t min_us = 0;
        double avg_us = 0.0;
        int64_t p50_us = 0;
        int64_t p95_us = 0;
        int64_t p99_us = 0;
        int64_t max_us = 0;
    };

    // 允许创建隔离实例，便于测试/诊断且不污染进程级 singleton。
    LatencyProbe() = default;
    LatencyProbe(const LatencyProbe&) = delete;
    LatencyProbe& operator=(const LatencyProbe&) = delete;

    static LatencyProbe& instance() {
        static LatencyProbe inst;
        return inst;
    }

    void onCanRx(int64_t rx_us) {
        m_last_rx_us.store(rx_us, std::memory_order_release);
        m_rx_seq.fetch_add(1, std::memory_order_relaxed);
    }

    // binder 完成一次「新帧」推送时调用
    void onQmlPush(int64_t push_us, uint64_t frame_seq) {
        // 当前生产调用者是 GUI 线程。mutex 使统计读取或未来调用线程变化时，
        // frame_seq 去重、样本数组和聚合值仍构成一个无 data race 的快照。
        std::lock_guard<std::mutex> lock(m_samples_mutex);
        if (frame_seq == m_last_recorded_seq) return;
        m_last_recorded_seq = frame_seq;

        const int64_t rx = m_last_rx_us.load(std::memory_order_acquire);
        if (rx <= 0) return;

        const int64_t latency_us = push_us - rx;
        if (latency_us < 0 || latency_us > 5'000'000) return;  // 丢弃异常值

        // 固定容量语义：饱和后拒绝新样本，所有统计量保持同一批样本。
        if (m_count >= kMaxSamples) return;
        m_samples[static_cast<size_t>(m_count)] = latency_us;
        ++m_count;
        m_sum_us += latency_us;
        m_min_us = std::min(m_min_us, latency_us);
        m_max_us = std::max(m_max_us, latency_us);
    }

    // 返回同一时刻的只读统计快照；无样本时 valid=false、count=0。
    Stats stats() const {
        std::vector<int64_t> sorted;
        int count = 0;
        int64_t sum_us = 0;
        int64_t min_us = 0;
        int64_t max_us = 0;
        {
            std::lock_guard<std::mutex> lock(m_samples_mutex);
            count = m_count;
            sum_us = m_sum_us;
            min_us = m_min_us;
            max_us = m_max_us;
            sorted.assign(m_samples.begin(), m_samples.begin() + count);
        }

        Stats result;
        result.count = count;
        if (count == 0) return result;

        std::sort(sorted.begin(), sorted.end());
        const auto percentile = [&](double p) {
            const size_t index = static_cast<size_t>(p * (count - 1));
            return sorted[index];
        };

        result.valid = true;
        result.min_us = min_us;
        result.avg_us = static_cast<double>(sum_us) / count;
        result.p50_us = percentile(0.50);
        result.p95_us = percentile(0.95);
        result.p99_us = percentile(0.99);
        result.max_us = max_us;
        return result;
    }

    // 样本不足返回 -1；否则返回当前缓冲的 p95（微秒）
    int64_t p95Us() const {
        const Stats snapshot = stats();
        return snapshot.valid ? snapshot.p95_us : -1;
    }

    // 默认约每 5s 打一行 p95；无样本时静默。供采样主线程 tick 调用。
    void maybeLogP95(int64_t now_us, int64_t every_us = 5'000'000,
                     FILE* out = stderr) {
        int64_t expected = m_last_p95_log_us.load(std::memory_order_relaxed);
        if (expected > 0 && now_us - expected < every_us) return;
        if (!m_last_p95_log_us.compare_exchange_strong(expected, now_us,
                                                      std::memory_order_relaxed))
            return;

        const Stats snapshot = stats();
        if (!snapshot.valid) return;
        std::fprintf(out, "[Latency] p95=%.2f ms (n=%d)\n",
                     snapshot.p95_us / 1000.0, snapshot.count);
    }

    void printReport(FILE* out = stderr) const {
        const Stats snapshot = stats();
        if (!snapshot.valid) {
            std::fprintf(out, "[Latency] 无有效样本 (CAN 未收到数据或 binder 未推送)\n");
            return;
        }

        std::fprintf(out, "\n=== CAN → QML 延迟报告 (样本数=%d) ===\n",
                     snapshot.count);
        std::fprintf(out, "  min : %8.2f ms\n", snapshot.min_us / 1000.0);
        std::fprintf(out, "  avg : %8.2f ms\n", snapshot.avg_us / 1000.0);
        std::fprintf(out, "  p50 : %8.2f ms\n", snapshot.p50_us / 1000.0);
        std::fprintf(out, "  p95 : %8.2f ms\n", snapshot.p95_us / 1000.0);
        std::fprintf(out, "  p99 : %8.2f ms\n", snapshot.p99_us / 1000.0);
        std::fprintf(out, "  max : %8.2f ms\n", snapshot.max_us / 1000.0);
        std::fprintf(out, "  注: 含 SnapshotPump %dms (60fps) 轮询等待\n", UI_TICK_MS);
        std::fprintf(out, "========================================\n");
    }

private:
    // CAN 线程仅写入这两个原子值；GUI 线程在 onQmlPush 中读取时间戳。
    std::atomic<int64_t> m_last_rx_us{0};
    std::atomic<uint64_t> m_rx_seq{0};

    mutable std::mutex m_samples_mutex;
    uint64_t m_last_recorded_seq = 0;

    std::array<int64_t, kMaxSamples> m_samples{};
    int m_count = 0;
    int64_t m_sum_us = 0;
    int64_t m_min_us = INT64_MAX;
    int64_t m_max_us = 0;
    std::atomic<int64_t> m_last_p95_log_us{0};
};

}  // namespace platform
