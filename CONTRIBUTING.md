# Contributing

感谢关注 cluster-logic-unified。这是一个把车载仪表盘逻辑抽成**数据源无关的规则引擎**
的开源工程：CAN 报文 → `DecodeTable` 解包 → `LogicEngine`（YAML 规则）→
`SnapshotPump` → 上屏绑定（Kanzi / Qt）。

## 快速上手

```bash
# 1. 构建控制台宿主 + 单元测试（无需 Qt）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCLUSTER_LOGIC_UI=none
cmake --build build -j

# 2. 跑测试
ctest --test-dir build --output-on-failure

# 3. 跑内置演示（DBC 字段名直接驱动规则上屏）
(cd build && ./cluster-logic --inject-demo)
```

Qt 原型宿主：

```bash
cmake -S . -B build-qt -DCMAKE_BUILD_TYPE=Debug -DCLUSTER_LOGIC_UI=qt
cmake --build build-qt -j
```

## 代码风格

- C++17，无 RTTI 无关紧要；遵循现有命名：类 `PascalCase`、方法 `camelCase`、
  成员 `m_` 前缀、文件 `snake_case`。
- 核心逻辑（`src/logic`、`src/ingress`、`src/platform`）必须保持**无 Qt / 无 Kanzi
  依赖**，以便三个宿主共用并可直接单测。
- 派生状态尽量下沉到 `config/logic.yaml` 规则，而不是写进 C++。
- 注释只解释「为什么」，不叙述代码在做什么；中文注释沿用现状即可。

## 提交规范

- 每个 commit 只做一件事；信息写清楚动机而非罗列改动。
- 涉及规则/配置变更时，同步更新 `schemas/` 下的文档。
- 新增逻辑请补对应单元测试（`tests/`），并确认 `ctest` 全绿。

## 测试

测试框架为零依赖的 `tests/minitest.h`（无需网络下载，可离线/交叉编译）。测试放在
`tests/`，夹具放在 `tests/fixtures/`：

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure          # 全量
./build/clk_tests                                    # 直接运行
```

`-DBUILD_TESTING=OFF` 可关闭测试构建（交叉编译时常用）。

## CI

`.github/workflows/ci.yml` 在 push / PR 上构建并测试 `none` 与 `qt` 两个宿主。
