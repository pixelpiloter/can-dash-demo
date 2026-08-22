// config_loader.cpp
#include "config_loader.h"
#include "can_formula.h"

#include <yaml-cpp/yaml.h>

#include <cstdio>
#include <regex>
#include <sstream>

namespace platform {

namespace {

std::string dirnameOf(const std::string& path) {
    const auto pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return ".";
    if (pos == 0) return "/";
    return path.substr(0, pos);
}

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty() || a == ".") return b;
    if (!a.empty() && (a.back() == '/' || a.back() == '\\')) return a + b;
    return a + "/" + b;
}

// framework.yaml 内 paths 历史上相对进程 cwd（安装根下的 config/...）。
// 宿主可能 chdir 到别处（如 Kanzi 工程目录），故相对路径改为相对 framework 文件位置解析。
std::string resolveConfigPath(const std::string& framework_yaml, const std::string& p) {
    if (p.empty() || p[0] == '/') return p;
    const std::string fwDir = dirnameOf(framework_yaml);
    if (p.size() >= 7 && p.compare(0, 7, "config/") == 0) {
        return joinPath(dirnameOf(fwDir), p);
    }
    return joinPath(fwDir, p);
}

bool loadFramework(const std::string& path, FrameworkConfig& fw, std::string& err) {
    try {
        // 缺省键保留 FrameworkConfig 的编译期默认值；显式但越界的频率则硬失败，
        // 避免静默接受拼写正确但危险的调度配置。
        YAML::Node root = YAML::LoadFile(path);
        if (root["framework_version"])
            fw.version = root["framework_version"].as<std::string>();
        if (root["tick_hz"])
            fw.tick_hz = root["tick_hz"].as<int>();
        if (root["ui_fps_hz"])
            fw.ui_fps_hz = root["ui_fps_hz"].as<int>();
        if (root["data_source"])
            fw.data_source = root["data_source"].as<std::string>();
        if (root["can_socket_path"])
            fw.can_socket_path = root["can_socket_path"].as<std::string>();
        if (root["ui_backend"])
            fw.ui_backend = root["ui_backend"].as<std::string>();
        if (root["paths"]) {
            auto p = root["paths"];
            if (!p.IsMap()) {
                err = path + ": framework.paths must be a map";
                return false;
            }
            if (p["can_ids"]) fw.path_can_ids = p["can_ids"].as<std::string>();
            if (p["logic"])   fw.path_logic   = p["logic"].as<std::string>();
            if (p["warn"])    fw.path_warn    = p["warn"].as<std::string>();
            if (p["lights"])  fw.path_lights  = p["lights"].as<std::string>();
        }
        fw.path_can_ids = resolveConfigPath(path, fw.path_can_ids);
        fw.path_logic = resolveConfigPath(path, fw.path_logic);
        fw.path_warn = resolveConfigPath(path, fw.path_warn);
        fw.path_lights = resolveConfigPath(path, fw.path_lights);
        if (fw.tick_hz <= 0 || fw.tick_hz > 100) {
            err = path + ": framework.tick_hz out of range (1..100)";
            return false;
        }
        if (fw.ui_fps_hz <= 0 || fw.ui_fps_hz > 120) {
            err = path + ": framework.ui_fps_hz out of range (1..120)";
            return false;
        }
        // 数据源统一为 can_socket（Unix socket 接 CAN 帧，DBC 字段名）。
        if (fw.data_source != "can_socket") {
            err = path + ": framework.data_source invalid value '" + fw.data_source
                + "'; expected can_socket";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        err = path + ": " + e.what();
        return false;
    }
}

bool validateCanIds(const std::string& path,
                    std::unordered_set<std::string>& fields,
                    std::string& err) {
    try {
        YAML::Node root = YAML::LoadFile(path);
        if (!root["can_sources"] || !root["can_sources"].IsSequence()) {
            err = path + ": missing can_sources[]";
            return false;
        }
        // 这是轻量启动门禁：保证解码表的基本唯一性/必填项，并复用 DecodeTable
        // 的 formula 编译器；不尝试证明 byte/bits、endian 或 period_ms 完全合理。
        std::unordered_set<uint32_t> ids;
        for (const auto& src : root["can_sources"]) {
            if (!src["can_id"] || !src["fields"] || !src["period_ms"]) {
                err = path + ": can_source missing can_id/fields/period_ms";
                return false;
            }
            if (!src["fields"].IsSequence()) {
                err = path + ": can_source fields must be a sequence";
                return false;
            }
            uint32_t can_id = 0;
            std::string s = src["can_id"].as<std::string>();
            if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0)
                can_id = static_cast<uint32_t>(std::stoul(s.substr(2), nullptr, 16));
            else
                can_id = src["can_id"].as<uint32_t>();
            const std::string can_source = src["name"]
                ? src["name"].as<std::string>()
                : s;
            if (ids.count(can_id)) {
                err = path + ": duplicate can_id " + s;
                return false;
            }
            ids.insert(can_id);
            for (const auto& f : src["fields"]) {
                if (!f["name"] || !f["bits"] || !f["type"]) {
                    err = path + ": field missing name/bits/type";
                    return false;
                }
                std::string name = f["name"].as<std::string>();
                if (!f["bits"].IsScalar() && !f["bits"].IsSequence()) {
                    err = path + ": field '" + name +
                          "' bits must be a scalar or sequence";
                    return false;
                }
                if (!f["type"].IsScalar()) {
                    err = path + ": field '" + name + "' type must be a scalar";
                    return false;
                }
                if (fields.count(name)) {
                    err = path + ": duplicate field name '" + name + "'";
                    return false;
                }
                if (f["formula"]) {
                    const std::string formula = f["formula"].as<std::string>();
                    if (!f["bits"].IsScalar()) {
                        err = path + ": field '" + name +
                              "' formula requires scalar integer bits";
                        return false;
                    }
                    demo::can_formula::CompiledFormula compiled;
                    std::string formula_error;
                    if (!demo::can_formula::compile(
                            formula, compiled, formula_error) ||
                        !demo::can_formula::validateRawRange(
                            compiled, f["bits"].as<int>(),
                            f["type"].as<std::string>().rfind("int", 0) == 0,
                            formula_error)) {
                        err = demo::can_formula::validationError(
                            path, can_source, name, formula, formula_error);
                        return false;
                    }
                }
                fields.insert(name);
            }
        }
        return true;
    } catch (const std::exception& e) {
        err = path + ": " + e.what();
        return false;
    }
}

