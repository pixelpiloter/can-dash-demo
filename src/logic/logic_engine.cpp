// logic_engine.cpp
// core/logic: 纯逻辑引擎实现 (字符串 id 直通 ExprTk)
//
// 设计: 7 个 setter + 1 个返回值函数 (isovertime) 全部以
//       igeneric_function<double> 子类化, 字符串字面量直接喂给 ExprTk.
//       compileRule 之前 preprocessQuotes 把 "foo" → 'foo' 转换.

#include "logic/logic_engine.h"

#include <exprtk.hpp>
#include <yaml-cpp/yaml.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace clk {

// helper: ExprTk type_store (string 模式) → std::string 拷贝
// type_store::data 在 string 模式下是 char*, size 是字节数.
// string_view 是 type_view<char>, 不能从 type_store 隐式转换, 这里显式构造.
static inline std::string copy_str(const exprtk::type_store<double>& ts) {
    if (ts.type != exprtk::type_store<double>::e_string) return {};
    return std::string(reinterpret_cast<const char*>(ts.data), ts.size);
}

// ── SetterBundle: 一条 rule 用的全部 setter, 统一 lifetime ──
// 7 个 igeneric_function<double> 子类, 每个绑到 LogicEngine 的 this (raw ptr,
// 因为 SetterBundle 生命周期 = LogicEngine)
//
// 签名约定:
//   setdisplayvalue(name, type, value) → SSS
//   isovertime(name)                   → S
//   setwarnon/off(name)                → S
//   setlighton/shine/off(name)         → S
struct LogicEngine::SetterBundle {
    // ── setdisplayvalue(name, type, value) ──
    // 签名 'SSS|SST':
    //   - SSS: setdisplayvalue(name_str, type_str, value_str) — value 是 string 字面
    //         例: setdisplayvalue("bat_volt_disp", "string", "--")
    //   - SST: setdisplayvalue(name_str, type_str, value_scalar) — value 是 double
    //         例: setdisplayvalue("bat_volt_disp", "float", bat_volt)
    // ExprTk 按调用点的实际参数类型分派, callback 内部根据 params[2].type 决定走哪条
    //
    // 注意: union 签名 (|) 让 ExprTk 走 multimode_genfunction_node, 调用的是
    //       `operator()(const std::size_t& psi, parameter_list_t)` (2 参版本),
    //       不是单参 `operator()(parameter_list_t)`. 所以必须同时 override 两个重载,
    //       否则 2 参版本走 igeneric_function 基类 default empty_body,返回 NaN.
    //       (单签名 setter 不受影响, 它们走 generic_function_node 调单参版本.)
    struct SetDisplayValue : public exprtk::igeneric_function<double> {
        LogicEngine* eng;
        explicit SetDisplayValue(LogicEngine* e)
            : exprtk::igeneric_function<double>("SSS|SST"), eng(e) {}
        // shared 逻辑: 实际写 m_displayValues 的工作
        void write(parameter_list_t params) {
            std::string name = copy_str(params[0]);
            std::string type = copy_str(params[1]);
            ++eng->m_setterCalls["setdisplayvalue"];
            DisplayEntry v;
            v.type = type;
            if (params[2].type == exprtk::type_store<double>::e_string) {
                std::string val = copy_str(params[2]);
                if (eng->m_verbose)
                    std::printf("  [setter] setdisplayvalue(\"%s\", \"%s\", \"%s\")\n",
                                name.c_str(), type.c_str(), val.c_str());
                v.str = val;
                if (type == "int") {
                    try { v.num = std::stoll(val); } catch (...) { v.num = 0; }
                } else {
                    v.num = 0.0;
                }
            } else {
                exprtk::type_store<double>::scalar_view sv(params[2]);
                double num = sv();
                if (eng->m_verbose)
                    std::printf("  [setter] setdisplayvalue(\"%s\", \"%s\", %.1f)\n",
                                name.c_str(), type.c_str(), num);
                v.num = num;
                v.str = std::to_string(num);
            }
            eng->m_displayValues[name] = v;
        }
        // 单签名路径 (paramseq_count==1) — 兼容纯 SSS 或纯 SST 单签名
        double operator()(parameter_list_t params) override {
            write(params);
            return 0.0;
        }
        // union 路径 (paramseq_count>1, multimode_genfunction_node 调) — SSS|SST 必须
        double operator()(const std::size_t&, parameter_list_t params) override {
            write(params);
            return 0.0;
        }
    } setdisplayvalue;

