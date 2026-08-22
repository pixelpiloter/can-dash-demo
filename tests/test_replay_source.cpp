// test_replay_source.cpp — 离线回放与 ClusterRuntime 集成
#include "minitest.h"

#include "platform/cluster_runtime.h"
#include "platform/data_store.h"
#include "platform/signal_sources/replay_signal_source.h"
#include "platform/store_binder.h"

#include "test_util.h"

#include <thread>

TEST_CASE("ReplaySignalSource 加载 YAML") {
    std::vector<platform::ReplayStep> steps;
    bool loop = true;
    std::string err;
    REQUIRE(platform::ReplaySignalSource::loadYaml(
        fixture("replay_basic.yaml"), steps, loop, err));
    CHECK_EQ(steps.size(), 3u);
    CHECK(!loop);
    CHECK_EQ(steps[1].signals.at("speed"), 55.0);
}

TEST_CASE("ClusterRuntime replay 驱动上屏") {
    platform::ClusterRuntimeOptions opt;
    opt.framework_path = fixture("framework.yaml");
    opt.source = platform::SignalSourceKind::Replay;
    opt.replay_path = fixture("replay_basic.yaml");

    platform::ClusterRuntime runtime;
    std::string err;
    REQUIRE(runtime.init(opt, err));

    cluster::DataStore store;
    platform::StoreBinder binder(&store);
    runtime.setBinder(&binder);
    REQUIRE(runtime.start(err));

    std::this_thread::sleep_for(std::chrono::milliseconds(700));

    runtime.requestStop();
    runtime.join();

    CHECK_APPROX(store.getNumber("display.cluster.speed"), 110.0);
    CHECK(store.getBool("warn.overspeed"));
}
