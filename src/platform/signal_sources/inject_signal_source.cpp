#include "platform/signal_sources/inject_signal_source.h"

#include "shared_state.h"

#include <chrono>
#include <thread>
#include <unordered_map>

namespace platform {

void InjectSignalSource::start(cluster::SharedState& shared,
                               std::chrono::steady_clock::time_point start,
                               std::atomic<bool>& stop) {
    m_shared = &shared;
    m_start = start;
    m_stop = &stop;
    m_thread = std::make_unique<std::thread>([this]() {
        double spd = 0.0;
        double soc = 80.0;
        int tick = 0;
        const double gears[] = {0.0, 1.0, 2.0, 3.0};
        while (!m_stop->load()) {
            const double gear = gears[(tick / 25) % 4];
            const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - m_start)
                                    .count();
            notifyRx();
            cluster::publishSignals(*m_shared, {
                {"vehicle_speed", spd},
                {"gear_status", gear},
                {"motor_rpm", spd * 30.0},
                {"motor_temp", 80.0},
                {"bat_volt", 380.0},
                {"bat_curr", -20.0},
                {"bat_soc", soc},
                {"battery_temp", 25.0},
                {"energy_mode", static_cast<double>(tick / 50 % 4)},
                {"engine_rpm", spd > 40.0 ? 2500.0 : 0.0},
                {"charge_power", 0.0},
                {"ev_range", 60.0},
                {"fuel_range", 320.0},
                {"fuel_level", 55.0},
                {"brake", (spd < 5.0) ? 60.0 : 0.0},
                {"driver_occupied", 1.0},
                {"driver_buckled", 1.0},
            }, now_ms, true);
            spd += 2.5;
            if (spd > 120.0) spd = 0.0;
            soc -= 0.05;
            if (soc < 15.0) soc = 90.0;
            ++tick;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
}

void InjectSignalSource::requestStop() {
    if (m_stop) m_stop->store(true);
}

void InjectSignalSource::join() {
    if (m_thread && m_thread->joinable()) m_thread->join();
    m_thread.reset();
}

}  // namespace platform
