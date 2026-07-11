#include "price.hpp"
#include <stdexcept>

Price::Price(const Ticks ticks_, const TickSize tick_size_) : ticks(ticks_), tick_size(tick_size_) {
    if (ticks.getValue() < 0) {
        throw std::invalid_argument("Negative prices are not allowed");
    }
}

Price Price::fromDecimal(const std::string &decimal, TickSize tick_size_) {
    std::string digits;
    int decimal_pos = -1;

    for (const char c : decimal) {
        if (c == '.') {
            decimal_pos = static_cast<int>(digits.size());
        } else if (std::isdigit(c) != 0) {
            digits += c;
        }
    }

    if (decimal_pos == -1) {
        decimal_pos = static_cast<int>(digits.size());
    }

    const int actual_decimals = static_cast<int>(digits.size()) - decimal_pos;
    const int required_decimals = 2 + static_cast<int>(tick_size_.getValue());

    if (actual_decimals > required_decimals) {
        throw std::invalid_argument("Decimal precision exceeds tick size");
    }

    const int zeros_to_add = required_decimals - actual_decimals;
    for (int i = 0; i < zeros_to_add; ++i) {
        digits += '0';
    }

    const int64_t ticks_value = std::stoll(digits);
    return Price(Ticks(ticks_value), tick_size_);
}

Ticks Price::getTicks() const { return ticks; }

TickSize Price::getTickSize() const { return tick_size; }

std::string Price::toDecimal() const {
    std::string s = std::to_string(ticks.getValue());
    const int required_decimals = 2 + static_cast<int>(tick_size.getValue());
    int decimal_pos = static_cast<int>(s.size()) - required_decimals;

    if (decimal_pos <= 0) {
        s = std::string(-decimal_pos + 1, '0') + s;
        decimal_pos = 1;
    }

    s.insert(decimal_pos, ".");
    return s;
}

Price Price::operator+(const Price &other) const {
    if (tick_size.getValue() != other.tick_size.getValue()) {
        throw std::invalid_argument("Cannot add prices with different tick sizes");
    }
    return Price(ticks + other.ticks, tick_size);
}

Price Price::operator-(const Price &other) const {
    if (tick_size.getValue() != other.tick_size.getValue()) {
        throw std::invalid_argument("Cannot subtract prices with different tick sizes");
    }
    const Ticks result = ticks - other.ticks;
    if (result.getValue() < 0) {
        throw std::invalid_argument("Result would be negative");
    }
    return Price(result, tick_size);
}

bool Price::operator<(const Price &other) const {
    if (tick_size.getValue() != other.tick_size.getValue()) {
        throw std::invalid_argument("Cannot compare prices with different tick sizes");
    }
    return ticks < other.ticks;
}

bool Price::operator<=(const Price &other) const {
    if (tick_size.getValue() != other.tick_size.getValue()) {
        throw std::invalid_argument("Cannot compare prices with different tick sizes");
    }
    return ticks <= other.ticks;
}

bool Price::operator>(const Price &other) const {
    if (tick_size.getValue() != other.tick_size.getValue()) {
        throw std::invalid_argument("Cannot compare prices with different tick sizes");
    }
    return ticks > other.ticks;
}

bool Price::operator>=(const Price &other) const {
    if (tick_size.getValue() != other.tick_size.getValue()) {
        throw std::invalid_argument("Cannot compare prices with different tick sizes");
    }
    return ticks >= other.ticks;
}

bool Price::operator==(const Price &other) const { return ticks == other.ticks && tick_size == other.tick_size; }

bool Price::operator!=(const Price &other) const { return !(*this == other); }
