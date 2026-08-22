// kanzi_application.cpp — 同进程 Kanzi 宿主（CLUSTER_LOGIC_UI=kanzi，编译宏 CLUSTER_LOGIC_KANZI_UI=1）
//
// kzappfw 提供 main → createApplication()。本文件只做「落地骨架」：
//   onConfigure      加载 Kanzi 工程（kzb + application.cfg）
//   onProjectLoaded  取 #DataSource、写入初始状态、启动 ClusterBackend
//   onUpdate         把逻辑快照 flush 到 #DataSource
//
// 集成方按自身环境提供（环境变量）：
//   KANZI_KZB               Kanzi 工程二进制名（默认 demo_dashboard.kzb）
//   KANZI_WORKDIR           工程所在目录（application.cfg 与 kzb 同目录时设置）
//   CLUSTER_LOGIC_FRAMEWORK 配置文件路径（否则用后端默认 config/framework.yaml）
//   CLUSTER_LOGIC_LOG_DIR   日志目录（否则用 ./logs）
//   XDG_RUNTIME_DIR / WAYLAND_DISPLAY  显示环境（不设置则用 /tmp、wayland-0 兜底）
//
// 上屏绑定不在此文件，在 config/logic.yaml。

#include "app/cluster_backend.h"
#include "platform/kanzi/kanzi_native_binder.h"
#include "platform/logging/file_logger.h"

#include <kanzi/kanzi.hpp>

#include <clusterdatasource_module.hpp>

#include <cstdlib>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#if KZ_FREETYPE_FONT_ENGINE_LINKED
#include <kanzi/plugins/freetype/freetype_plugin.hpp>
#endif

using namespace kanzi;

namespace {

std::vector<std::string> g_arg_storage;
std::vector<char*> g_argv_ptrs;

// 读环境变量，未设置或为空则回退到 fallback（返回的指针指向静态存储）。
const char* envOr(const char* name, const char* fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? v : fallback;
}

void captureArgs(const SystemProperties& props) {
    g_arg_storage.clear();
    g_arg_storage.emplace_back("cluster-logic-kanzi");
    for (const auto& p : props) {
        g_arg_storage.emplace_back(std::string(p.data(), p.size()));
    }
    g_argv_ptrs.clear();
    for (auto& s : g_arg_storage) g_argv_ptrs.push_back(&s[0]);
}

void setIntProp(Node* node, const char* key, int value) {
    if (!node) return;
    node->setProperty(DynamicPropertyType<int>(key), value);
}

}  // namespace

class ClkKanziApplication : public Application {
public:
    void onConfigure(ApplicationProperties& configuration) override {
        ::setenv("XDG_RUNTIME_DIR", envOr("XDG_RUNTIME_DIR", "/tmp"), 1);
        ::setenv("WAYLAND_DISPLAY", envOr("WAYLAND_DISPLAY", "wayland-0"), 1);

        // 可选：切到 Kanzi 工程目录（application.cfg 与 kzb 同目录时）。
        const char* workdir = std::getenv("KANZI_WORKDIR");
        if (workdir && *workdir && ::chdir(workdir) != 0) {
            CLK_LOG_WARN("KanziApp",
                         std::string("chdir ") + workdir + " failed");
        }

        configuration.binaryName = envOr("KANZI_KZB", "demo_dashboard.kzb");
        configuration.defaultSurfaceProperties.bitsColorR = 8;
        configuration.defaultSurfaceProperties.bitsColorG = 8;
        configuration.defaultSurfaceProperties.bitsColorB = 8;
        configuration.defaultSurfaceProperties.bitsAlpha = 8;
        configuration.defaultWindowProperties.order = 10;
        configuration.defaultWindowProperties.width = 1920;
        configuration.defaultWindowProperties.height = 720;
        configuration.defaultWindowProperties.x = 0;
        configuration.defaultWindowProperties.y = 0;
        configuration.defaultSurfaceProperties.antiAliasing = 4;
        configuration.applicationIdleStateEnabled = false;
        configuration.frameRateLimit = 30;
        CLK_LOG_INFO("KanziApp", "onConfigure kzb=" +
                                     std::string(envOr("KANZI_KZB",
                                                       "demo_dashboard.kzb")));
    }

    void registerMetadataOverride(ObjectFactory& factory) override {
        Application::registerMetadataOverride(factory);
        KanziComponentsModule::registerModule(getDomain());
        ClusterDataSourceModule::registerModule(getDomain());
#if KZ_FREETYPE_FONT_ENGINE_LINKED
        FreeTypeFontEnginePlugin::registerModule(getDomain());
#endif
    }

    void initializeOverride(const SystemProperties& systemProperties) override {
        captureArgs(systemProperties);
        Application::initializeOverride(systemProperties);
    }

    void onProjectLoaded() override {
        m_dataSource = getScreen()->lookupNodeRaw("#DataSource");
        if (!m_dataSource) {
            CLK_LOG_ERROR("KanziApp", "#DataSource not found");
        } else {
            CLK_LOG_INFO("KanziApp", "#DataSource ready — seed display state");
            // 初始状态由集成方 HMI 数据模型决定；此处仅作演示占位。
            setIntProp(m_dataSource, "cluster.power_state", 2);
            setIntProp(m_dataSource, "cluster.active_page", 1);
        }

        m_store = std::make_unique<cluster::DataStore>();
        m_binder = std::make_unique<cluster::KanziNativeBinder>(m_store.get());
        m_backend = std::make_unique<cluster::ClusterBackend>();

        cluster::ClusterBackendOptions opt;
        if (!g_argv_ptrs.empty()) {
            opt = cluster::parseBackendArgs(
                static_cast<int>(g_argv_ptrs.size()), g_argv_ptrs.data());
        }
        // 配置/日志路径优先取环境变量，否则用后端默认（相对路径）。
        if (const char* fw = std::getenv("CLUSTER_LOGIC_FRAMEWORK");
            fw && *fw) {
            opt.framework = fw;
        }
        if (const char* ld = std::getenv("CLUSTER_LOGIC_LOG_DIR"); ld && *ld) {
            opt.log_dir = ld;
        }

        if (!m_backend->start(opt, m_binder.get(), m_store.get())) {
            CLK_LOG_ERROR("KanziApp", "ClusterBackend start failed");
        } else {
            CLK_LOG_INFO("KanziApp", "ClusterBackend started (in-process UI)");
        }
        platform::FileLogger::instance().flush();
    }

    void onUpdate(chrono::nanoseconds deltaTime) override {
        if (m_binder && m_dataSource) {
            m_binder->flushToDataSource(m_dataSource);
        }
        Application::onUpdate(deltaTime);
    }

    void uninitializeOverride() override {
        if (m_backend) {
            m_backend->requestStop();
            m_backend->join();
            m_backend.reset();
        }
        m_binder.reset();
        m_store.reset();
        Application::uninitializeOverride();
    }

private:
    Node* m_dataSource = nullptr;
    std::unique_ptr<cluster::DataStore> m_store;
    std::unique_ptr<cluster::KanziNativeBinder> m_binder;
    std::unique_ptr<cluster::ClusterBackend> m_backend;
};

Application* createApplication() {
    ::setenv("XDG_RUNTIME_DIR", "/tmp", 1);
    ::setenv("WAYLAND_DISPLAY", "wayland-0", 1);
    return new ClkKanziApplication();
}
