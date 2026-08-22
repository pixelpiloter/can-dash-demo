// config_catalog.h
// 项目 YAML 元数据目录：LogicEngine 只发布 id/状态，catalog 为 UI 补充可选文案、
// 颜色和布局。缺少单项元数据时查询返回 nullptr，由 Binder 回退到 id/default；
// 文件不可读或顶层结构错误则 load* 返回 false，是否终止启动由组装层决定。

#pragma once

#include <string>
#include <unordered_map>

namespace platform {

struct WarnMeta {
    std::string text_zh;
    std::string text_en;
    std::string color;      // "#RRGGBB"
    std::string priority;   // "A".."D"
};

struct LightMeta {
    std::string image;
    int width = 0;
    int height = 0;
    int pos_x = 0;
    int pos_y = 0;
};

class ConfigCatalog {
public:
    bool loadWarnYaml(const std::string& path);
    bool loadLightYaml(const std::string& path);

    const WarnMeta*  warnMeta(const std::string& id) const;
    const LightMeta* lightMeta(const std::string& id) const;

    const std::unordered_map<std::string, WarnMeta>&  warns() const { return m_warns; }
    const std::unordered_map<std::string, LightMeta>& lights() const { return m_lights; }

private:
    std::unordered_map<std::string, WarnMeta>  m_warns;
    std::unordered_map<std::string, LightMeta> m_lights;
};

}  // namespace platform
