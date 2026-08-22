// can_socket.cpp
// 多会话状态机：Idle → Listening → Accepted → Listening，可重复接入客户端；
// close() 才进入永久 Closed 终态。所有 fd 都先在锁内撤销发布，再在锁外关闭，
// 避免阻塞系统调用持有状态锁，并防止关闭路径误伤后续会话。
#include "can_socket.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

namespace demo {

namespace {

enum class IoResult { Ok, Timeout, Closed };

using SteadyClock = std::chrono::steady_clock;

// 从整帧首字节到齐帧最多允许 1200ms；超过后连接不可重同步，必须结束会话。
constexpr auto kFrameAssemblyTimeout = std::chrono::milliseconds(1200);

struct FrameAssemblyDeadline {
    bool started = false;
    SteadyClock::time_point deadline {};
};

IoResult readFull(int fd, void* buf, size_t n,
                  FrameAssemblyDeadline& assembly) {
    char* p = static_cast<char*>(buf);
    size_t got = 0;
    while (got < n) {
        if (assembly.started && SteadyClock::now() >= assembly.deadline) {
            return IoResult::Closed;
        }

        ssize_t r = ::read(fd, p + got, n - got);
        if (r == 0) return IoResult::Closed;
        if (r < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (!assembly.started) return IoResult::Timeout;
                if (SteadyClock::now() >= assembly.deadline) {
                    return IoResult::Closed;
                }
                continue;
            }
            return IoResult::Closed;
        }

        const auto now = SteadyClock::now();
        if (!assembly.started) {
            assembly.started = true;
            assembly.deadline = now + kFrameAssemblyTimeout;
        } else if (now >= assembly.deadline) {
            return IoResult::Closed;
        }
        got += static_cast<size_t>(r);
    }
    return IoResult::Ok;
}

bool setRecvTimeoutMs(int fd, int ms) {
    timeval tv{};
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
}

bool setNonBlocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

}  // namespace

CanSocketServer::CanSocketServer(const std::string& path)
    : m_path(path) {}

CanSocketServer::~CanSocketServer() {
    close();
}

bool CanSocketServer::validatePath(const std::string& path, std::string& error) {
    sockaddr_un addr {};
    // sun_path 是含结尾 NUL 的定长字节数组，限制按本地编码后的 std::string 字节数，
    // 不是 Unicode 字符数；预留一字节后才能安全 memcpy(size + 1)。
    if (path.size() >= sizeof(addr.sun_path)) {
        error = "Unix socket path too long (" + std::to_string(path.size()) +
                " bytes; maximum " +
                std::to_string(sizeof(addr.sun_path) - 1) + "): " + path;
        return false;
    }
    if (path.find('\0') != std::string::npos) {
        error = "Unix socket path contains an embedded NUL byte";
        return false;
    }
    error.clear();
    return true;
}

bool CanSocketServer::bindListen() {
    {
        std::lock_guard<std::mutex> lk(m_mu);
        if (m_close_requested) {
            std::fprintf(stderr,
                         "[CanSocket] bindListen() rejected: server is closed\n");
            return false;
        }
        if (m_srv >= 0 || m_cli >= 0) {
            std::fprintf(stderr,
                         "[CanSocket] bindListen() rejected: already bound\n");
            return false;
        }
    }

    std::string path_error;
    if (!validatePath(m_path, path_error)) {
        std::fprintf(stderr, "FATAL: %s\n", path_error.c_str());
        return false;
    }

    sockaddr_un addr {};
    ::unlink(m_path.c_str());

    int srv = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (srv < 0) {
        std::fprintf(stderr, "[CanSocket] socket() failed: %s\n",
                     std::strerror(errno));
        return false;
    }

    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, m_path.c_str(), m_path.size() + 1);

    if (::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::fprintf(stderr, "[CanSocket] bind(%s) failed: %s\n",
                     m_path.c_str(), std::strerror(errno));
        ::close(srv);
        return false;
    }
    if (::listen(srv, 1) < 0) {
        std::fprintf(stderr, "[CanSocket] listen() failed: %s\n",
                     std::strerror(errno));
        ::close(srv);
        ::unlink(m_path.c_str());
        return false;
    }
    if (!setNonBlocking(srv)) {
        std::fprintf(stderr, "[CanSocket] fcntl(O_NONBLOCK) failed: %s\n",
                     std::strerror(errno));
        ::close(srv);
        ::unlink(m_path.c_str());
        return false;
    }

    bool publish = false;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        if (!m_close_requested && m_srv < 0 && m_cli < 0) {
            m_srv = srv;
            m_listened = true;
            publish = true;
        }
    }
    if (!publish) {
        // bind/listen 使用局部 fd，成功后才在锁内发布。close() 若先到，关闭请求
        // 不能被后发布的 fd“复活”；实际 close/unlink 仍在锁外执行。
        ::close(srv);
        ::unlink(m_path.c_str());
        return false;
    }
    return true;
}

