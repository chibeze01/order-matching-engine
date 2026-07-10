#ifndef ORDER_MATCHING_ENGINE_TICKS_HPP
#define ORDER_MATCHING_ENGINE_TICKS_HPP

#include <cstdint>

class Ticks {
  public:
    explicit Ticks(const int64_t value_ = 0) : value(value_) {}

    Ticks operator+(const Ticks &other) const { return Ticks(value + other.value); }
    Ticks operator-(const Ticks &other) const { return Ticks(value - other.value); }
    Ticks operator*(const int64_t scalar) const { return Ticks(value * scalar); }
    Ticks operator/(const int64_t divisor) const { return Ticks(value / divisor); }

    bool operator<(const Ticks &other) const { return value < other.value; }
    bool operator<=(const Ticks &other) const { return value <= other.value; }
    bool operator>(const Ticks &other) const { return value > other.value; }
    bool operator>=(const Ticks &other) const { return value >= other.value; }
    bool operator==(const Ticks &other) const { return value == other.value; }
    bool operator!=(const Ticks &other) const { return value != other.value; }

    int64_t getValue() const { return value; }

  private:
    const int64_t value;
};

#endif // ORDER_MATCHING_ENGINE_TICKS_HPP