    // ── isovertime(name) ──
    struct Isovertime : public exprtk::igeneric_function<double> {
        LogicEngine* eng;
        explicit Isovertime(LogicEngine* e)
            : exprtk::igeneric_function<double>("S"), eng(e) {}
        double operator()(parameter_list_t params) override {
            std::string name = copy_str(params[0]);
            ++eng->m_setterCalls["isovertime"];
            auto it = eng->m_lastUpdateMs.find(name);
            if (it == eng->m_lastUpdateMs.end()) {
                if (eng->m_verbose)
                    std::printf("  [setter] isovertime(\"%s\") → 1.0 (从未收到)\n",
                                name.c_str());
                return 1.0;
            }
            auto sit = eng->m_signals.find(name);
            if (sit == eng->m_signals.end()) {
                if (eng->m_verbose)
                    std::printf("  [setter] isovertime(\"%s\") → 1.0 (未知信号)\n",
                                name.c_str());
                return 1.0;
            }
            int timeout_ms = sit->second.cycle_ms * 5;
            bool timed = (eng->m_nowMs - it->second) > timeout_ms;
            if (eng->m_verbose)
                std::printf("  [setter] isovertime(\"%s\") → %.0f (Δ=%ldms, th=%dms)\n",
                            name.c_str(), timed ? 1.0 : 0.0,
                            static_cast<long>(eng->m_nowMs - it->second),
                            timeout_ms);
            return timed ? 1.0 : 0.0;
        }
    } isovertime;

    // ── setwarnon/off(name) ──
    struct SetWarn : public exprtk::igeneric_function<double> {
        LogicEngine* eng;
        bool on;
        const char* name;
        SetWarn(LogicEngine* e, bool on_, const char* n)
            : exprtk::igeneric_function<double>("S"), eng(e), on(on_), name(n) {}
        double operator()(parameter_list_t params) override {
            std::string id = copy_str(params[0]);
            ++eng->m_setterCalls[name];
            if (eng->m_verbose)
                std::printf("  [setter] %s(\"%s\")\n", name, id.c_str());
            eng->m_warnState[id] = on;
            return 0.0;
        }
    } setwarnon, setwarnoff;

    // ── setlighton/off/shine(name) ──
    struct SetLight : public exprtk::igeneric_function<double> {
        LogicEngine* eng;
        bool on;
        bool flash;
        const char* name;
        SetLight(LogicEngine* e, bool on_, bool flash_, const char* n)
            : exprtk::igeneric_function<double>("S"), eng(e),
              on(on_), flash(flash_), name(n) {}
        double operator()(parameter_list_t params) override {
            std::string id = copy_str(params[0]);
            ++eng->m_setterCalls[name];
            if (eng->m_verbose)
                std::printf("  [setter] %s(\"%s\")\n", name, id.c_str());
            eng->m_lightState[id].on    = on;
            eng->m_lightState[id].flash = flash;
            return 0.0;
        }
    } setlighton, setlightshine, setlightoff;

    SetterBundle(LogicEngine* e)
        : setdisplayvalue(e),
          isovertime(e),
          setwarnon(e, true,  "setwarnon"),
          setwarnoff(e, false, "setwarnoff"),
          setlighton(e, true,  false, "setlighton"),
          setlightshine(e, true,  true,  "setlightshine"),
          setlightoff(e, false, false, "setlightoff")
    {}
};

