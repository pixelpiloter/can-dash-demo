# cluster-logic-unified

[![CI](https://github.com/pixelpiloter/can-dash-demo/actions/workflows/ci.yml/badge.svg)](https://github.com/pixelpiloter/can-dash-demo/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](./LICENSE)

> **CAN 报文驱动的车载仪表盘逻辑引擎。** 仪表上显示什么、灯什么时候亮、告警什么时候弹，
> 全部用 YAML 定义 —— 改行为不写 C++。

[快速开始](#快速开始) · [它是怎么工作的](#它是怎么工作的) · [配置](#配置) · [构建](#构建) · [测试](#测试) · [License](#license)

---

## 它能做什么

一句话：**把「原始 CAN 帧」变成「仪表盘上该显示的内容」**，中间这段逻辑用 YAML 表达。

比如「车速超过 120 就弹超速告警」，这样写就够：

```yaml
- id: overspeed
  can_signals: ["vehicle_speed"]
  expr: |
    if (isovertime("vehicle_speed") == 0 and vehicle_speed > 120) {
      setwarnon("overspeed");
    } else {
      setwarnoff("overspeed");
    }
```

- `vehicle_speed` 来自 `config/can_ids.yaml`（DBC 里 0x203 帧、第 3~4 字节、缩放 `×0.1`）；
- `isovertime(...)` 处理「信号超时未到」，防止车停了还显示旧值；
- `setwarnon/off` 交给告警调度器按优先级仲裁，再推给界面。

跑起来的效果（无 UI 控制台，`--replay` 离线回放）：

```
up framework=0.3.0 data_source=can_socket tick_hz=30 ui_fps=60 source=replay replay=config/scenarios/demo_drive.yaml
mvp: health=connected speed=125 gear=4 turnL=0 turnR=0 src=1 mode=standalone
```

### 特性

- **改 YAML 不改 C++**：显示、灯、告警、超时全是规则，快速迭代。
- **三端共用一份规则**：同一套 `logic.yaml` 驱动 Qt 原型、Kanzi 嵌入式 UI、无界面控制台。
- **纯 CAN 接入**：Unix socket 收帧 → 表驱动解码 → 物理量，无私有协议。
- **跨线程安全**：采样、规则求值、上屏各自解耦，GUI 线程只做属性更新。
- **离线回放**：`--replay` 从 YAML 时间线喂信号，无需 CAN 硬件即可验证全链路。

---

## 快速开始

依赖：CMake ≥ 3.16、C++17 编译器。`yaml-cpp` 优先用系统库，没有则自动下载；`exprtk` 是单头文件库自动获取。

### 1. 无 UI 控制台（最快看到效果，不需要 Qt）

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCLUSTER_LOGIC_UI=none
cmake --build build -j
cd build && ./cluster-logic --inject-demo
```

### 2. Qt/QML 原型界面

需要 Qt6（或 Qt5）的 Core/Qml/Quick：

```bash
cmake -S . -B build-qt -DCMAKE_BUILD_TYPE=Debug -DCLUSTER_LOGIC_UI=qt
cmake --build build-qt -j
cd build-qt && ./cluster-logic-qt --inject-demo
```

### 3. 接 CAN 数据

在 `config/framework.yaml` 配置 `can_socket_path`（默认 `/tmp/cluster_logic_can.sock`），
宿主监听该路径，客户端按以下格式推送原始帧：

```
[can_id: 4 字节小端][dlc: 1 字节][data: dlc 字节]
```

```bash
./cluster-logic --can-socket
# 或离线回放（无需 CAN）：
./cluster-logic --replay config/scenarios/demo_drive.yaml
```

你的数据源（socketcan 桥、回放工具等）连上后按格式写帧即可。

---

## 什么时候用它 / 什么时候别用

**适合：**

- 车载仪表 / 数字座舱的业务逻辑，UI 后端想可替换（Kanzi / Qt / 控制台）；
- 逻辑频繁迭代、不想每次改动都重编译 C++；
- 想一套规则在 PC 原型与目标机间复用。

**不适合（诚实说明）：**

- 需要毫秒级硬实时的场景 —— 当前是 60fps 软采样，不是 RTOS 级确定性；
- 非 CAN 数据源（目前只支持 CAN socket，无私有 RPC/GID 解包链）；
- 想要拖拽式可视化配置 —— 目前是手写 YAML 文本。

---

## 它是怎么工作的

```
 ISignalSource ──► SharedState ──► LogicEngine ──► SnapshotPump ──► IDataBinder
 (inject /           最新信号缓存      logic.yaml      60fps 采样        Qt / Kanzi / 控制台
  can_socket /
  replay)
        ▲
 ClusterRuntime 统一启动（三宿主共用）
```

| 数据源 | 命令 | 说明 |
|--------|------|------|
| 内置演示 | `--inject-demo` | C++ 正弦/阶梯信号，最快看效果 |
| 离线回放 | `--replay PATH` | YAML 时间线，适合场景测试与 CI |
| CAN socket | `--can-socket` | Unix socket 接真实/桥接帧 |

| 概念 | 说明 |
|---|---|
| **信号（signal）** | 解码后的物理量，名字即 `can_ids.yaml` 的 DBC 字段名，如 `vehicle_speed` |
| **规则（rule）** | `logic.yaml` 里的一条表达式，输入若干信号，输出 display/light/warn |
| **上屏键（display key）** | `cluster.*`（主显示）、`TT.*`（指示灯）、`demo.*`（原型字段） |
| **超时（isovertime）** | 信号 `period_ms × 5` 未更新即视为超时，规则据此显示 `---` 占位 |
| **告警调度（WarnScheduler）** | 多条告警同时成立时，按类型/优先级仲裁出唯一上屏告警 |

---

## 配置

五个配置文件都在 `config/` 下，缺省路径由 `config/framework.yaml` 指定。

| 文件 | 作用 |
|---|---|
| `framework.yaml` | 启动契约：tick 频率、UI 后端、各文件路径 |
| `can_ids.yaml` | CAN 帧布局：每个 can_id 的字节/位域/端序/类型/缩放公式 |
| `logic.yaml` | 业务规则：信号 → 显示值 / 灯 / 告警 |
| `warn.yaml` | 告警表：文案、颜色、优先级等字段 |
| `lights.yaml` | 指示灯声明：名字（`TT.*`）、图标、尺寸、位置 |

### 规则内置函数

`logic.yaml` 的 `expr` 是 ExprTk 表达式，可直接写信号名当变量、做条件与算术，并内置：

| 函数 | 含义 |
|---|---|
| `setdisplayvalue(key, "float"\|"string", v)` | 写一个显示值（数值或字符串） |
| `isovertime(signal)` | 该信号是否超时（1 = 超时） |
| `setwarnon(id)` / `setwarnoff(id)` | 触发 / 解除告警（id 在 `warn.yaml`） |
| `setlighton(id)` / `setlightshine(id)` / `setlightoff(id)` | 点亮 / 闪烁 / 熄灭指示灯（id 在 `lights.yaml`） |

### 加一个新信号的最小步骤

假设要加「冷却液温度超过 110℃ 报警」：

1. 在 `can_ids.yaml` 声明信号（若 DBC 已有则直接写名字）：

```yaml
- name: coolant_temp
  byte: 2
  bits: 8
  type: uint8
  formula: x + -40
```

2. 在 `logic.yaml` 写规则：

```yaml
- id: coolant_overheat
  can_signals: ["coolant_temp"]
  expr: |
    if (isovertime("coolant_temp") == 0 and coolant_temp > 110) {
      setwarnon("coolant_overheat");
    } else {
      setwarnoff("coolant_overheat");
    }
```

3. 在 `warn.yaml` 加一条告警文案（可选，给 Qt 横幅用）。重启即可生效。

---

## 项目结构

```
config/           五个 YAML 配置
src/logic/        LogicEngine（规则引擎，核心）
src/ingress/      CAN socket 接收 + 解码表
src/platform/     配置加载、采样线程、告警调度、数据存储、日志（均无 UI 依赖）
src/platform/kanzi/  Kanzi 专用上屏出口
src/platform/qt/     Qt 专用上屏出口
src/app/          三个宿主入口：main.cpp(控制台) / qt_main.cpp(QML) / kanzi_application.cpp
src/ui/           QML 组件
tests/            单元测试（零依赖 minitest）+ fixtures
schemas/          配置结构文档
scripts/          构建脚本
```

---

## 构建

`CLUSTER_LOGIC_UI` 决定编译出哪个宿主：

| 值 | 产物 | 用途 |
|---|---|---|
| `none`（默认） | `cluster-logic` | 无 UI 控制台，验证数据源与规则 |
| `qt` | `cluster-logic-qt` | Qt6/QML 原型界面 |
| `kanzi` | `cluster-logic-kanzi` | 目标机：同进程 Kanzi Engine |

交叉编译 aarch64（需目标板 SDK）：

```bash
CLUSTER_LOGIC_UI=kanzi CLUSTER_LOGIC_AARCH64_SDK=/path/to/sdk bash scripts/cross-aarch64.sh
```

---

## 测试

测试框架是零依赖的 `tests/minitest.h`，可离线编译。构建自动产出 `clk_tests` 并接入 CTest：

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure     # smoke_help + clk_tests
./build/clk_tests                               # 直接运行
```

覆盖：CAN 公式、解码表、规则引擎、告警调度、配置校验、**ClusterRuntime + replay 端到端**。

CI（`.github/workflows/ci.yml`）在每次 push / PR 上构建并测试 `none` 与 `qt` 两种宿主。

---

## License

[MIT](./LICENSE)。欢迎按 [CONTRIBUTING.md](./CONTRIBUTING.md) 参与贡献。
