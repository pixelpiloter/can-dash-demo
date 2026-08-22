// can_frame.h
// 层: runtime core — DecodeTable
// 职责: can_ids.yaml → 报文布局表；帧到达后解码为 ctx[field]=物理值
// 布局源自 config/demo_vehicle.dbc，转写为 config/can_ids.yaml（信号名/字节/位域/公式）。
//
// 数据流:
//   load(can_ids) → m_messages[can_id]
//   decode(frame, ctx) → extractRaw → formula → ctx[name]

#pragma once

#include "can_formula.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace demo {

struct CanFrame {
    uint32_t can_id = 0;
    uint8_t  dlc    = 0;
    uint8_t  data[8] = {};

    // 帧头格式常量 (跟 engine.py: msg = struct.pack("<IB", can_id, len(data)) + data)
    static constexpr size_t HEADER_SIZE = 5;  // 4 bytes id + 1 byte dlc
};

// ── 单字段解码描述 (从 can_ids.yaml can_sources[].fields[] 解析) ──
struct FieldSpec {
    std::string name;            // 物理信号名, 也是写入 ctx 的 key
    int  byte_first = 0;         // bytes 起始
    int  byte_last  = 0;         // bytes 结束 (byte_first == byte_last 表示单字节)
    int  bits       = 0;         // 总位数
    bool little_endian = true;   // 字节序 (默认 little, big 是 Motorola)
    bool is_signed  = false;     // signed 类型才需要 (int16/int8)
    std::string formula;         // "x * 0.1" / "x * 0.1 - 1000" / "x + -40" 等
    can_formula::CompiledFormula compiled_formula;
    std::shared_ptr<std::atomic_bool> formula_error_logged =
        std::make_shared<std::atomic_bool>(false);
    std::string unit;            // 仅供调试
};

// ── 单报文解码描述 (can_sources[] 一项) ──
struct MessageSpec {
    uint32_t can_id = 0;
    std::vector<FieldSpec> fields;
};

// ── 解码表 ── 启动时 load 一次, 之后 decode(frame, ctx) O(1) 查表 ──
class DecodeTable {
public:
    // 加载 can_ids.yaml, 填充 m_messages
    // 注: 这里假设文件结构见 schemas/can_ids.schema.json
    bool load(const std::string& canIdsYamlPath);

    // 把当前帧涉及的信号值合并进 ctx (只更新该帧的字段, 其它保持不变)。
    // 单字段发生意外时跳过该字段、保留其 ctx 旧值并仅记录一次；其它字段继续。
    // 返回 true 表示至少成功解码了一个信号。
    bool decode(const struct CanFrame& f,
                std::unordered_map<std::string, double>& ctx) const;

    // 测试用: 当前已加载的 message 数
    size_t messageCount() const { return m_messages.size(); }

    // 测试用: 列出所有已加载的字段名
    std::vector<std::string> allFieldNames() const;

    // 返回该 can_id 在当前 DLC 下被完整覆盖的字段名。结果在 load() 时预计算，
    // 运行期只做一次哈希查找和数组索引；返回只读引用，不暴露可变解码表状态。
    // 调用方不得跨越后续 load() 保存该引用。
    const std::vector<std::string>& availableFieldNames(uint32_t can_id,
                                                        uint8_t dlc) const;

private:
    std::unordered_map<uint32_t, MessageSpec> m_messages;
    std::unordered_map<uint32_t, std::array<std::vector<std::string>, 9>>
        m_available_field_names;

    // 把 raw 字节按 byte range + endian + signed 解析成无符号 64-bit
    static uint64_t extractRaw(const uint8_t data[8], const FieldSpec& fs);
    // 对一个 raw uint64 做 signed reinterpret (若 signed)
    static int64_t toSigned(uint64_t raw, int bits);
};

// 进程级单例：启动时 load 一次，之后 decode 只读查表。三个宿主共用同一份。
inline DecodeTable& globalDecodeTable() {
    static DecodeTable t;
    return t;
}

inline bool loadDecodeTable(const std::string& canIdsYamlPath) {
    return globalDecodeTable().load(canIdsYamlPath);
}

inline bool decodeFrameToCtx(const CanFrame& f,
                             std::unordered_map<std::string, double>& ctx) {
    return globalDecodeTable().decode(f, ctx);
}

}  // namespace demo

#include "can_frame_impl.h"  // 实现放在 .h 里 (header-only, 跟 demo 其它风格一致)