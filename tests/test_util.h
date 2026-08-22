// test_util.h — 单元测试共用小工具：fixture 路径 + 临时 YAML 落盘
#pragma once

#include <fstream>
#include <string>

inline std::string fixture(const char* name) {
    return std::string(TEST_FIXTURE_DIR) + "/" + name;
}

// 写入 /tmp 下唯一命名的临时 YAML，返回路径（进程内 tag 需唯一）。
inline std::string writeTempYaml(const std::string& tag, const std::string& content) {
    const std::string path = "/tmp/clk_test_" + tag + ".yaml";
    std::ofstream f(path, std::ios::trunc);
    f << content;
    f.close();
    return path;
}
