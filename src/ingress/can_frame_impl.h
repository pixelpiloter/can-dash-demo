// can_frame_impl.h — DecodeTable 的实现 (header-only, 跟 demo 其它 inline 风格一致)
// 不需要单独编译, 只需在 main.cpp / 测试代码 #include <can_frame.h> 即可

#pragma once

#include "can_frame.h"

#include <yaml-cpp/yaml.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace demo {

// ── load: 读 YAML, 填充 m_messages ──
inline bool DecodeTable::load(const std::string& canIdsYamlPath) {
    try {
        decltype(m_messages) messages;
        decltype(m_available_field_names) available_field_names;
        YAML::Node root = YAML::LoadFile(canIdsYamlPath);
        if (!root["can_sources"]) {
            std::fprintf(stderr, "[DecodeTable] %s: missing 'can_sources'\n",
                         canIdsYamlPath.c_str());
            return false;
        }
        for (const auto& src : root["can_sources"]) {
            // can_id 可能是 int 或 "0x18..." 字符串
            uint32_t can_id = 0;
            YAML::Node id_node = src["can_id"];
            std::string can_source = src["name"]
                ? src["name"].as<std::string>()
                : id_node.as<std::string>();
            if (id_node.IsScalar()) {
                std::string s = id_node.as<std::string>();
                if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0)
                    can_id = static_cast<uint32_t>(std::stoul(s.substr(2), nullptr, 16));
                else
                    can_id = id_node.as<uint32_t>();
            } else {
                can_id = id_node.as<uint32_t>();
            }
            MessageSpec ms;
            ms.can_id = can_id;
            for (const auto& f : src["fields"]) {
                FieldSpec fs;
                fs.name = f["name"].as<std::string>();
                YAML::Node byte_node = f["byte"];
                if (byte_node.IsSequence()) {
                    fs.byte_first = byte_node[0].as<int>();
                    fs.byte_last  = byte_node[1].as<int>();
                } else {
                    fs.byte_first = fs.byte_last = byte_node.as<int>();
                }
                fs.bits = f["bits"].as<int>();
                if (f["endian"]) {
                    std::string e = f["endian"].as<std::string>();
                    fs.little_endian = (e == "little");
                }
                std::string t = f["type"].as<std::string>();
                fs.is_signed = (t.rfind("int", 0) == 0);  // int8/int16/int32 → signed
                if (f["formula"]) {
                    fs.formula = f["formula"].as<std::string>();
                    std::string formula_error;
                    if (!can_formula::compile(
                            fs.formula, fs.compiled_formula, formula_error) ||
                        !can_formula::validateRawRange(
                            fs.compiled_formula, fs.bits, fs.is_signed,
                            formula_error)) {
                        const std::string error = can_formula::validationError(
                            canIdsYamlPath, can_source, fs.name, fs.formula,
                            formula_error);
                        std::fprintf(stderr, "[DecodeTable] load FAIL: %s\n",
                                     error.c_str());
                        return false;
                    }
                }
                if (f["unit"])     fs.unit    = f["unit"].as<std::string>();
                ms.fields.push_back(std::move(fs));
            }
            std::array<std::vector<std::string>, 9> available_by_dlc;
            for (const auto& fs : ms.fields) {
                for (size_t dlc = 0; dlc < available_by_dlc.size(); ++dlc) {
                    if (static_cast<int>(dlc) > fs.byte_last)
                        available_by_dlc[dlc].push_back(fs.name);
                }
            }
            available_field_names[can_id] = std::move(available_by_dlc);
            messages[can_id] = std::move(ms);
        }
        m_available_field_names = std::move(available_field_names);
        m_messages = std::move(messages);
        std::fprintf(stderr, "[DecodeTable] loaded %zu CAN messages from %s\n",
                     m_messages.size(), canIdsYamlPath.c_str());
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[DecodeTable] %s load FAIL: %s\n",
                     canIdsYamlPath.c_str(), e.what());
        return false;
    }
}