static int64_t defaultClockMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()).count();
}

LogicEngine::LogicEngine() : m_clock(defaultClockMs) {}
LogicEngine::~LogicEngine() = default;

// ── 加载 warn.yaml ──
// 把 yaml 里声明的所有 warn id 写进 m_warnState (默认 false),
// 保证 snapshot() 在 tick 前也能列出所有声明的 id
bool LogicEngine::loadWarnYaml(const std::string& path) {
    try {
        YAML::Node root = YAML::LoadFile(path);
        m_warnState.clear();
        if (!root["warns"]) return true;
        for (const auto& w : root["warns"])
            m_warnState[w["name"].as<std::string>()] = false;
        if (m_verbose)
            std::printf("[LogicEngine] warn.yaml loaded %zu ids\n", m_warnState.size());
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[LogicEngine] warn.yaml load FAIL: %s\n", e.what());
        return false;
    }
}

bool LogicEngine::loadLightYaml(const std::string& path) {
    try {
        YAML::Node root = YAML::LoadFile(path);
        m_lightState.clear();
        if (!root["lights"]) return true;
        for (const auto& l : root["lights"]) {
            LightState st;
            st.on = false; st.flash = false;
            m_lightState[l["name"].as<std::string>()] = st;
        }
        if (m_verbose)
            std::printf("[LogicEngine] lights.yaml loaded %zu ids\n", m_lightState.size());
        return true;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[LogicEngine] lights.yaml load FAIL: %s\n", e.what());
        return false;
    }
}