AcceptResult CanSocketServer::acceptClient() {
    int poll_fd = -1;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        if (m_close_requested) {
            std::fprintf(stderr,
                         "[CanSocket] acceptClient() rejected: server is closed\n");
            return AcceptResult::Closed;
        }
        if (m_srv < 0) {
            std::fprintf(stderr,
                         "[CanSocket] acceptClient() rejected: server is not bound\n");
            return AcceptResult::Closed;
        }
        if (m_cli >= 0) {
            std::fprintf(stderr,
                         "[CanSocket] acceptClient() rejected: client already active\n");
            return AcceptResult::Closed;
        }
        poll_fd = m_srv;
    }

    // 单次 200ms poll 把重试策略交给调用方；close() 会关闭 m_srv，且超时后还会
    // 复查永久关闭状态，所以跨线程停止最多一个 poll 周期即可收敛。
    for (;;) {
        pollfd pfd{};
        pfd.fd = poll_fd;
        pfd.events = POLLIN;
        const int pr = ::poll(&pfd, 1, 200);
        if (pr < 0) {
            if (errno == EINTR) continue;
            std::fprintf(stderr, "[CanSocket] poll() failed: %s\n",
                         std::strerror(errno));
            return AcceptResult::Closed;
        }
        if (pr == 0) {
            std::lock_guard<std::mutex> lk(m_mu);
            return (!m_close_requested && m_srv == poll_fd)
                ? AcceptResult::Timeout
                : AcceptResult::Closed;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            return AcceptResult::Closed;
        }

        int cli = ::accept(poll_fd, nullptr, nullptr);
        if (cli < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return AcceptResult::Timeout;
            }
            if (errno == EBADF || errno == EINVAL) {
                return AcceptResult::Closed;
            }
            std::fprintf(stderr, "[CanSocket] accept() failed: %s\n",
                         std::strerror(errno));
            return AcceptResult::Closed;
        }
        // recv 超时让 Reading 状态定期把控制权还给调用方检查 g_stop。
        if (!setRecvTimeoutMs(cli, 200)) {
            std::fprintf(stderr, "[CanSocket] setsockopt(SO_RCVTIMEO) failed: %s\n",
                         std::strerror(errno));
            ::close(cli);
            return AcceptResult::Closed;
        }

        bool publish = false;
        {
            std::lock_guard<std::mutex> lk(m_mu);
            if (!m_close_requested && m_srv == poll_fd && m_cli < 0) {
                m_cli = cli;
                publish = true;
            }
        }
        if (!publish) {
            // close() 或另一个 acceptClient() 抢先；局部 fd 从未发布。
            ::shutdown(cli, SHUT_RDWR);
            ::close(cli);
            return AcceptResult::Closed;
        }
        return AcceptResult::Ok;
    }
}

bool CanSocketServer::listen() {
    if (!bindListen()) return false;
    for (;;) {
        const AcceptResult result = acceptClient();
        if (result == AcceptResult::Ok) return true;
        if (result == AcceptResult::Closed) return false;
    }
}

SocketReadResult CanSocketServer::readFrame(CanFrame& f) {
    int cli = -1;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        if (m_cli >= 0) cli = ::dup(m_cli);
    }
    if (cli < 0) return SocketReadResult::Closed;

    const auto finish = [cli](SocketReadResult result) {
        ::close(cli);
        return result;
    };

    uint32_t can_id_le;
    uint8_t dlc;
    FrameAssemblyDeadline assembly;
    IoResult r = readFull(cli, &can_id_le, 4, assembly);
    if (r == IoResult::Timeout) return finish(SocketReadResult::Timeout);
    if (r != IoResult::Ok) return finish(SocketReadResult::Closed);

    r = readFull(cli, &dlc, 1, assembly);
    if (r == IoResult::Timeout) return finish(SocketReadResult::Timeout);
    if (r != IoResult::Ok) return finish(SocketReadResult::Closed);

    if (dlc > 8) {
        // 流协议没有帧边界可供可靠重同步；非法长度后继续读会把 payload 当下一帧头。
        // 因此把协议错误提升为连接终止，由上层按“数据源断开”统一收尾。
        std::fprintf(stderr, "[CanSocket] bad dlc=%u (max 8), abort frame\n", dlc);
        return finish(SocketReadResult::Closed);
    }
    if (dlc > 0) {
        r = readFull(cli, f.data, dlc, assembly);
        if (r == IoResult::Timeout) return finish(SocketReadResult::Timeout);
        if (r != IoResult::Ok) return finish(SocketReadResult::Closed);
    }

    f.can_id = can_id_le;
    f.dlc = dlc;
    return finish(SocketReadResult::Ok);
}

void CanSocketServer::closeClient() {
    int cli = -1;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        cli = m_cli;
        m_cli = -1;
    }
    // 状态锁只保护 fd 所有权转移。shutdown/close 可唤醒正在 read 的线程，
    // 必须在锁外执行；它不触碰 server fd、永久关闭标志或 socket 路径。
    if (cli >= 0) {
        ::shutdown(cli, SHUT_RDWR);
        ::close(cli);
    }
}

void CanSocketServer::close() {
    int cli = -1;
    int srv = -1;
    bool do_unlink = false;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_close_requested = true;
        cli = m_cli;
        srv = m_srv;
        m_cli = -1;
        m_srv = -1;
        do_unlink = m_listened;
        m_listened = false;
    }
    // 先在锁内撤销发布（后续读者只能看到 -1），再在锁外 shutdown/close。
    // 系统调用可能唤醒或等待另一个 I/O 线程，绝不能占着状态锁执行。
    if (cli >= 0) {
        ::shutdown(cli, SHUT_RDWR);
        ::close(cli);
    }
    if (srv >= 0) {
        ::shutdown(srv, SHUT_RDWR);
        ::close(srv);
    }
    if (do_unlink) {
        ::unlink(m_path.c_str());
    }
}

}  // namespace demo
