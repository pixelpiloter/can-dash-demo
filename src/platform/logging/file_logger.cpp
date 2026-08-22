#include "platform/logging/file_logger.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace platform {
namespace {

const char* levelToString(int level) {
    switch (level) {
        case 0:
            return "DEBUG";
        case 1:
            return "INFO ";
        case 2:
            return "WARN ";
        case 3:
            return "ERROR";
        default:
            return "UNK  ";
    }
}

std::string isoTimestampMs() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms =
        duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                  tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));
    return buf;
}

long long fileSizeOrZero(const std::string& path) {
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) return 0;
    return static_cast<long long>(st.st_size);
}

bool mkdirOne(const std::string& path) {
    if (path.empty() || path == "/") return true;
    struct stat st {};
    if (::stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return ::mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

bool mkdirParents(const std::string& path) {
    if (path.empty()) return false;
    std::string cur;
    if (!path.empty() && path[0] == '/') cur = "/";
    std::stringstream ss(path);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (part.empty()) continue;
        if (cur == "/")
            cur += part;
        else if (cur.empty())
            cur = part;
        else
            cur += "/" + part;
        if (!mkdirOne(cur)) return false;
    }
    return true;
}

}  // namespace

FileLogger& FileLogger::instance() {
    static FileLogger inst;
    return inst;
}

FileLogger::FileLogger() {
    const char* env = std::getenv("CLUSTER_LOGIC_LOG_DIR");
    if (env && env[0]) {
        applyDir(env);
        return;
    }
    applyDir("logs");
}

FileLogger::~FileLogger() {
    if (!m_dirOk || m_buffer.empty()) return;
    flushBuffer();
}

void FileLogger::setLogDir(const std::string& dir) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_buffer.empty()) flushBuffer();
    applyDir(dir);
}

void FileLogger::applyDir(const std::string& dir) {
    m_logDir = dir;
    m_filePath = m_logDir + "/" + kDefaultLogFile;
    m_dirOk = ensureDirectory(m_logDir);
    if (!m_dirOk) {
        std::fprintf(stderr, "FileLogger: log dir unavailable: %s\n",
                     m_logDir.c_str());
    }
}

void FileLogger::info(const std::string& source, const std::string& message) {
    log(1, source, message);
}
void FileLogger::warn(const std::string& source, const std::string& message) {
    log(2, source, message);
}
void FileLogger::error(const std::string& source, const std::string& message) {
    log(3, source, message);
}
void FileLogger::debug(const std::string& source, const std::string& message) {
    log(0, source, message);
}

bool FileLogger::isReady() const noexcept { return m_dirOk; }

void FileLogger::log(int level, const std::string& source,
                     const std::string& message) {
    writeImpl(level, source, message);
}

void FileLogger::flush() {
    std::lock_guard<std::mutex> lock(m_mutex);
    flushBuffer();
}

void FileLogger::writeImpl(int level, const std::string& source,
                           const std::string& message) {
    if (!m_dirOk) return;

    std::ostringstream oss;
    oss << '[' << isoTimestampMs() << "] " << levelToString(level) << ' '
        << source << " | " << message << '\n';
    const std::string line = oss.str();

    std::lock_guard<std::mutex> lock(m_mutex);
    const size_t available =
        m_buffer.size() >= static_cast<size_t>(kMaxBufferSize)
            ? 0
            : static_cast<size_t>(kMaxBufferSize) - m_buffer.size();
    if (available > 0) {
        m_buffer.append(line, 0, std::min(available, line.size()));
    }
    if (m_buffer.size() >= static_cast<size_t>(kFlushThreshold) || level == 3) {
        flushBuffer();
    }
}

bool FileLogger::flushBuffer() {
    if (m_buffer.empty()) return true;
    rotateIfNeeded();

    std::ofstream out(m_filePath, std::ios::out | std::ios::app | std::ios::binary);
    if (!out) {
        reportIoFailure("open", m_filePath);
        m_buffer.clear();
        return false;
    }
    out.write(m_buffer.data(), static_cast<std::streamsize>(m_buffer.size()));
    if (!out) {
        reportIoFailure("write", m_filePath);
        m_buffer.clear();
        return false;
    }
    out.flush();
    if (!out) {
        reportIoFailure("flush", m_filePath);
        m_buffer.clear();
        return false;
    }
    m_buffer.clear();
    m_ioFailureReported = false;
    return true;
}

void FileLogger::reportIoFailure(const char* operation,
                                 const std::string& detail) {
    if (m_ioFailureReported) return;
    std::fprintf(stderr, "FileLogger: %s failed for '%s'\n", operation,
                 detail.c_str());
    std::fflush(stderr);
    m_ioFailureReported = true;
}

bool FileLogger::ensureDirectory(const std::string& dir) {
    return mkdirParents(dir);
}

void FileLogger::rotateIfNeeded() {
    if (fileSizeOrZero(m_filePath) < kMaxFileSize) return;

    const std::string oldest =
        m_logDir + "/" + kDefaultLogFile + "." + std::to_string(kMaxBackups);
    ::unlink(oldest.c_str());

    for (int i = kMaxBackups - 1; i >= 1; --i) {
        const std::string src =
            m_logDir + "/" + kDefaultLogFile + "." + std::to_string(i);
        const std::string dst =
            m_logDir + "/" + kDefaultLogFile + "." + std::to_string(i + 1);
        ::rename(src.c_str(), dst.c_str());
    }
    const std::string first = m_logDir + "/" + kDefaultLogFile + ".1";
    ::rename(m_filePath.c_str(), first.c_str());
}

}  // namespace platform