// 解码采用“可用字段优先”：未知 can_id 返回 false；短 DLC 只跳过越界字段，已完整
// 到达的字段仍可更新 ctx。调用方据此保留缺失字段的上一值，而不是清空整帧信号。
inline bool DecodeTable::decode(const CanFrame& f,
                                std::unordered_map<std::string, double>& ctx) const {
    bool any = false;
    try {
        auto it = m_messages.find(f.can_id);
        if (it == m_messages.end()) return false;
        const auto& ms = it->second;
        for (const auto& fs : ms.fields) {
            // 不整帧失败：逐字段跳过越界项，允许已完整到达的前缀字段更新 ctx。
            if (static_cast<int>(f.dlc) <= fs.byte_last) continue;
            try {
                uint64_t raw_u = extractRaw(f.data, fs);
                double raw_d;
                if (fs.is_signed) {
                    int64_t sv = toSigned(raw_u, fs.bits);
                    raw_d = static_cast<double>(sv);
                } else {
                    raw_d = static_cast<double>(raw_u);
                }

                double physical = raw_d;
                std::string formula_error;
                if (!fs.formula.empty() &&
                    !can_formula::evaluate(
                        fs.compiled_formula, raw_d, physical, formula_error)) {
                    if (!fs.formula_error_logged->exchange(true)) {
                        std::fprintf(
                            stderr,
                            "[DecodeTable] decode skipped can_id=0x%X field='%s' "
                            "formula='%s': %s (reported once)\n",
                            f.can_id, fs.name.c_str(), fs.formula.c_str(),
                            formula_error.c_str());
                    }
                    continue;
                }
                ctx[fs.name] = physical;
                any = true;
            } catch (const std::exception& e) {
                if (!fs.formula_error_logged->exchange(true)) {
                    std::fprintf(
                        stderr,
                        "[DecodeTable] decode skipped can_id=0x%X field='%s': "
                        "unexpected exception: %s (reported once)\n",
                        f.can_id, fs.name.c_str(), e.what());
                }
            } catch (...) {
                if (!fs.formula_error_logged->exchange(true)) {
                    std::fprintf(
                        stderr,
                        "[DecodeTable] decode skipped can_id=0x%X field='%s': "
                        "unknown exception (reported once)\n",
                        f.can_id, fs.name.c_str());
                }
            }
        }
    } catch (const std::exception& e) {
        static std::atomic_bool logged{false};
        if (!logged.exchange(true))
            std::fprintf(stderr,
                         "[DecodeTable] decode aborted safely: %s (reported once)\n",
                         e.what());
    } catch (...) {
        static std::atomic_bool logged{false};
        if (!logged.exchange(true))
            std::fprintf(stderr,
                         "[DecodeTable] decode aborted safely: unknown exception "
                         "(reported once)\n");
    }
    return any;
}

inline std::vector<std::string> DecodeTable::allFieldNames() const {
    std::vector<std::string> out;
    for (const auto& [_, ms] : m_messages)
        for (const auto& fs : ms.fields)
            out.push_back(fs.name);
    return out;
}

inline const std::vector<std::string>& DecodeTable::availableFieldNames(
    uint32_t can_id, uint8_t dlc) const {
    static const std::vector<std::string> empty;
    if (dlc > 8) return empty;
    const auto it = m_available_field_names.find(can_id);
    return it == m_available_field_names.end() ? empty : it->second[dlc];
}

// ── 内部: raw 字节提取 (little/big endian + 多字节拼接) ──
inline uint64_t DecodeTable::extractRaw(const uint8_t data[8], const FieldSpec& fs) {
    int span = fs.byte_last - fs.byte_first + 1;
    uint64_t raw = 0;
    if (fs.little_endian) {
        // LSB first
        for (int i = 0; i < span; ++i) {
            if (fs.byte_first + i >= 8) break;
            raw |= static_cast<uint64_t>(data[fs.byte_first + i]) << (8 * i);
        }
    } else {
        // MSB first (Motorola)
        for (int i = 0; i < span; ++i) {
            if (fs.byte_first + i >= 8) break;
            raw = (raw << 8) | static_cast<uint64_t>(data[fs.byte_first + i]);
        }
    }
    // 截断到 fs.bits 位
    if (fs.bits < 64) {
        raw &= (1ULL << fs.bits) - 1;
    }
    return raw;
}

// ── 内部: 无符号 → signed 解析 (二的补码) ──
inline int64_t DecodeTable::toSigned(uint64_t raw, int bits) {
    if (bits <= 0 || bits >= 64) return static_cast<int64_t>(raw);
    uint64_t sign_mask = 1ULL << (bits - 1);
    if (raw & sign_mask) {
        // 负数: 补码 → int64
        uint64_t extend = ~((1ULL << bits) - 1);
        return static_cast<int64_t>(raw | extend);
    }
    return static_cast<int64_t>(raw);
}

}  // namespace demo
