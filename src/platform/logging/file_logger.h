// file_logger.h — 无 Qt：单例文件日志，1MB x 3 轮转
// 默认目录：环境变量 CLUSTER_LOGIC_LOG_DIR，否则 ./logs（相对 cwd）。
// 目录不可用时静默丢弃，不拖垮业务。
#pragma once

#include <mutex>
#include <string>

namespace platform {

class FileLogger {
public:
    static FileLogger& instance();

    FileLogger(const FileLogger&) = delete;
    FileLogger& operator=(const FileLogger&) = delete;

    void info(const std::string& source, const std::string& message);
    void warn(const std::string& source, const std::string& message);
    void error(const std::string& source, const std::string& message);
    void debug(const std::string& source, const std::string& message);

    bool isReady() const noexcept;
    const std::string& filePath() const noexcept { return m_filePath; }

    // 0=DEBUG 1=INFO 2=WARN 3=ERROR
    void log(int level, const std::string& source, const std::string& message);

    // 强制刷盘（退出前调用）
    void flush();

    // 覆盖默认目录（须在首条 log 前调用；会重建路径）
    void setLogDir(const std::string& dir);

private:
    FileLogger();
    ~FileLogger();

    void writeImpl(int level, const std::string& source,
                   const std::string& message);
    bool flushBuffer();
    void reportIoFailure(const char* operation, const std::string& detail);
    void rotateIfNeeded();
    bool ensureDirectory(const std::string& dir);
    void applyDir(const std::string& dir);

    static constexpr const char* kDefaultLogFile = "cluster.log";
    static constexpr int kMaxFileSize = 1024 * 1024;
    static constexpr int kMaxBackups = 3;
    static constexpr int kFlushThreshold = 1024;
    static constexpr int kMaxBufferSize = 64 * 1024;

    std::mutex m_mutex;
    std::string m_logDir;
    std::string m_filePath;
    std::string m_buffer;
    bool m_dirOk = false;
    bool m_ioFailureReported = false;
};

}  // namespace platform

#define CLK_LOG_DEBUG(src, msg) \
    ::platform::FileLogger::instance().debug((src), (msg))
#define CLK_LOG_INFO(src, msg) \
    ::platform::FileLogger::instance().info((src), (msg))
#define CLK_LOG_WARN(src, msg) \
    ::platform::FileLogger::instance().warn((src), (msg))
#define CLK_LOG_ERROR(src, msg) \
    ::platform::FileLogger::instance().error((src), (msg))
