// test_config_loader.cpp — 启动期校验：正常加载 + 各类非法配置拒绝
#include "minitest.h"

#include "platform/config_loader.h"

#include "test_util.h"

namespace {

// 以绝对路径指向 fixtures，配合临时 framework 做非法用例注入。
std::string frameworkWith(const std::string& data_source, int tick_hz,
                          const std::string& can_ids_abs,
                          const std::string& logic_abs) {
    return "framework_version: \"0.3.0\"\n"
           "tick_hz: " + std::to_string(tick_hz) + "\n"
           "ui_fps_hz: 30\n"
           "data_source: \"" + data_source + "\"\n"
           "ui_backend: none\n"
           "paths:\n"
           "  can_ids: \"" + can_ids_abs + "\"\n"
           "  logic: \"" + logic_abs + "\"\n"
           "  warn: \"" + fixture("warn.yaml") + "\"\n"
           "  lights: \"" + fixture("lights.yaml") + "\"\n";
}

}  // namespace

TEST_CASE("ConfigLoader 正常加载 fixtures") {
    platform::ConfigBundle b;
    std::string err;
    REQUIRE(platform::ConfigLoader::load(fixture("framework.yaml"), b, err));

    CHECK_EQ(b.framework.data_source, "can_socket");
    CHECK_EQ(b.can_field_names.count("speed"), 1u);
    CHECK_EQ(b.can_field_names.count("scaled"), 1u);
    CHECK_EQ(b.can_field_names.size(), 5u);
    CHECK_EQ(b.logic_rule_ids.size(), 4u);
    CHECK_EQ(b.warn_ids.count("overspeed"), 1u);
    CHECK_EQ(b.light_ids.count("tt.turn_left"), 1u);
}

TEST_CASE("ConfigLoader 拒绝非法 data_source") {
    const std::string fw = frameworkWith("mcu_rpc", 10,
                                         fixture("can_ids.yaml"),
                                         fixture("logic.yaml"));
    platform::ConfigBundle b;
    std::string err;
    CHECK_FALSE(platform::ConfigLoader::load(writeTempYaml("bad_src", fw), b, err));
}

TEST_CASE("ConfigLoader 拒绝越界 tick_hz") {
    const std::string fw = frameworkWith("can_socket", 0,
                                         fixture("can_ids.yaml"),
                                         fixture("logic.yaml"));
    platform::ConfigBundle b;
    std::string err;
    CHECK_FALSE(platform::ConfigLoader::load(writeTempYaml("bad_tick", fw), b, err));
}

TEST_CASE("ConfigLoader 拒绝重复 can_id") {
    const std::string can_ids =
        "can_sources:\n"
        "  - name: A\n"
        "    can_id: '0x100'\n"
        "    period_ms: 10\n"
        "    fields:\n"
        "      - name: a\n"
        "        byte: 0\n"
        "        bits: 8\n"
        "        type: uint8\n"
        "  - name: B\n"
        "    can_id: '0x100'\n"
        "    period_ms: 10\n"
        "    fields:\n"
        "      - name: b\n"
        "        byte: 0\n"
        "        bits: 8\n"
        "        type: uint8\n";
    const std::string fw = frameworkWith("can_socket", 10,
                                         writeTempYaml("dup_can", can_ids),
                                         fixture("logic.yaml"));
    platform::ConfigBundle b;
    std::string err;
    CHECK_FALSE(platform::ConfigLoader::load(writeTempYaml("dup_fw", fw), b, err));
}

TEST_CASE("ConfigLoader 拒绝未声明的 can 信号") {
    const std::string logic =
        "logics:\n"
        "  - id: bad\n"
        "    can_signals: [ghost_signal]\n"
        "    expr: |\n"
        "      setdisplayvalue(\"cluster.x\", \"float\", ghost_signal);\n";
    const std::string fw = frameworkWith("can_socket", 10,
                                         fixture("can_ids.yaml"),
                                         writeTempYaml("undeclared", logic));
    platform::ConfigBundle b;
    std::string err;
    CHECK_FALSE(platform::ConfigLoader::load(writeTempYaml("undeclared_fw", fw), b, err));
}
