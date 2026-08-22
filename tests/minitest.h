// minitest.h — 极简零依赖测试框架（无 doctest/GoogleTest 外部依赖，可离线/交叉编译）
//
// API（与主流框架心智一致）：
//   TEST_CASE(name) { ... }    注册一个测试
//   CHECK(expr)                非致命断言
//   CHECK_FALSE(expr)
//   CHECK_EQ(a, b)             相等断言，失败打印两侧值
//   CHECK_APPROX(a, b)         浮点近似（默认 1e-9）
//   REQUIRE(expr)              致命断言，失败立即结束当前测试
//   MINITEST_MAIN()            在单个 TU 里展开为 main()
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

namespace minitest {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

struct Context {
    int checks = 0;
    int failures = 0;
};

// REQUIRE 失败时抛出，用于中断当前测试（可跨越非 void 的辅助函数）。
struct AbortTest {};

inline Context& ctx() {
    static Context c;
    return c;
}

template <typename T>
inline std::string toStr(const T& v) {
    using U = std::decay_t<T>;
    if constexpr (std::is_same_v<U, bool>) {
        return v ? "true" : "false";
    } else if constexpr (std::is_same_v<U, std::string>) {
        return "\"" + v + "\"";
    } else if constexpr (std::is_same_v<U, const char*> ||
                         std::is_same_v<U, char*>) {
        return v ? std::string("\"") + v + "\"" : "(null)";
    } else if constexpr (std::is_floating_point_v<U>) {
        char b[32];
        std::snprintf(b, sizeof b, "%.6g", static_cast<double>(v));
        return b;
    } else {
        return std::to_string(v);
    }
}

inline int runAll() {
    int failed = 0;
    for (const auto& t : registry()) {
        const int before = ctx().failures;
        std::printf("[ RUN  ] %s\n", t.name);
        try {
            t.fn();
        } catch (const AbortTest&) {
            // 已计入 failure，仅中断后续断言
        }
        const int delta = ctx().failures - before;
        if (delta > 0) {
            failed += delta;
            std::printf("[ FAIL ] %s (%d)\n", t.name, delta);
        } else {
            std::printf("[  OK  ] %s\n", t.name);
        }
    }
    std::printf("===================================\n");
    std::printf("%zu tests, %d checks, %d failures\n", registry().size(),
                ctx().checks, ctx().failures);
    return failed == 0 ? 0 : 1;
}

}  // namespace minitest

#define MINITEST_CONCAT2(a, b) a##b
#define MINITEST_CONCAT(a, b) MINITEST_CONCAT2(a, b)

#define TEST_CASE(name)                                                   \
    static void MINITEST_CONCAT(minitest_fn_, __LINE__)();                \
    static ::minitest::Registrar MINITEST_CONCAT(minitest_reg_, __LINE__)(\
        name, &MINITEST_CONCAT(minitest_fn_, __LINE__));                  \
    static void MINITEST_CONCAT(minitest_fn_, __LINE__)()

#define CHECK(expr)                                                       \
    do {                                                                  \
        ++::minitest::ctx().checks;                                       \
        if (!(expr)) {                                                    \
            ++::minitest::ctx().failures;                                 \
            std::printf("  FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__,  \
                        #expr);                                           \
        }                                                                 \
    } while (0)

#define CHECK_FALSE(expr)                                                 \
    do {                                                                  \
        ++::minitest::ctx().checks;                                       \
        if ((expr)) {                                                     \
            ++::minitest::ctx().failures;                                 \
            std::printf("  FAIL %s:%d  CHECK_FALSE(%s)\n", __FILE__,      \
                        __LINE__, #expr);                                 \
        }                                                                 \
    } while (0)

#define REQUIRE(expr)                                                     \
    do {                                                                  \
        ++::minitest::ctx().checks;                                       \
        if (!(expr)) {                                                    \
            ++::minitest::ctx().failures;                                 \
            std::printf("  FAIL %s:%d  REQUIRE(%s)\n", __FILE__, __LINE__, \
                        #expr);                                           \
            throw ::minitest::AbortTest{};                                \
        }                                                                 \
    } while (0)

#define CHECK_EQ(a, b)                                                    \
    do {                                                                  \
        ++::minitest::ctx().checks;                                       \
        const auto& _me_a = (a);                                          \
        const auto& _me_b = (b);                                          \
        if (!(_me_a == _me_b)) {                                          \
            ++::minitest::ctx().failures;                                 \
            std::printf("  FAIL %s:%d  CHECK_EQ(%s, %s)\n    left = %s\n" \
                        "    right= %s\n", __FILE__, __LINE__, #a, #b,    \
                        ::minitest::toStr(_me_a).c_str(),                 \
                        ::minitest::toStr(_me_b).c_str());                \
        }                                                                 \
    } while (0)

#define CHECK_APPROX(a, b)                                                \
    do {                                                                  \
        ++::minitest::ctx().checks;                                       \
        const double _me_a = static_cast<double>(a);                      \
        const double _me_b = static_cast<double>(b);                      \
        if (std::fabs(_me_a - _me_b) > 1e-9) {                            \
            ++::minitest::ctx().failures;                                 \
            std::printf("  FAIL %s:%d  CHECK_APPROX(%s, %s)\n    left = " \
                        "%s\n    right= %s\n", __FILE__, __LINE__, #a,    \
                        #b, ::minitest::toStr(_me_a).c_str(),             \
                        ::minitest::toStr(_me_b).c_str());                \
        }                                                                 \
    } while (0)

#define MINITEST_MAIN()                                                   \
    int main() { return ::minitest::runAll(); }
