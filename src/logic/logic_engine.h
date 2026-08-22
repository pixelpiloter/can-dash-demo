// logic_engine.h
// core/logic: runtime core (与 UI 解耦)
// 职责: yaml 驱动的纯逻辑 — tick(ctx) → display/warn/light
// 频率: 通常 1Hz (framework.tick_hz)；与 UI 60fps 解耦
// 不连: 前端 binder、CAN 驱动（只吃外部 ctx）
//
// ExprTk (字符串 id 直通):
//   - setter 用 igeneric_function；loadConfigs 前 preprocessQuotes " → '
//   - warn/light id 必须先在 warn.yaml / lights.yaml 声明 (ConfigLoader 校验)
//
// setter/query:
//   setdisplayvalue / isovertime / setwarnon|off / setlighton|shine|off

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <cstdint>
#include <mutex>
#include <optional>

// exprtk 头文件 (third_party/exprtk/exprtk.hpp, header-only)
#include <exprtk.hpp>

namespace clk {

class LogicEngine {
public:
    LogicEngine();
    ~LogicEngine();

    LogicEngine(const LogicEngine&) = delete;
    LogicEngine& operator=(const LogicEngine&) = delete;

    // ── 加载配置 ──
    // can_ids_yaml: 来自 DBC 解析的 can_sources 列表
    //   每个 can_source 的 period_ms 作为其 fields 的 cycle_ms,
    //   超时 = period_ms × 5
    // logic_yaml: 表达式规则列表, expr 里字符串字面量用 "..."
    //             (loadConfigs 内部 preprocessQuotes 转 '...')
    //
    // YAML schema 细节见 schemas/can_ids.schema.json
    bool loadConfigs(const std::string& canIdsYamlPath,
                     const std::string& logicYamlPath);

    // ── 加载 id YAML (snapshot 元数据, 不强制) ──
    // 多次调用会覆盖前一次结果 (内部先 clear 对应 map)
    // 读出来的 entry 立即写入 m_warnState / m_lightState (默认 false),
    // 保证 snapshot() 在 tick 前也能列出所有 yaml 声明的 id
    bool loadWarnYaml(const std::string& path);
    bool loadLightYaml(const std::string& path);

    using SignalUpdateTimes = std::unordered_map<std::string, int64_t>;

    // ── 每帧 tick ──
    // ctx: 信号当前值 (key: 信号名, value: double)
    // 兼容入口：ctx 中全部信号在本 tick 时刻视为刚刚到达。
    void tick(const std::unordered_map<std::string, double>& ctx);

    // 运行时入口：signal_update_ms 是各信号CAN 到达时间，单位为 monotonic
    // milliseconds，且必须与 m_clock 使用同一时间原点。map 中未列出的信号保留
    // 既有更新时间；ctx 可继续携带并使用已超时信号的最后值。
    void tick(const std::unordered_map<std::string, double>& ctx,
              const SignalUpdateTimes& signal_update_ms);

    // ── 测试用: snapshot 快照 ──
    struct DisplayEntry {
        std::string type;   // "int" / "float" / "string"
        double      num = 0.0;
        std::string str;    // type==string 时有效
    };
    struct LightState {
        bool on = false;
        bool flash = false;
    };
    struct Snapshot {
        std::unordered_map<std::string, DisplayEntry>    display_values;
        std::unordered_map<std::string, bool>            warn_state;
        std::unordered_map<std::string, LightState>      light_state;
        std::unordered_set<std::string>                   stale_signals;
        uint64_t generation = 0;
    };
    Snapshot snapshot() const;
    // generation 未变化时不复制哈希表；与 tick() 互斥，供高频 UI 采样。
    bool snapshotIfNew(uint64_t known_generation, Snapshot& out) const;

    // ── 测试用: setter 调用次数 (perf 监控) ──
    int setterCallCount(const std::string& name) const;

    // ── 测试用: 注入时钟 ──
    using Clock = std::function<int64_t()>;
    void setClock(Clock c) { m_clock = std::move(c); }

    // ── 测试用: 日志开关 (默认开, 关掉避免测试输出太乱) ──
    void setVerbose(bool on) { m_verbose = on; }

    // ── 测试用: 当前信号超时状态查询 ──
    bool isSignalTimeout(const std::string& name) const {
        return isSignalTimeout(name, m_clock ? m_clock() : 0);
    }

private:
    // ── 信号元信息 (from can_ids.yaml → DBC 派生) ──
    struct SignalInfo {
        std::string name;
        int         cycle_ms = 1000;
    };
    std::unordered_map<std::string, SignalInfo> m_signals;

    // ── 信号最近一次更新时间 (monotonic ms) ──
    std::unordered_map<std::string, int64_t> m_lastUpdateMs;

    // ── 规则列表 (每条: id + expr_source + can_signals + 编译结果) ──
    struct CompiledRule {
        std::string id;
        std::string expr_source;
        std::vector<std::string> can_signals;
        std::shared_ptr<exprtk::symbol_table<double>> syms;
        std::vector<double>      var_slots;  // 编译前 reserve, push_back 不 reallocate
        std::vector<std::string> var_names;
        std::shared_ptr<exprtk::expression<double>> expr;  // 2 参 overload 返回的 expression
    };
    std::vector<CompiledRule> m_rules;

    // ── setter spy (string-keyed, 直接用 yaml 里的 id 字符串) ──
    std::unordered_map<std::string, DisplayEntry>  m_displayValues;
    std::unordered_map<std::string, bool>          m_warnState;
    std::unordered_map<std::string, LightState>    m_lightState;
    std::unordered_set<std::string>                 m_staleSignals;

    // ── setter 调用计数 ──
    std::unordered_map<std::string, int> m_setterCalls;

    // ── exprtk igeneric_function 持有器 (生命周期 = LogicEngine) ──
    // 每个 rule 都有自己的 syms, 持有它自己的 setter instance
    // 用 shared_ptr 是因为 sym.add_function 要 reference, 内部 pointer 必须 stable
    struct SetterBundle;
    std::vector<std::shared_ptr<SetterBundle>> m_setterBundles;

    // ── tick 状态 ──
    int64_t m_nowMs = -1;
    uint64_t m_generation = 0;
    mutable std::mutex m_snapshotMutex;

    // ── 时钟 + verbose ──
    Clock m_clock;
    bool  m_verbose = true;

    // ── 内部: 编译一条规则 ──
    // 1. preprocessQuotes: 把 "foo" → 'foo' (ExprTk string literal 语法)
    // 2. 给 rule 注册 setter + 所有 can_signals 变量
    // 3. parser.compile(src, sym) 2 参 overload
    bool compileRule(CompiledRule& rule);

    // ── 内部: 判断信号是否超时 ──
    bool isSignalTimeout(const std::string& name, int64_t now_ms) const;
    void tickImpl(const std::unordered_map<std::string, double>& ctx,
                  const SignalUpdateTimes* signal_update_ms);
};

}  // namespace clk