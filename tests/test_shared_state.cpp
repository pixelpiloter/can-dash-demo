// test_shared_state.cpp — SharedState::publishSignals 的在线标记与 generation
#include "minitest.h"

#include "shared_state.h"

TEST_CASE("publishSignals 在线标记") {
    cluster::SharedState s;
    cluster::publishSignals(s, {{"a", 1.0}}, 100, true);
    CHECK(s.source_online);
    CHECK_APPROX(s.ctx.at("a"), 1.0);

    cluster::publishSignals(s, {}, 200, false);
    CHECK_FALSE(s.source_online);
}

TEST_CASE("publishSignals generation 仅在值变化时递增") {
    cluster::SharedState s;

    cluster::publishSignals(s, {{"a", 1.0}}, 100, true);
    const uint64_t g1 = s.generation;

    cluster::publishSignals(s, {{"a", 1.0}}, 200, true);  // 同值 → 不 bump
    CHECK_EQ(s.generation, g1);

    cluster::publishSignals(s, {{"a", 2.0}}, 300, true);  // 变值 → bump
    CHECK_EQ(s.generation, g1 + 1);

    cluster::publishSignals(s, {{"b", 9.0}}, 400, true);  // 新键 → bump
    CHECK_EQ(s.generation, g1 + 2);
}