bool loadIdList(const std::string& path, const char* list_key, const char* name_key,
                std::unordered_set<std::string>& out, std::string& err) {
    try {
        YAML::Node root = YAML::LoadFile(path);
        if (!root[list_key] || !root[list_key].IsSequence()) {
            err = path + ": missing " + list_key + "[]";
            return false;
        }
        if (root[list_key].size() == 0) {
            err = path + ": empty " + list_key + "[]";
            return false;
        }
        for (const auto& n : root[list_key]) {
            if (!n[name_key]) {
                err = path + ": entry missing '" + name_key + "'";
                return false;
            }
            const std::string id = n[name_key].as<std::string>();
            if (!out.insert(id).second) {
                err = path + ": duplicate " + list_key + "[]." + name_key
                    + " '" + id + "'";
                return false;
            }
        }
        return true;
    } catch (const std::exception& e) {
        err = path + ": " + e.what();
        return false;
    }
}

// 仅识别当前 DSL 中参数为字符串字面量的 setter。动态构造 id 或新增 setter 名称时，
// 必须同步扩展此校验器；regex 不是通用表达式解析器。
void extractSetterIds(const std::string& expr,
                      std::vector<std::string>& warn_refs,
                      std::vector<std::string>& light_refs) {
    static const std::regex warn_re(
        R"((?:setwarnon|setwarnoff)\s*\(\s*[\"']([^\"']+)[\"']\s*\))");
    static const std::regex light_re(
        R"((?:setlighton|setlightshine|setlightoff)\s*\(\s*[\"']([^\"']+)[\"']\s*\))");
    std::smatch m;
    std::string s = expr;
    while (std::regex_search(s, m, warn_re)) {
        warn_refs.push_back(m[1].str());
        s = m.suffix();
    }
    s = expr;
    while (std::regex_search(s, m, light_re)) {
        light_refs.push_back(m[1].str());
        s = m.suffix();
    }
}

