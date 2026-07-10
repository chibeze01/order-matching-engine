#ifndef ORDER_MATCHING_ENGINE_PRICE_HPP
#define ORDER_MATCHING_ENGINE_PRICE_HPP

#include "tick_size.hpp"
#include "ticks.hpp"
#include <string>

class Price {
  public:
    Price(Ticks ticks_, TickSize tick_size_);

    static Price fromDecimal(const std::string &decimal, TickSize tick_size_);

    Ticks getTicks() const;
    TickSize getTickSize() const;
    std::string toDecimal() const;

    Price operator+(const Price &other) const;
    Price operator-(const Price &other) const;

    bool operator<(const Price &other) const;
    bool operator<=(const Price &other) const;
    bool operator>(const Price &other) const;
    bool operator>=(const Price &other) const;
    bool operator==(const Price &other) const;
    bool operator!=(const Price &other) const;

  private:
    Ticks ticks;
    TickSize tick_size;
};

#endif // ORDER_MATCHING_ENGINE_PRICE_HPP
