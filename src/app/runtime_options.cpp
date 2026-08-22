#include "app/runtime_options.h"

#include "platform/logging/file_logger.h"

#include <cstring>
#include <sstream>

namespace cluster {

RuntimeCliOptions parseRuntimeCli(int argc, char** argv, const char* help_text) {
    RuntimeCliOptions out;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--framework") == 0 && i + 1 < argc) {
            out.runtime.framework_path = argv[++i];
        } else if (std::strcmp(argv[i], "--log-dir") == 0 && i + 1 < argc) {
            out.runtime.log_dir = argv[++i];
        } else if (std::strcmp(argv[i], "--inject-demo") == 0) {
            out.runtime.source = platform::SignalSourceKind::InjectDemo;
        } else if (std::strcmp(argv[i], "--can-socket") == 0) {
            out.runtime.source = platform::SignalSourceKind::CanSocket;
        } else if (std::strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
            out.runtime.source = platform::SignalSourceKind::Replay;
            out.runtime.replay_path = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0) {
            out.show_help = true;
        }
    }
    if (out.show_help && help_text) {
        std::printf("%s", help_text);
    }
    return out;
}

void printRuntimeStartupLog(const char* tag,
                            const platform::ClusterRuntime& rt,
                            const platform::ClusterRuntimeOptions& opt) {
    std::ostringstream oss;
    oss << "up framework=" << rt.bundle().framework.version
        << " data_source=" << rt.bundle().framework.data_source
        << " tick_hz=" << rt.bundle().framework.tick_hz
        << " ui_fps=" << rt.bundle().framework.ui_fps_hz
        << " source=" << rt.sourceName();
    if (opt.source == platform::SignalSourceKind::Replay) {
        oss << " replay=" << opt.replay_path;
    }
    CLK_LOG_INFO(tag, oss.str());
    platform::FileLogger::instance().flush();
}

}  // namespace cluster
