#include "core/PyFormat.hpp"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <system_error>

namespace bitzkey {

double py_round(double value, int digits) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", digits, value);
    return std::strtod(buffer, nullptr);
}

double py_sum(std::span<const double> values) {
    double total = 0.0;
    double compensation = 0.0;
    for (const double value : values) {
        const double sum = total + value;
        if (std::fabs(total) >= std::fabs(value)) {
            compensation += (total - sum) + value;
        } else {
            compensation += (value - sum) + total;
        }
        total = sum;
    }
    if (compensation != 0.0 && std::isfinite(compensation)) total += compensation;
    return total;
}

std::string py_float_repr(double value) {
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value > 0 ? "Infinity" : "-Infinity";
    // The shortest round-trip digits, read off scientific notation: d.ddde±x.
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::scientific);
    std::string_view text(buffer, static_cast<std::size_t>(result.ptr - buffer));
    const std::size_t e = text.find('e');
    std::string_view mantissa = text.substr(0, e);
    int exponent = 0;
    std::from_chars(text.data() + e + 1 + (text[e + 1] == '+'), text.data() + text.size(), exponent);

    std::string sign;
    if (!mantissa.empty() && mantissa.front() == '-') {
        sign = "-";
        mantissa.remove_prefix(1);
    }
    std::string digits;
    for (const char c : mantissa) {
        if (c != '.') digits += c;
    }
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();

    if (-4 <= exponent && exponent < 16) {
        // Fixed notation, always with a fractional part.
        std::string out;
        const int point = exponent + 1;  // digits before the decimal point
        if (point <= 0) {
            out = "0." + std::string(static_cast<std::size_t>(-point), '0') + digits;
        } else if (static_cast<std::size_t>(point) >= digits.size()) {
            out = digits + std::string(static_cast<std::size_t>(point) - digits.size(), '0') + ".0";
        } else {
            out = digits.substr(0, static_cast<std::size_t>(point)) + "." + digits.substr(static_cast<std::size_t>(point));
        }
        return sign + out;
    }
    std::string out = digits.substr(0, 1);
    if (digits.size() > 1) out += "." + digits.substr(1);
    char exp_text[16];
    std::snprintf(exp_text, sizeof exp_text, "e%c%02d", exponent < 0 ? '-' : '+', std::abs(exponent));
    return sign + out + exp_text;
}

std::string py_percent(double value, int width) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.0f%%", value * 100);
    std::string text(buffer);
    if (static_cast<int>(text.size()) < width) text.insert(0, static_cast<std::size_t>(width) - text.size(), ' ');
    return text;
}

}  // namespace bitzkey