bool validateLogic(const std::string& path,
                   const std::unordered_set<std::string>& warn_ids,
                   const std::unordered_set<std::string>& light_ids,
                   const std::unordered_set<std::string>& can_field_names,
                   std::vector<std::string>& rule_ids,
                   std::string& err) {
    try {
        YAML::Node root = YAML::LoadFile(path);
        if (!root["logics"] || !root["logics"].IsSequence()) {
            err = path + ": missing logics[]";
            return false;
        }
        if (root["logics"].size() == 0) {
            err = path + ": empty logics[]";
            return false;
        }
        std::unordered_set<std::string> seen;
        for (const auto& r : root["logics"]) {
            if (!r["id"] || !r["expr"]) {
                err = path + ": rule missing id/expr";
                return false;
            }
            std::string id = r["id"].as<std::string>();
            if (seen.count(id)) {
                err = path + ": duplicate rule id '" + id + "'";
                return false;
            }
            seen.insert(id);
            rule_ids.push_back(id);

            YAML::Node can_signals = r["can_signals"];
            if (!can_signals) {
                err = path + ": rule '" + id + "' missing can_signals";
                return false;
            }
            if (!can_signals.IsSequence()) {
                err = path + ": rule '" + id + "' can_signals must be a sequence";
                return false;
            }
            std::size_t signal_index = 0;
            for (const auto& signal_node : can_signals) {
                if (!signal_node.IsScalar()) {
                    err = path + ": rule '" + id + "' can_signals[" +
                          std::to_string(signal_index) + "] must be a scalar";
                    return false;
                }
                const std::string signal = signal_node.as<std::string>();
                if (!can_field_names.count(signal)) {
                    err = path + ": rule '" + id +
                          "' references undeclared CAN signal '" + signal + "'";
                    return false;
                }
                ++signal_index;
            }

            std::string expr = r["expr"].as<std::string>();
            std::vector<std::string> warn_refs, light_refs;
            extractSetterIds(expr, warn_refs, light_refs);
            for (const auto& w : warn_refs) {
                if (!warn_ids.count(w)) {
                    err = path + ": rule '" + id + "' references undeclared warn '" + w + "'";
                    return false;
                }
            }
            for (const auto& L : light_refs) {
                if (!light_ids.count(L)) {
                    err = path + ": rule '" + id + "' references undeclared light '" + L + "'";
                    return false;
                }
            }
        }
        return true;
    } catch (const std::exception& e) {
        err = path + ": " + e.what();
        return false;
    }
}

}  // namespace

bool ConfigLoader::load(const std::string& framework_yaml,
                        ConfigBundle& out,
                        std::string& error_out) {
    // 每次从干净默认值开始，任一步失败都返回 false；调用方不得使用部分填充 bundle。
    out = ConfigBundle{};
    if (!loadFramework(framework_yaml, out.framework, error_out))
        return false;
    if (!validateCanIds(out.framework.path_can_ids, out.can_field_names, error_out))
        return false;
    if (!loadIdList(out.framework.path_warn, "warns", "name", out.warn_ids, error_out))
        return false;
    if (!loadIdList(out.framework.path_lights, "lights", "name", out.light_ids, error_out))
        return false;
    if (!validateLogic(out.framework.path_logic, out.warn_ids, out.light_ids,
                       out.can_field_names,
                       out.logic_rule_ids, error_out))
        return false;

    std::fprintf(stderr,
        "[ConfigLoader] framework %s | tick=%dHz ui=%dHz src=%s | "
        "%zu fields, %zu rules, %zu warns, %zu lights\n",
        out.framework.version.c_str(),
        out.framework.tick_hz,
        out.framework.ui_fps_hz,
        out.framework.data_source.c_str(),
        out.can_field_names.size(),
        out.logic_rule_ids.size(),
        out.warn_ids.size(),
        out.light_ids.size());
    return true;
}

}  // namespace platform
