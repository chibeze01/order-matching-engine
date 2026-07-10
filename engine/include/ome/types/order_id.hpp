#ifndef ORDER_MATCHING_ENGINE_ORDER_ID_HPP
#define ORDER_MATCHING_ENGINE_ORDER_ID_HPP

#include <cstdint>

class OrderId {
  public:
    explicit OrderId(const uint64_t value_) : value(value_) {}

    bool operator<(const OrderId &other) const { return value < other.value; }
    bool operator<=(const OrderId &other) const { return value <= other.value; }
    bool operator>(const OrderId &other) const { return value > other.value; }
    bool operator>=(const OrderId &other) const { return value >= other.value; }
    bool operator==(const OrderId &other) const { return value == other.value; }
    bool operator!=(const OrderId &other) const { return value != other.value; }

    uint64_t getValue() const { return value; }

  private:
    const uint64_t value;
};

#endif // ORDER_MATCHING_ENGINE_ORDER_ID_HPP
