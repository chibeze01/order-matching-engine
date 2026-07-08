#ifndef ORDER_MATCHING_ENGINE_TICK_SIZE_HPP
#define ORDER_MATCHING_ENGINE_TICK_SIZE_HPP

#include <cstdint>

class TickSize {
public:
    explicit TickSize(const uint8_t value_) : value(value_) {}

    bool operator==(const TickSize& other) const { return value == other.value; }
    bool operator!=(const TickSize& other) const { return value != other.value; }

    uint8_t getValue() const { return value; }

private:
    const uint8_t value;
};

#endif // ORDER_MATCHING_ENGINE_TICK_SIZE_HPP
