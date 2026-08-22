#pragma once

#include "platform/cluster_runtime.h"

namespace cluster {

struct RuntimeCliOptions {
    platform::ClusterRuntimeOptions runtime;
    bool show_help = false;
};

RuntimeCliOptions parseRuntimeCli(int argc, char** argv, const char* help_text);

void printRuntimeStartupLog(const char* tag,
                            const platform::ClusterRuntime& rt,
                            const platform::ClusterRuntimeOptions& opt);

}  // namespace cluster
