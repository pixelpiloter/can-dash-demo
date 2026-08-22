// test_warn_scheduler.cpp — 报警窗口仲裁：type / proi 优先级与抢占
#include "minitest.h"

#include "platform/warn_scheduler.h"

#include "test_util.h"

#include <unordered_map>

namespace {

platform::WarnScheduler makeScheduler() {
    const std::string yaml =
        "warns:\n"
        "  - name: w_a\n"
        "    type: 1\n"
        "    proi: 100\n"
        "  - name: w_b\n"
        "    type: 2\n"
        "    proi: 10\n"
        "  - name: w_c\n"
        "    type: 1\n"
        "    proi: 5\n"
        "  - name: w_osd\n"
        "    type: 5\n"
        "    proi: 999\n";
    const std::string path = writeTempYaml("warn_sched", yaml);

    platform::WarnScheduler s;
    std::string err;
    REQUIRE(s.loadWarnYaml(path, err));
    return s;
}

std::unordered_map<std::string, bool> activeOf(
    std::initializer_list<std::string> names) {
    std::unordered_map<std::string, bool> m;
    for (const auto& n : names) m[n] = true;
    return m;
}

}  // namespace

TEST_CASE("WarnScheduler 无报警") {
    auto s = makeScheduler();
    auto out = s.apply({});
    CHECK_EQ(out.warning_window, 0);
    CHECK(out.active_name.empty());
}

TEST_CASE("WarnScheduler 同 type 按 proi 升序") {
    auto s = makeScheduler();
    auto out = s.apply(activeOf({"w_a", "w_c"}));
    CHECK_EQ(out.active_name, "w_c");  // 同 type=1, proi 5 < 100
}

TEST_CASE("WarnScheduler type 1 优先于 type 2") {
    auto s = makeScheduler();
    auto out = s.apply(activeOf({"w_a", "w_b"}));
    CHECK_EQ(out.active_name, "w_a");
}

TEST_CASE("WarnScheduler type 5/osd 抢占") {
    auto s = makeScheduler();
    auto out = s.apply(activeOf({"w_a", "w_b", "w_c", "w_osd"}));
    CHECK_EQ(out.active_name, "w_osd");
    CHECK_EQ(out.warning_window, 1);
}
