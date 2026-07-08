#ifndef ORDER_MATCHING_ENGINE_PRICE_QUANTITY_MATH_HPP
#define ORDER_MATCHING_ENGINE_PRICE_QUANTITY_MATH_HPP

#include "price.hpp"
#include "quantity.hpp"

class PriceQuantityMath {
  public:
    static Price multiply(Price price, Quantity quantity);
    static Price multiply(const Quantity quantity, const Price price) { return multiply(price, quantity); }
    static Price divide(Price price, Quantity quantity);
};

#endif // ORDER_MATCHING_ENGINE_PRICE_QUANTITY_MATH_HPP
