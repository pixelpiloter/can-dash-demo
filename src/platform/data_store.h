// data_store.h
// 进程内键值数据存储（数值/布尔/字符串/报警/健康），UI 无关。
#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace cluster {

struct WarnItem {
    std::string id;
    bool active = false;
};

class DataStore {
public:
    void setNumber(const std::string& key, double value);
    void setBool(const std::string& key, bool value);
    void setString(const std::string& key, const std::string& value);
    void setWarnList(std::vector<WarnItem> items);
    void setHealth(const std::string& status);

    double getNumber(const std::string& key, double fallback = 0.0) const;
    bool getBool(const std::string& key, bool fallback = false) const;
    std::string getString(const std::string& key,
                          const std::string& fallback = {}) const;
    std::vector<WarnItem> warnList() const;
    std::string health() const;

    // 调试：导出当前数值键（MVP 控制台宿主用）
    std::unordered_map<std::string, double> numbersSnapshot() const;

private:
    mutable std::mutex m_mtx;
    std::unordered_map<std::string, double> m_numbers;
    std::unordered_map<std::string, bool> m_bools;
    std::unordered_map<std::string, std::string> m_strings;
    std::vector<WarnItem> m_warns;
    std::string m_health = "disconnected";
};

}  // namespace cluster
