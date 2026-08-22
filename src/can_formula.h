#pragma once

#include <cerrno>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <limits>
#include <string>

namespace demo::can_formula {

// CAN formula 是 DBC scale/offset 的线性表示，不是通用表达式语言。
// 解析后保存为 physical = raw * scale + offset，解码期不再扫描配置字符串。
struct CompiledFormula {
    double scale = 1.0;
    double offset = 0.0;
};

namespace detail {

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    bool parse(CompiledFormula& out, std::string& error) {
        try {
            skipSpaces();
            if (atEnd()) {
                out = {};
                return true;
            }

            double x_sign = 1.0;
            if (consume('+')) {
                skipSpaces();
            } else if (consume('-')) {
                x_sign = -1.0;
                skipSpaces();
            }
            if (!consume('x')) return fail("expected 'x'", error);

            CompiledFormula parsed;
            parsed.scale = x_sign;
            skipSpaces();
            if (atEnd()) {
                out = parsed;
                return true;
            }

            if (peek() == '*' || peek() == '/') {
                const char op = text_[pos_++];
                double factor = 0.0;
                if (!parseNumber(factor, error)) return false;
                if (op == '/' && factor == 0.0)
                    return fail("division by zero", error);
                parsed.scale =
                    op == '*' ? parsed.scale * factor : parsed.scale / factor;
                if (!std::isfinite(parsed.scale))
                    return fail("scale overflow", error);
                skipSpaces();
            }

            if (!atEnd() && (peek() == '+' || peek() == '-')) {
                const char op = text_[pos_++];
                double offset = 0.0;
                if (!parseNumber(offset, error)) return false;
                parsed.offset = op == '+' ? offset : -offset;
                if (!std::isfinite(parsed.offset))
                    return fail("offset overflow", error);
                skipSpaces();
            }

            if (!atEnd())
                return fail(std::string("unexpected token '") + peek() + "'", error);

            out = parsed;
            return true;
        } catch (const std::exception& e) {
            error = std::string("formula parser failure: ") + e.what();
            return false;
        } catch (...) {
            error = "formula parser failure: unknown exception";
            return false;
        }
    }

private:
    const std::string& text_;
    std::size_t pos_ = 0;

    bool atEnd() const noexcept { return pos_ >= text_.size(); }
    char peek() const noexcept { return atEnd() ? '\0' : text_[pos_]; }

    void skipSpaces() noexcept {
        while (!atEnd()) {
            const unsigned char c = static_cast<unsigned char>(text_[pos_]);
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
            ++pos_;
        }
    }

    bool consume(char expected) noexcept {
        if (peek() != expected) return false;
        ++pos_;
        return true;
    }

    bool fail(const std::string& message, std::string& error) const {
        error = message + " at byte " + std::to_string(pos_);
        return false;
    }

    bool parseNumber(double& value, std::string& error) {
        skipSpaces();
        const std::size_t start = pos_;
        if (peek() == '+' || peek() == '-') ++pos_;

        bool digits = false;
        while (!atEnd() && peek() >= '0' && peek() <= '9') {
            digits = true;
            ++pos_;
        }
        if (consume('.')) {
            while (!atEnd() && peek() >= '0' && peek() <= '9') {
                digits = true;
                ++pos_;
            }
        }
        if (!digits) {
            pos_ = start;
            return fail("expected number", error);
        }

        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') ++pos_;
            const std::size_t exponent_start = pos_;
            while (!atEnd() && peek() >= '0' && peek() <= '9') ++pos_;
            if (pos_ == exponent_start)
                return fail("invalid scientific exponent", error);
        }

        const std::string token = text_.substr(start, pos_ - start);
        char* end = nullptr;
        errno = 0;
        value = std::strtod(token.c_str(), &end);
        if (end != token.c_str() + token.size())
            return fail("invalid number", error);
        if (errno == ERANGE || !std::isfinite(value))
            return fail("number out of range", error);
        skipSpaces();
        return true;
    }
};

}  // namespace detail

inline bool compile(const std::string& formula,
                    CompiledFormula& out,
                    std::string& error) {
    error.clear();
    return detail::Parser(formula).parse(out, error);
}

inline bool evaluate(const CompiledFormula& formula,
                     double x,
                     double& value,
                     std::string& error) {
    if (!std::isfinite(x)) {
        error = "input is not finite";
        return false;
    }
    const double scaled = x * formula.scale;
    if (!std::isfinite(scaled)) {
        error = "multiplication overflow";
        return false;
    }
    value = scaled + formula.offset;
    if (!std::isfinite(value)) {
        error = "addition overflow";
        return false;
    }
    error.clear();
    return true;
}

inline bool evaluate(const std::string& formula,
                     double x,
                     double& value,
                     std::string& error) {
    CompiledFormula compiled;
    return compile(formula, compiled, error) &&
           evaluate(compiled, x, value, error);
}

inline bool validateRawRange(const CompiledFormula& formula,
                             int bits,
                             bool is_signed,
                             std::string& error) {
    if (bits <= 0 || bits > 64) {
        error = "invalid raw bit width " + std::to_string(bits);
        return false;
    }

    double raw_min = 0.0;
    double raw_max = 0.0;
    if (is_signed) {
        if (bits == 64) {
            raw_min = static_cast<double>(std::numeric_limits<int64_t>::min());
            raw_max = static_cast<double>(std::numeric_limits<int64_t>::max());
        } else {
            const uint64_t magnitude = uint64_t{1} << (bits - 1);
            raw_min = -static_cast<double>(magnitude);
            raw_max = static_cast<double>(magnitude - 1);
        }
    } else {
        const uint64_t maximum = bits == 64
            ? std::numeric_limits<uint64_t>::max()
            : (uint64_t{1} << bits) - 1;
        raw_max = static_cast<double>(maximum);
    }

    double ignored = 0.0;
    std::string endpoint_error;
    if (!evaluate(formula, raw_min, ignored, endpoint_error)) {
        error = "raw range minimum: " + endpoint_error;
        return false;
    }
    if (!evaluate(formula, raw_max, ignored, endpoint_error)) {
        error = "raw range maximum: " + endpoint_error;
        return false;
    }
    error.clear();
    return true;
}

inline std::string validationError(const std::string& file,
                                   const std::string& can_source,
                                   const std::string& field,
                                   const std::string& formula,
                                   const std::string& detail) {
    return file + ": can_source '" + can_source + "' field '" + field +
           "' formula '" + formula + "': " + detail;
}

}  // namespace demo::can_formula
