# 数据链路与模块（同进程 Kanzi）

产品路径：`CLUSTER_LOGIC_UI=kanzi`，数据源为 CAN 报文（`--can-socket` / `--inject-demo`）。
开发验证走 `CLUSTER_LOGIC_UI=qt`（QtBinder → QML），控制台走 `CLUSTER_LOGIC_UI=none`。

## 设计原则

落地后**尽量只改 yaml**：

| 改什么 | 改哪个 yaml |
|--------|-------------|
| CAN 报文 / 位域 / 缩放 | `config/can_ids.yaml`（DBC 字段名） |
| 业务规则 / **上屏绑定** | `config/logic.yaml`（`setdisplayvalue` / `setlight*` 的 id = Kanzi 属性名） |
| 灯声明 | `config/lights.yaml`（TT 名与 Kanzi 一致） |
| 告警文案/优先级 | `config/warn.yaml` |

信号名全程用 DBC 字段名（`can_ids.yaml` 派生）。

## 链路

```
ISignalSource（inject / can_socket / replay）
  → SharedState
  → LogicEngine (logic.yaml)
  → SnapshotPump
  → IDataBinder（Qt / Kanzi / Store）
  → UI
```

三宿主共用 `platform::ClusterRuntime` 启动上述流水线；`--replay` 从 YAML 时间线离线喂信号。

## 链路（CAN socket）

```
CAN 帧 (Unix socket)
  → CanSocketServer          收帧
  → DecodeTable(can_ids.yaml) 表驱动解包，字段名原样（DBC 名）
  → SharedState.ctx
  → LogicEngine (logic.yaml)  setdisplayvalue("cluster.speed", …)
  → SnapshotPump
  → KanziNativeBinder         通用刷 display/light（键含 '.'）
  → #DataSource
```

## 上屏示例（logic.yaml）

```yaml
- id: ui_speed
  can_signals: ["vehicle_speed"]
  expr: |
    setdisplayvalue("cluster.speed", "float", vehicle_speed);

- id: ui_turn_left
  can_signals: ["brake", "vehicle_speed"]
  expr: |
    if (vehicle_speed < 5) { setlighton("tt.turn_left"); }
    else { setlightoff("tt.turn_left"); }
```

## 嵌入式部署

交叉编译：`CLUSTER_LOGIC_UI=kanzi bash scripts/cross-aarch64.sh`（需目标板 SDK，
经 `CLUSTER_LOGIC_AARCH64_SDK` 指定），产物为 `build-aarch64-kanzi/cluster-logic-kanzi`。
部署后以 `--can-socket`、`--inject-demo` 或 `--replay` 启动，具体启动脚本由集成方自备。
