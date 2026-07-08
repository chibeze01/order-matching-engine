#include "price_quantity_math.hpp"
#include <stdexcept>

Price PriceQuantityMath::multiply(const Price price, const Quantity quantity) {
    const Ticks result(price.getTicks().getValue() * static_cast<int64_t>(quantity.getValue()));
    return Price(result, price.getTickSize());
}

Price PriceQuantityMath::divide(const Price price, const Quantity quantity) {
    if (quantity.getValue() == 0) {
        throw std::invalid_argument("Cannot divide price by zero quantity");
    }
    const Ticks result(price.getTicks().getValue() / static_cast<int64_t>(quantity.getValue()));
    return Price(result, price.getTickSize());
}
