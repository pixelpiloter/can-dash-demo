# replay 场景 YAML 格式

供 `--replay` / `ReplaySignalSource` 使用，无需 CAN 硬件。

```yaml
loop: false          # 可选，默认 false；true 则播完后从头循环
steps:
  - at_ms: 0         # 相对进程启动的毫秒时间戳
    signals:         # 信号名 = can_ids.yaml 的 field.name
      vehicle_speed: 60
      gear_status: 3
```

示例见 `config/scenarios/demo_drive.yaml`、`tests/fixtures/replay_basic.yaml`。
