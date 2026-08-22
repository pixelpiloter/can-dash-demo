// test_can_formula.cpp — CAN formula 线性 scale/offset 解析与求值
#include "minitest.h"

#include "can_formula.h"

using namespace demo::can_formula;

TEST_CASE("can_formula::compile identity") {
    CompiledFormula c;
    std::string err;
    REQUIRE(compile("x", c, err));
    CHECK_APPROX(c.scale, 1.0);
    CHECK_APPROX(c.offset, 0.0);
}

TEST_CASE("can_formula::compile scale + offset") {
    CompiledFormula c;
    std::string err;
    REQUIRE(compile("x * 0.1 - 1000", c, err));
    CHECK_APPROX(c.scale, 0.1);
    CHECK_APPROX(c.offset, -1000.0);
}

TEST_CASE("can_formula::compile negative scale + negative offset") {
    CompiledFormula c;
    std::string err;
    REQUIRE(compile("-x + -40", c, err));
    CHECK_APPROX(c.scale, -1.0);
    CHECK_APPROX(c.offset, -40.0);
}

TEST_CASE("can_formula::compile divide") {
    CompiledFormula c;
    std::string err;
    REQUIRE(compile("x / 2", c, err));
    CHECK_APPROX(c.scale, 0.5);
}

TEST_CASE("can_formula::compile spaces") {
    CompiledFormula c;
    std::string err;
    REQUIRE(compile("  x  +  5.5 ", c, err));
    CHECK_APPROX(c.scale, 1.0);
    CHECK_APPROX(c.offset, 5.5);
}

TEST_CASE("can_formula::compile 非法输入") {
    CompiledFormula c;
    std::string err;
    CHECK_FALSE(compile("y", c, err));
    CHECK_FALSE(compile("x / 0", c, err));
    CHECK_FALSE(compile("x +", c, err));
    CHECK_FALSE(compile("x * 1e999", c, err));  // 数字越界 → scale overflow
}

TEST_CASE("can_formula::evaluate") {
    double v = 0.0;
    std::string err;
    REQUIRE(evaluate("x * 0.5 + 10", 100.0, v, err));
    CHECK_APPROX(v, 60.0);

    REQUIRE(evaluate("x - 40", 20.0, v, err));
    CHECK_APPROX(v, -20.0);

    CHECK_FALSE(evaluate("x / 0", 5.0, v, err));
}

TEST_CASE("can_formula::validateRawRange 安全域") {
    CompiledFormula c;
    std::string err;
    REQUIRE(compile("x * 0.1 - 1000", c, err));
    CHECK(validateRawRange(c, 8, false, err));
    CHECK(validateRawRange(c, 8, true, err));
}

TEST_CASE("can_formula::validateRawRange 非法位宽") {
    CompiledFormula c;
    std::string err;
    REQUIRE(compile("x", c, err));
    CHECK_FALSE(validateRawRange(c, 0, false, err));
    CHECK_FALSE(validateRawRange(c, 65, false, err));
}
