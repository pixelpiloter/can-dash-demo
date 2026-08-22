// test_logic_engine.cpp — LogicEngine：规则编译、display/warn/light setter、超时、generation
#include "minitest.h"

#include "logic/logic_engine.h"

#include "test_util.h"

namespace {

struct EngineFixture {
    clk::LogicEngine engine;
    int64_t now = 0;

    EngineFixture() {
        engine.setVerbose(false);
        engine.setClock([this] { return now; });
        REQUIRE(engine.loadConfigs(fixture("can_ids.yaml"),
                                   fixture("logic.yaml")));
        REQUIRE(engine.loadWarnYaml(fixture("warn.yaml")));
        REQUIRE(engine.loadLightYaml(fixture("lights.yaml")));
    }
};

}  // namespace

TEST_CASE("LogicEngine 基础 setter 与上屏值") {
    EngineFixture fx;

    fx.now = 0;
    fx.engine.tick({{"speed", 50.0}, {"temp", -3.0}});
    auto s = fx.engine.snapshot();

    CHECK_EQ(s.display_values.at("cluster.speed").type, "float");
    CHECK_APPROX(s.display_values.at("cluster.speed").num, 50.0);
    CHECK_APPROX(s.display_values.at("cluster.temp").num, -3.0);
    CHECK_FALSE(s.warn_state.at("overspeed"));
    CHECK_FALSE(s.light_state.at("tt.turn_left").on);  // speed 50 ≥ 5
}

TEST_CASE("LogicEngine warn / light 随值切换") {
    EngineFixture fx;

    fx.now = 0;
    fx.engine.tick({{"speed", 150.0}});
    CHECK(fx.engine.snapshot().warn_state.at("overspeed"));

    fx.now = 1;
    fx.engine.tick({{"speed", 2.0}});
    auto s = fx.engine.snapshot();
    CHECK_FALSE(s.warn_state.at("overspeed"));
    CHECK(s.light_state.at("tt.turn_left").on);
}

TEST_CASE("LogicEngine isovertime 超时占位") {
    EngineFixture fx;

    fx.now = 0;
    fx.engine.tick({{"speed", 50.0}});
    CHECK_EQ(fx.engine.snapshot().display_values.at("cluster.speed").type,
             "float");

    // speed cycle_ms=100 → 超时阈值 500ms；时钟推进后空 ctx 触发超时
    fx.now = 1000;
    fx.engine.tick({});
    auto s = fx.engine.snapshot();
    CHECK_EQ(s.display_values.at("cluster.speed").type, "string");
    CHECK_EQ(s.display_values.at("cluster.speed").str, "---");
}

TEST_CASE("LogicEngine snapshotIfNew generation 门控") {
    EngineFixture fx;

    fx.now = 0;
    fx.engine.tick({{"speed", 1.0}});

    const uint64_t g0 = fx.engine.snapshot().generation;
    clk::LogicEngine::Snapshot out;
    CHECK_FALSE(fx.engine.snapshotIfNew(g0, out));

    fx.now = 1;
    fx.engine.tick({{"speed", 2.0}});
    CHECK(fx.engine.snapshotIfNew(g0, out));
    CHECK_APPROX(out.display_values.at("cluster.speed").num, 2.0);
}

TEST_CASE("LogicEngine 未声明变量导致编译失败并回滚") {
    // ghost_var 既不在 can_ids.yaml 也不在 can_signals，exprtk 编译应失败。
    const std::string yaml =
        "logics:\n"
        "  - id: bad_rule\n"
        "    can_signals: [speed]\n"
        "    expr: |\n"
        "      setdisplayvalue(\"cluster.x\", \"float\", ghost_var);\n";
    const std::string path = writeTempYaml("bad_logic", yaml);

    clk::LogicEngine engine;
    engine.setVerbose(false);
    CHECK_FALSE(engine.loadConfigs(fixture("can_ids.yaml"), path));
}
