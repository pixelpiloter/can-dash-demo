#include "data_store.h"

namespace cluster {

void DataStore::setNumber(const std::string& key, double value) {
    std::lock_guard<std::mutex> lk(m_mtx);
    m_numbers[key] = value;
}

void DataStore::setBool(const std::string& key, bool value) {
    std::lock_guard<std::mutex> lk(m_mtx);
    m_bools[key] = value;
}

void DataStore::setString(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lk(m_mtx);
    m_strings[key] = value;
}

void DataStore::setWarnList(std::vector<WarnItem> items) {
    std::lock_guard<std::mutex> lk(m_mtx);
    m_warns = std::move(items);
}

void DataStore::setHealth(const std::string& status) {
    std::lock_guard<std::mutex> lk(m_mtx);
    m_health = status;
}

double DataStore::getNumber(const std::string& key, double fallback) const {
    std::lock_guard<std::mutex> lk(m_mtx);
    auto it = m_numbers.find(key);
    return it == m_numbers.end() ? fallback : it->second;
}

bool DataStore::getBool(const std::string& key, bool fallback) const {
    std::lock_guard<std::mutex> lk(m_mtx);
    auto it = m_bools.find(key);
    return it == m_bools.end() ? fallback : it->second;
}

std::string DataStore::getString(const std::string& key,
                                 const std::string& fallback) const {
    std::lock_guard<std::mutex> lk(m_mtx);
    auto it = m_strings.find(key);
    return it == m_strings.end() ? fallback : it->second;
}

std::vector<WarnItem> DataStore::warnList() const {
    std::lock_guard<std::mutex> lk(m_mtx);
    return m_warns;
}

std::string DataStore::health() const {
    std::lock_guard<std::mutex> lk(m_mtx);
    return m_health;
}

std::unordered_map<std::string, double> DataStore::numbersSnapshot() const {
    std::lock_guard<std::mutex> lk(m_mtx);
    return m_numbers;
}

}  // namespace cluster
