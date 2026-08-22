cmake_minimum_required(VERSION 3.16)
project(cluster-logic-unified CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    add_compile_options(/bigobj /EHsc /utf-8)
endif()

# ── UI 后端选择（编译期互斥）────────────────────────────
#   none  控制台/MVP 宿主（默认，可跑全部数据源，无 UI）
#   qt    Qt6/QML 原型宿主（x86 开发或板端带 Qt）
set(CLUSTER_LOGIC_UI "none" CACHE STRING "UI backend: none|kanzi|qt")
option(CLUSTER_LOGIC_BOARD
    "Build against board Kanzi Engine SDK" OFF)

if(NOT CLUSTER_LOGIC_UI STREQUAL "none" AND
   NOT CLUSTER_LOGIC_UI STREQUAL "kanzi" AND
   NOT CLUSTER_LOGIC_UI STREQUAL "qt")
    message(FATAL_ERROR "CLUSTER_LOGIC_UI must be one of none|kanzi|qt")
endif()

find_package(yaml-cpp QUIET)
# 依赖解析优先级：系统 yaml-cpp → 仓库自带 third_party/yaml-cpp → FetchContent 下载。
# 注意：third_party/ 不在 git 仓库里（clone 后自动下载，见 README）。
if(CMAKE_CROSSCOMPILING OR NOT yaml-cpp_FOUND)
    if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/yaml-cpp/CMakeLists.txt")
        message(STATUS "Using vendored third_party/yaml-cpp (static)")
        set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
        set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
        set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
        set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        add_subdirectory(third_party/yaml-cpp EXCLUDE_FROM_ALL)
    elseif(NOT yaml-cpp_FOUND)
        message(STATUS "yaml-cpp not found; fetching via FetchContent")
        set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
        set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
        set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
        set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        include(FetchContent)
        FetchContent_Declare(yaml-cpp
            GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
            GIT_TAG 0.8.0)
        FetchContent_MakeAvailable(yaml-cpp)
    endif()
endif()

find_package(Threads REQUIRED)
# exprtk 是单头文件库：优先仓库自带 third_party/exprtk，否则 FetchContent 拉取源码目录。
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/exprtk/exprtk.hpp")
    set(EXPRTK_DIR ${CMAKE_SOURCE_DIR}/third_party/exprtk)
else()
    message(STATUS "exprtk not vendored; fetching via FetchContent")
    include(FetchContent)
    FetchContent_Declare(exprtk
        GIT_REPOSITORY https://github.com/ArashPartow/exprtk.git
        GIT_TAG master)
    FetchContent_GetProperties(exprtk)
    if(NOT exprtk_POPULATED)
        FetchContent_Populate(exprtk)
    endif()
    set(EXPRTK_DIR ${exprtk_SOURCE_DIR})
endif()

if(CLUSTER_LOGIC_UI STREQUAL "qt")
    find_package(Qt6 QUIET COMPONENTS Core Qml Quick)
    if(NOT Qt6_FOUND)
        find_package(Qt5 5.15 QUIET COMPONENTS Core Qml Quick)
    endif()
    if(NOT Qt6_FOUND AND NOT Qt5_FOUND)
        message(FATAL_ERROR "CLUSTER_LOGIC_UI=qt requires Qt6 (or Qt5) Quick")
    endif()
endif()

# ── 核心库（无 UI 依赖，两端共用）─────────────────────────
add_library(clk_core STATIC
    src/shared_state.cpp
    src/logic/logic_engine.cpp
    src/ingress/can_socket.cpp
    src/platform/config_loader.cpp
    src/platform/config_catalog.cpp
    src/platform/snapshot_pump.cpp
    src/platform/warn_scheduler.cpp
    src/platform/data_store.cpp
    src/platform/store_binder.cpp
    src/platform/kanzi/kanzi_native_binder.cpp
    src/platform/cluster_runtime.cpp
    src/platform/signal_sources/inject_signal_source.cpp
    src/platform/signal_sources/socket_signal_source.cpp
    src/platform/signal_sources/replay_signal_source.cpp
    src/platform/logging/file_logger.cpp
)
target_include_directories(clk_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/platform
    ${CMAKE_SOURCE_DIR}/src/ingress
    ${EXPRTK_DIR}
)
target_link_libraries(clk_core PUBLIC yaml-cpp Threads::Threads)

# ── 宿主目标 ──────────────────────────────────────────────
if(CLUSTER_LOGIC_UI STREQUAL "kanzi")
    add_executable(cluster-logic-kanzi
        src/app/kanzi_application.cpp
        src/app/cluster_backend.cpp
    )
    target_link_libraries(cluster-logic-kanzi PRIVATE clk_core)
    target_compile_definitions(cluster-logic-kanzi PRIVATE
        CLUSTER_LOGIC_KANZI_UI=1
        KZ_FREETYPE_FONT_ENGINE_LINKED=1
        KZ_SUPPORT_GRAPHICS_CONTEXT_API_EGL=1
        KZ_SUPPORT_GRAPHICS_API_GLES=1
        KZ_LINKED_GLES_SYMBOLS=31
        KZ_DLOAD_GLES_SYMBOLS=31
        KZ_USE_BOOST_STRING_VIEW=1
        KZ_USE_BOOST_OPTIONAL=1
        KANZI_FEATURE_3D
        LINUX
        WAYLAND
        WL_EGL_PLATFORM
        NDEBUG
    )

    set(KANZI_ENGINE_ROOT
        "${CLUSTER_LOGIC_KANZI_ENGINE}"
        CACHE PATH "Kanzi Engine root (packages/Engine)")
    if(NOT KANZI_ENGINE_ROOT OR NOT EXISTS "${KANZI_ENGINE_ROOT}/include/kanzi/kanzi.hpp")
        message(FATAL_ERROR "CLUSTER_LOGIC_UI=kanzi requires -DCLUSTER_LOGIC_KANZI_ENGINE=<path> "
                            "pointing at a Kanzi Engine package with include/kanzi/kanzi.hpp")
    endif()
    set(KANZI_LIB_DIR "${KANZI_ENGINE_ROOT}/lib/linux_wayland_aarch64/ES3_Release")
    set(KANZI_3RD_DIR "${KANZI_ENGINE_ROOT}/libraries/platforms/linux-aarch64")
    # 可选：HMI 组件库（含仪表 Application 与属性映射）
    #   -DCLUSTER_LOGIC_KANZI_HMI_ROOT=<HMI 工程根目录>
    #   -DCLUSTER_LOGIC_KANZI_COMP_LIB=<HMI 组件静态库路径>
    set(KANZI_HMI_ROOT "${CLUSTER_LOGIC_KANZI_HMI_ROOT}" CACHE PATH "HMI project root")
    set(KANZI_COMP_LIB "${CLUSTER_LOGIC_KANZI_COMP_LIB}" CACHE FILEPATH "HMI component library")

    target_include_directories(cluster-logic-kanzi PRIVATE
        ${KANZI_ENGINE_ROOT}/include
        ${KANZI_ENGINE_ROOT}/libraries/common/boost/include
        ${KANZI_ENGINE_ROOT}/libraries/common/opengl_es_2_0/include
        ${KANZI_HMI_ROOT}/Application/src/executable/src
    )
    target_link_directories(cluster-logic-kanzi PRIVATE
        ${KANZI_LIB_DIR}
        ${KANZI_3RD_DIR}/boost/lib
        ${KANZI_3RD_DIR}/zlib/lib
        ${KANZI_3RD_DIR}/libjpeg/lib
        ${KANZI_3RD_DIR}/libpng/lib
        ${KANZI_3RD_DIR}/icu/lib
        ${KANZI_3RD_DIR}/libunibreak/lib
        ${KANZI_3RD_DIR}/freetype/lib
        ${KANZI_3RD_DIR}/harfbuzz/lib
    )
    target_link_libraries(cluster-logic-kanzi PRIVATE
        -Wl,--start-group
        kzfreetypeplugin
        kzappfw
        kzui
        kzcoreui
        harfbuzz
        freetype
        unibreak
        icudata
        icui18n
        icuuc
        png
        jpeg
        z
        GLESv2
        EGL
        kzcore
        $<$<BOOL:${KANZI_COMP_LIB}>:${KANZI_COMP_LIB}>
        wayland-client
        wayland-egl
        rt
        m
        -Wl,--end-group
    )
    message(STATUS "CLUSTER_LOGIC_UI=kanzi engine=${KANZI_ENGINE_ROOT}")

elseif(CLUSTER_LOGIC_UI STREQUAL "qt")
    # Qt6/QML 原型宿主（x86 开发；同一份 logic.yaml/数据源）
    if(Qt6_FOUND)
        set(QT_PKG Qt6)
        set(CLK_QT_QT_DEFS "")
    else()
        set(QT_PKG Qt5)
        set(CLK_QT_QT_DEFS "QT_DEPRECATED_WARNINGS")
    endif()

    add_library(clk_qt STATIC
        src/platform/qt/qt_binder.cpp
    )
    target_include_directories(clk_qt PUBLIC ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(clk_qt PUBLIC
        clk_core
        ${QT_PKG}::Core
    )
    target_compile_definitions(clk_qt PRIVATE ${CLK_QT_QT_DEFS})
    set_target_properties(clk_qt PROPERTIES AUTOMOC ON)

    add_executable(cluster-logic-qt
        src/app/qt_main.cpp
        src/app/runtime_options.cpp
    )
    target_link_libraries(cluster-logic-qt PRIVATE
        clk_qt
        clk_core
        ${QT_PKG}::Qml
        ${QT_PKG}::Quick
    )
    target_compile_definitions(cluster-logic-qt PRIVATE ${CLK_QT_QT_DEFS})
    file(COPY ${CMAKE_SOURCE_DIR}/src/ui/ DESTINATION ${CMAKE_BINARY_DIR}/qml/)
    message(STATUS "CLUSTER_LOGIC_UI=qt — building cluster-logic-qt (${QT_PKG})")

else()
    # 控制台/MVP 宿主（默认）
    add_executable(cluster-logic
        src/app/main.cpp
        src/app/runtime_options.cpp
    )
    target_link_libraries(cluster-logic PRIVATE clk_core)
endif()

file(COPY ${CMAKE_SOURCE_DIR}/config/ DESTINATION ${CMAKE_BINARY_DIR}/config/)

# ── 测试 ──────────────────────────────────────────────────────
include(CTest)
if(BUILD_TESTING)
    if(NOT CLUSTER_LOGIC_UI STREQUAL "kanzi")
        if(CLUSTER_LOGIC_UI STREQUAL "qt")
            set(CLK_HELP_TARGET cluster-logic-qt)
        else()
            set(CLK_HELP_TARGET cluster-logic)
        endif()
        add_test(NAME smoke_help COMMAND $<TARGET_FILE:${CLK_HELP_TARGET}> --help)
    endif()

    # 测试框架为零依赖的 tests/minitest.h，无需 FetchContent，可离线/交叉编译。
    add_executable(clk_tests
        tests/test_main.cpp
        tests/test_can_formula.cpp
        tests/test_decode_table.cpp
        tests/test_logic_engine.cpp
        tests/test_shared_state.cpp
        tests/test_warn_scheduler.cpp
        tests/test_config_loader.cpp
        tests/test_replay_source.cpp
    )
    target_compile_definitions(clk_tests PRIVATE
        TEST_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures")
    target_link_libraries(clk_tests PRIVATE clk_core)
    add_test(NAME clk_tests COMMAND clk_tests)
endif()
