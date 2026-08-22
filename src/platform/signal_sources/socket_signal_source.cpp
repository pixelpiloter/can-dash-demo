#include "platform/signal_sources/socket_signal_source.h"

#include "ingress/can_frame.h"
#include "ingress/can_socket.h"
#include "platform/logging/file_logger.h"
#include "shared_state.h"

#include <chrono>
#include <unordered_map>

namespace platform {

bool SocketSignalSource::prepare(const FrameworkConfig& fw, std::string& err) {
    m_socket_path = fw.can_socket_path;
    if (!demo::loadDecodeTable(fw.path_can_ids)) {
        err = "DecodeTable load failed: " + fw.path_can_ids;
        return false;
    }
    return true;
}

void SocketSignalSource::start(cluster::SharedState& shared,
                               std::chrono::steady_clock::time_point start,
                               std::atomic<bool>& stop) {
    m_shared = &shared;
    m_start = start;
    m_stop = &stop;
    const std::string path = m_socket_path;
    m_thread = std::make_unique<std::thread>([this, path]() {
        demo::CanSocketServer server(path);
        std::string serr;
        if (!demo::CanSocketServer::validatePath(path, serr)) {
            CLK_LOG_ERROR("can_socket", "path invalid: " + serr);
            return;
        }
        if (!server.bindListen()) {
            CLK_LOG_ERROR("can_socket", "bindListen failed: " + path);
            return;
        }
        CLK_LOG_INFO("can_socket", "listening on " + path);
        while (!m_stop->load()) {
            if (server.acceptClient() != demo::AcceptResult::Ok) continue;
            CLK_LOG_INFO("can_socket", "client connected");
            while (!m_stop->load()) {
                demo::CanFrame frame;
                const auto res = server.readFrame(frame);
                if (res == demo::SocketReadResult::Timeout) continue;
                if (res == demo::SocketReadResult::Closed) break;
                std::unordered_map<std::string, double> ctx;
                if (!demo::decodeFrameToCtx(frame, ctx)) continue;
                notifyRx();
                const auto now_ms = std::chrono::duration_cast<
                                        std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() -
                                        m_start)
                                        .count();
                cluster::publishSignals(*m_shared, ctx, now_ms, true);
            }
            server.closeClient();
        }
        server.close();
    });
}

void SocketSignalSource::requestStop() {
    if (m_stop) m_stop->store(true);
}

void SocketSignalSource::join() {
    if (m_thread && m_thread->joinable()) m_thread->join();
    m_thread.reset();
}

}  // namespace platform
