// config_loader.h
// 层: platform
// 职责: 统一加载 framework + can_ids/logic/warn/lights，做启动期的基础结构校验。
// 边界: 检查频率范围、当前构建的数据源支持、必填键、CAN/rule/warn/light
// 名称唯一性、CAN formula、can_signals 声明及 expr 中 warn/light setter 引用；
// 不替代完整 schema，不验证完整字节布局。
// 失败策略: 返回 false + 错误字符串，调用方必须 FATAL 退出
// 扩展: 换 JSON Schema 引擎时只改 validate* 实现
#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace platform {

struct FrameworkConfig {
    std::string version = "0.1.0";
    int tick_hz = 1;
    int ui_fps_hz = 60;
    std::string data_source = "can_socket";
    std::string can_socket_path = "/tmp/cluster_logic_can.sock";
    std::string ui_backend;  // none|kanzi|qt（供宿主启动校验；缺省不强制）
    std::string path_can_ids = "config/can_ids.yaml";
    std::string path_logic = "config/logic.yaml";
    std::string path_warn = "config/warn.yaml";
    std::string path_lights = "config/lights.yaml";
};

struct ConfigBundle {
    FrameworkConfig framework;
    std::unordered_set<std::string> warn_ids;
    std::unordered_set<std::string> light_ids;
    std::unordered_set<std::string> can_field_names;
    std::vector<std::string> logic_rule_ids;
};

class ConfigLoader {
public:
    // framework_yaml: 通常 "config/framework.yaml"
    // 成功填充 out；失败写 error_out
    static bool load(const std::string& framework_yaml,
                     ConfigBundle& out,
                     std::string& error_out);
};

}  // namespace platform
