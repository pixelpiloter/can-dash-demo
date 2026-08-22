// qt_main.cpp — Qt6/QML 原型宿主

#include "app/runtime_options.h"
#include "platform/cluster_runtime.h"
#include "platform/config_catalog.h"
#include "platform/latency_probe.h"
#include "platform/logging/file_logger.h"
#include "platform/qt/qt_binder.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QtQml>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>

namespace {

std::atomic<bool> g_stop{false};

void onSignal(int) {
    g_stop.store(true);
}

const char* kHelp =
    "usage: cluster-logic-qt [options]\n"
    "  --framework PATH        framework.yaml (default: config/framework.yaml)\n"
    "  --log-dir PATH          file logger output directory\n"
    "  --can-socket            Unix socket 接 CAN 帧\n"
    "  --inject-demo           内置 DBC 字段演示\n"
    "  --replay PATH           离线信号时间线 YAML\n"
    "  --help\n";

}  // namespace

int main(int argc, char** argv) {
    auto cli = cluster::parseRuntimeCli(argc, argv, kHelp);
    if (cli.show_help) return 0;

    QGuiApplication app(argc, argv);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    cli.runtime.rx_latency_probe = true;

    platform::ClusterRuntime runtime;
    std::string err;
    if (!runtime.init(cli.runtime, err)) {
        CLK_LOG_ERROR("qt_main", std::string("FATAL: ") + err);
        platform::FileLogger::instance().flush();
        return 1;
    }

    platform::ConfigCatalog catalog;
    if (!catalog.loadWarnYaml(runtime.bundle().framework.path_warn) ||
        !catalog.loadLightYaml(runtime.bundle().framework.path_lights)) {
        CLK_LOG_WARN("qt_main",
                     "ConfigCatalog load partial/failed; UI falls back to id");
    }

    platform::QtBinder backend;
    backend.setCatalog(&catalog);
    backend.installDataSource(runtime.releaseDataSource());

    if (!runtime.start(err)) {
        CLK_LOG_ERROR("qt_main", std::string("FATAL start: ") + err);
        platform::FileLogger::instance().flush();
        return 1;
    }

    QQmlApplicationEngine qmlEngine;
    qmlRegisterType<platform::QtBinder>("ClusterLogic", 1, 0, "DashboardBackend");
    qmlEngine.addImportPath("qml");
    qmlEngine.rootContext()->setContextProperty("dashboard", &backend);
    qmlEngine.rootContext()->setContextProperty(
        "uiFpsHz", runtime.bundle().framework.ui_fps_hz);
    qmlEngine.load(QUrl(QStringLiteral("qml/DashboardMain.qml")));
    if (qmlEngine.rootObjects().isEmpty()) {
        CLK_LOG_ERROR("qt_main", "FATAL: QML load failed (cwd must be build/)");
        platform::FileLogger::instance().flush();
        return 1;
    }

    cluster::printRuntimeStartupLog("qt_main", runtime, cli.runtime);

    QTimer quitPoller;
    QObject::connect(&quitPoller, &QTimer::timeout, [&]() {
        if (g_stop.load()) app.quit();
    });
    quitPoller.start(200);

    const int code = app.exec();

    backend.stopDataSource();
    runtime.requestStop();
    runtime.join();
    CLK_LOG_INFO("qt_main", "shutdown");
    platform::FileLogger::instance().flush();
    platform::LatencyProbe::instance().printReport();
    return code;
}
