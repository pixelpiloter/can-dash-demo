// config_catalog.cpp

#include "config_catalog.h"

#include <yaml-cpp/yaml.h>

namespace platform {

bool ConfigCatalog::loadWarnYaml(const std::string& path) {
    try {
        YAML::Node root = YAML::LoadFile(path);
        if (!root["warns"] || !root["warns"].IsSequence()) return false;
        m_warns.clear();
        for (const auto& node : root["warns"]) {
            // 无 id 的条目无法索引，局部跳过；其他展示字段缺失时保留空值，让
            // QtBinder 使用 id、默认颜色和默认优先级降级。
            if (!node["name"]) continue;
            WarnMeta m;
            const std::string id = node["name"].as<std::string>();
            if (node["text_zh"]) m.text_zh = node["text_zh"].as<std::string>();
            if (node["text_en"]) m.text_en = node["text_en"].as<std::string>();
            if (node["color"])   m.color   = node["color"].as<std::string>();
            if (node["priority"]) m.priority = node["priority"].as<std::string>();
            m_warns[id] = std::move(m);
        }
        return true;
    } catch (...) {
        // Catalog 不解释 YAML 异常；false 把启动策略留给调用方。
        return false;
    }
}

bool ConfigCatalog::loadLightYaml(const std::string& path) {
    try {
        YAML::Node root = YAML::LoadFile(path);
        if (!root["lights"] || !root["lights"].IsSequence()) return false;
        m_lights.clear();
        for (const auto& node : root["lights"]) {
            // image/size/position 是可选展示元数据，缺失保持零值而非使整表失败。
            if (!node["name"]) continue;
            LightMeta m;
            const std::string id = node["name"].as<std::string>();
            if (node["image"]) m.image = node["image"].as<std::string>();
            if (node["size"]) {
                if (node["size"]["width"])  m.width  = node["size"]["width"].as<int>();
                if (node["size"]["height"]) m.height = node["size"]["height"].as<int>();
            }
            if (node["position"]) {
                if (node["position"]["x"]) m.pos_x = node["position"]["x"].as<int>();
                if (node["position"]["y"]) m.pos_y = node["position"]["y"].as<int>();
            }
            m_lights[id] = std::move(m);
        }
        return true;
    } catch (...) {
        return false;
    }
}

const WarnMeta* ConfigCatalog::warnMeta(const std::string& id) const {
    auto it = m_warns.find(id);
    return it != m_warns.end() ? &it->second : nullptr;
}

const LightMeta* ConfigCatalog::lightMeta(const std::string& id) const {
    auto it = m_lights.find(id);
    return it != m_lights.end() ? &it->second : nullptr;
}

}  // namespace platform