bool LogicEngine::loadConfigs(const std::string& canIdsYamlPath,
                              const std::string& logicYamlPath) {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);

    // 配置加载采用事务语义：解析或编译失败时继续使用上一份完整配置，
    // 避免 m_rules 中留下 expr 为空的半编译规则。
    auto previousSignals = std::move(m_signals);
    auto previousRules = std::move(m_rules);
    auto previousSetterBundles = std::move(m_setterBundles);
    auto rollback = [&]() {
        m_signals = std::move(previousSignals);
        m_rules = std::move(previousRules);
        m_setterBundles = std::move(previousSetterBundles);
    };

    // can_ids.yaml → m_signals (DBC 派生的 can_sources 结构)
    try {
        YAML::Node root = YAML::LoadFile(canIdsYamlPath);
        m_signals.clear();
        for (const auto& src : root["can_sources"]) {
            int period_ms = 0;
            if (!src["period_ms"])
                throw std::runtime_error(
                    "can_source missing required 'period_ms' field");
            period_ms = src["period_ms"].as<int>();
            for (const auto& f : src["fields"]) {
                SignalInfo si;
                si.name = f["name"].as<std::string>();
                si.cycle_ms = period_ms;
                m_signals[si.name] = si;
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[LogicEngine] can_ids.yaml load FAIL: %s\n", e.what());
        rollback();
        return false;
    }

    // logic.yaml → m_rules
    // 必须 emplace_back 后再 compileRule, 保证 rule syms/slot 地址稳定
    try {
        YAML::Node logic = YAML::LoadFile(logicYamlPath);
        m_rules.clear();
        m_setterBundles.clear();
        for (const auto& r : logic["logics"]) {
            CompiledRule rule;
            rule.id = r["id"].as<std::string>();
            rule.expr_source = r["expr"].as<std::string>();
            for (const auto& s : r["can_signals"])
                rule.can_signals.push_back(s.as<std::string>());
            m_rules.push_back(std::move(rule));
            if (!compileRule(m_rules.back())) {
                rollback();
                return false;
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[LogicEngine] logic.yaml load FAIL: %s\n", e.what());
        rollback();
        return false;
    }

    if (m_verbose)
        std::printf("[LogicEngine] loaded %zu signals, %zu rules\n",
                    m_signals.size(), m_rules.size());
    return true;
}

// ── preprocessQuotes: 把 expr 里所有 "..." 替换成 '...' ──
// ExprTk 不支持双引号字面量, 但 yaml 用双引号更自然, 所以预处理一层.
// 只动 "..." 形式, '...' 视为已转换过直接放行, 数字字面量也不动.
static std::string preprocessQuotes(const std::string& src) {
    std::string out;
    out.reserve(src.size());
    bool in_str = false;
    for (size_t i = 0; i < src.size(); ++i) {
        char c = src[i];
        if (!in_str) {
            if (c == '"') {
                // start string literal: 输出 ' (而不是 ")
                in_str = true;
                out += '\'';
            } else {
                out += c;
            }
        } else {
            if (c == '"') {
                in_str = false;
                out += '\'';
            } else {
                out += c;
            }
        }
    }
    return out;
}

// 编译一条规则 ──
bool LogicEngine::compileRule(CompiledRule& rule) {
    // 1. preprocessQuotes: "foo" → 'foo'
    rule.expr_source = preprocessQuotes(rule.expr_source);
    if (m_verbose)
        std::printf("[LogicEngine] rule '%s' preprocessed expr:\n  %s\n",
                    rule.id.c_str(), rule.expr_source.c_str());

    // 2. 准备 syms + var_slots (vector 提前 reserve, address 永久稳定)
    rule.syms = std::make_shared<exprtk::symbol_table<double>>();
    auto bind = [&](const std::string& name) {
        if (std::find(rule.var_names.begin(), rule.var_names.end(), name)
            != rule.var_names.end()) return;
        rule.var_names.push_back(name);
    };
    for (const auto& s : rule.can_signals) bind(s);
    for (const auto& [n, _] : m_signals)    bind(n);

    rule.var_slots.reserve(rule.var_names.size());
    for (const auto& n : rule.var_names) {
        rule.var_slots.push_back(0.0);
        rule.syms->add_variable(n, rule.var_slots.back());
    }

    // 3. 注册 setter/query (igeneric_function<double>)
    auto bundle = std::make_shared<SetterBundle>(this);
    rule.syms->add_function("setdisplayvalue", bundle->setdisplayvalue);
    rule.syms->add_function("isovertime",      bundle->isovertime);
    rule.syms->add_function("setwarnon",       bundle->setwarnon);
    rule.syms->add_function("setwarnoff",      bundle->setwarnoff);
    rule.syms->add_function("setlighton",      bundle->setlighton);
    rule.syms->add_function("setlightshine",   bundle->setlightshine);
    rule.syms->add_function("setlightoff",     bundle->setlightoff);
    m_setterBundles.push_back(bundle);

    // 4. compile: 两步法 — 1参 overload 探测错误, 2参 overload 拿 expression
    //   1参 overload `bool compile(string, expression&)` 失败时 set_error,
    //     parser.error() 可读. 失败就立刻退出, 错误诊断清晰.
    //   2参 overload `expression_t compile(string, symbol_table&)` 失败信号
    //     不可靠 (parser.error() 在某些错误路径上返回 "No Error"), 但成功时
    //     直接返回已注册的 expression, 不需要我们手动 register_symbol_table.
    //   两步法的代价: 同一 expr 编译两次. 因为 rule.expr_source 是 const&,
    //     1参 overload 不会修改 parser state 干扰 2参, 所以两次 compile 安全.
    exprtk::parser<double> parser;
    exprtk::expression<double> probe;
    probe.register_symbol_table(*rule.syms);
    if (!parser.compile(rule.expr_source, probe)) {
        std::fprintf(stderr, "[LogicEngine] compile FAIL in '%s': %s\n",
                     rule.id.c_str(), parser.error().c_str());
        return false;
    }
    auto compiled = parser.compile(rule.expr_source, *rule.syms);
    rule.expr = std::make_shared<exprtk::expression<double>>(std::move(compiled));
    return true;
}

// ── tick ──
void LogicEngine::tick(const std::unordered_map<std::string, double>& ctx) {
    tickImpl(ctx, nullptr);
}

void LogicEngine::tick(
    const std::unordered_map<std::string, double>& ctx,
    const SignalUpdateTimes& signal_update_ms) {
    tickImpl(ctx, &signal_update_ms);
}

void LogicEngine::tickImpl(
    const std::unordered_map<std::string, double>& ctx,
    const SignalUpdateTimes* signal_update_ms) {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    const int64_t now = m_clock ? m_clock() : defaultClockMs();
    m_nowMs = now;

    // 1. 更新逐信号到达时间。兼容入口仍把本次 ctx 的所有值视为刚刚到达；
    // 运行时入口使用 CAN 接收线程记录的monotonic milliseconds。
    if (signal_update_ms != nullptr) {
        for (const auto& [name, update_ms] : *signal_update_ms)
            m_lastUpdateMs[name] = update_ms;
    } else {
        for (const auto& [name, _] : ctx)
            m_lastUpdateMs[name] = now;
    }

    // 2. 在快照锁内发布逐信号 freshness。配置中从未收到的信号和更新时间表里
    // 的未知信号都沿用 isovertime() 语义标为 stale。
    m_staleSignals.clear();
    m_staleSignals.reserve(m_signals.size());
    for (const auto& [name, _] : m_signals) {
        if (isSignalTimeout(name, now)) m_staleSignals.insert(name);
    }
    for (const auto& [name, _] : m_lastUpdateMs) {
        if (m_signals.find(name) == m_signals.end()) m_staleSignals.insert(name);
    }

    // 3. 同步当前/保留值到 expr 变量槽；是否 fresh 只由上面的时间戳决定。
    for (const auto& [name, val] : ctx) {
        for (auto& rule : m_rules) {
            auto it = std::find(rule.var_names.begin(), rule.var_names.end(), name);
            if (it == rule.var_names.end()) continue;
            rule.var_slots[it - rule.var_names.begin()] = val;
        }
    }

    if (m_verbose)
        std::printf("[tick @ %ldms] ctx has %zu signals, freshness has %zu\n",
                    now, ctx.size(),
                    signal_update_ms != nullptr ? signal_update_ms->size()
                                                : ctx.size());
    for (const auto& rule : m_rules) {
        if (m_verbose) {
            bool any_timeout = false;
            for (const auto& sig : rule.can_signals)
                if (isSignalTimeout(sig, now)) { any_timeout = true; break; }
            std::printf("  [rule] %s%s\n", rule.id.c_str(),
                        any_timeout ? " (TIMEOUT)" : "");
        }
        rule.expr->value();
    }
    ++m_generation;
}

bool LogicEngine::isSignalTimeout(const std::string& name, int64_t now_ms) const {
    auto it = m_lastUpdateMs.find(name);
    if (it == m_lastUpdateMs.end()) return true;
    auto sit = m_signals.find(name);
    if (sit == m_signals.end()) return true;
    int timeout_ms = sit->second.cycle_ms * 5;
    return (now_ms - it->second) > timeout_ms;
}

LogicEngine::Snapshot LogicEngine::snapshot() const {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    return Snapshot{
        m_displayValues, m_warnState, m_lightState, m_staleSignals, m_generation};
}

bool LogicEngine::snapshotIfNew(uint64_t known_generation, Snapshot& out) const {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    if (known_generation == m_generation) return false;
    out = Snapshot{
        m_displayValues, m_warnState, m_lightState, m_staleSignals, m_generation};
    return true;
}

int LogicEngine::setterCallCount(const std::string& name) const {
    auto it = m_setterCalls.find(name);
    return it != m_setterCalls.end() ? it->second : 0;
}

}  // namespace clk