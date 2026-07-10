#ifndef ORDER_MATCHING_ENGINE_QUANTITY_HPP
#define ORDER_MATCHING_ENGINE_QUANTITY_HPP

#include <cstdint>

class Quantity {
  public:
    explicit Quantity(const uint64_t value_ = 0) : value(value_) {}

    Quantity operator+(const Quantity &other) const { return Quantity(value + other.value); }
    Quantity operator-(const Quantity &other) const { return Quantity(value - other.value); }
    Quantity operator*(const uint64_t scalar) const { return Quantity(value * scalar); }
    Quantity operator/(const uint64_t divisor) const { return Quantity(value / divisor); }

    bool operator<(const Quantity &other) const { return value < other.value; }
    bool operator<=(const Quantity &other) const { return value <= other.value; }
    bool operator>(const Quantity &other) const { return value > other.value; }
    bool operator>=(const Quantity &other) const { return value >= other.value; }
    bool operator==(const Quantity &other) const { return value == other.value; }
    bool operator!=(const Quantity &other) const { return value != other.value; }

    uint64_t getValue() const { return value; }

  private:
    const uint64_t value;
};

#endif // ORDER_MATCHING_ENGINE_QUANTITY_HPP
