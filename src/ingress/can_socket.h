// can_socket.h
// 层: runtime — Unix socket CAN 帧接收
// 协议: [can_id u32 LE][dlc u8][data…]
// 退出: 他线程 close() 可打断阻塞 read/accept，配合 g_stop 干净 join
#pragma once

#include "can_frame.h"

#include <mutex>
#include <string>

namespace demo {

enum class SocketReadResult {
    Ok = 0,
    Timeout,   // 接收超时，可继续读（用于检查 g_stop）
    Closed,    // 对端断开 / 本端 close / 错误
};

enum class AcceptResult {
    Ok = 0,
    Timeout,   // 200ms 内没有新客户端，可继续 accept
    Closed,    // server 已关闭、未监听或发生不可恢复错误
};

class CanSocketServer {
public:
    explicit CanSocketServer(const std::string& path);
    ~CanSocketServer();

    CanSocketServer(const CanSocketServer&) = delete;
    CanSocketServer& operator=(const CanSocketServer&) = delete;

    // 校验路径是否可完整写入 sockaddr_un::sun_path。
    static bool validatePath(const std::string& path, std::string& error);

    // 只创建、bind 并监听 server socket；成功后可接入多个顺序会话。
    bool bindListen();

    // 在已有 server fd 上等待一次客户端，最多 200ms。成功后发布 client fd；
    // 已有客户端、尚未 bind 或已永久 close 都返回 Closed 并报告明确错误。
    AcceptResult acceptClient();

    // 兼容单会话调用方：bind 后循环 accept，直到接入成功或永久关闭。
    bool listen();

    // 读一帧；尚未收到任何字节时 Timeout 可重试，部分帧装配超时返回 Closed。
    SocketReadResult readFrame(CanFrame& f);

    // 只结束当前客户端会话；server 继续监听且 socket 路径保持存在。
    void closeClient();

    // 关闭 fd（可从其他线程调用以打断阻塞；幂等、互斥）；
    // 首次调用后，本实例不再允许 bind/listen，并移除 socket 路径。
    void close();

    const std::string& path() const { return m_path; }

private:
    std::string m_path;
    mutable std::mutex m_mu;
    int m_srv = -1;
    int m_cli = -1;
    bool m_listened = false;
    bool m_close_requested = false;
};

}  // namespace demo
