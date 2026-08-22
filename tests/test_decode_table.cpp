// test_decode_table.cpp — DecodeTable：加载、多字节/端序/符号/公式解码、短 DLC 容错
#include "minitest.h"

#include "ingress/can_frame.h"

#include "test_util.h"

#include <unordered_map>

namespace {

demo::CanFrame makeFrame(uint32_t id, uint8_t dlc) {
    demo::CanFrame f;
    f.can_id = id;
    f.dlc = dlc;
    return f;
}

}  // namespace

TEST_CASE("DecodeTable::load 正常加载") {
    demo::DecodeTable t;
    REQUIRE(t.load(fixture("can_ids.yaml")));
    CHECK_EQ(t.messageCount(), 1u);
    CHECK_EQ(t.allFieldNames().size(), 5u);
}

TEST_CASE("DecodeTable::decode 完整帧") {
    demo::DecodeTable t;
    REQUIRE(t.load(fixture("can_ids.yaml")));

    auto f = makeFrame(0x100, 8);
    f.data[0] = 50;    // speed  u8
    f.data[1] = 0xFF;  // temp   int8 → -1
    f.data[2] = 0x34;  // rpm    u16 LE
    f.data[3] = 0x12;
    f.data[4] = 0x12;  // volts  u16 BE
    f.data[5] = 0x34;
    f.data[6] = 100;   // scaled u8 → 100*0.5+10 = 60
    f.data[7] = 0;

    std::unordered_map<std::string, double> ctx;
    REQUIRE(t.decode(f, ctx));

    CHECK_APPROX(ctx["speed"], 50.0);
    CHECK_APPROX(ctx["temp"], -1.0);
    CHECK_APPROX(ctx["rpm"], 0x1234);
    CHECK_APPROX(ctx["volts"], 0x1234);
    CHECK_APPROX(ctx["scaled"], 60.0);
}

TEST_CASE("DecodeTable::decode 短 DLC 逐字段容错") {
    demo::DecodeTable t;
    REQUIRE(t.load(fixture("can_ids.yaml")));

    auto f = makeFrame(0x100, 2);  // 只覆盖 byte 0/1
    f.data[0] = 7;
    f.data[1] = 0xFE;  // -2

    std::unordered_map<std::string, double> ctx;
    REQUIRE(t.decode(f, ctx));

    CHECK_EQ(ctx.count("speed"), 1u);
    CHECK_EQ(ctx.count("temp"), 1u);
    CHECK_APPROX(ctx["temp"], -2.0);
    CHECK_EQ(ctx.count("rpm"), 0u);
    CHECK_EQ(ctx.count("volts"), 0u);
    CHECK_EQ(ctx.count("scaled"), 0u);
}

TEST_CASE("DecodeTable::decode 未知 can_id") {
    demo::DecodeTable t;
    REQUIRE(t.load(fixture("can_ids.yaml")));

    auto f = makeFrame(0x999, 8);
    std::unordered_map<std::string, double> ctx;
    CHECK_FALSE(t.decode(f, ctx));
    CHECK(ctx.empty());
}

TEST_CASE("DecodeTable::availableFieldNames") {
    demo::DecodeTable t;
    REQUIRE(t.load(fixture("can_ids.yaml")));

    CHECK_EQ(t.availableFieldNames(0x100, 2).size(), 2u);
    CHECK_EQ(t.availableFieldNames(0x100, 8).size(), 5u);
    CHECK(t.availableFieldNames(0x777, 8).empty());
}

TEST_CASE("DecodeTable::load 公式错误返回 false") {
    const std::string yaml =
        "can_sources:\n"
        "  - name: BAD\n"
        "    can_id: '0x200'\n"
        "    period_ms: 10\n"
        "    fields:\n"
        "      - name: bad_field\n"
        "        byte: 0\n"
        "        bits: 8\n"
        "        type: uint8\n"
        "        formula: x / 0\n";
    const std::string path = writeTempYaml("bad_formula", yaml);

    demo::DecodeTable t;
    CHECK_FALSE(t.load(path));
}
