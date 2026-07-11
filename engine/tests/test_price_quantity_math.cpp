#include "ome/types/price.hpp"
#include "ome/types/price_quantity_math.hpp"
#include "ome/types/quantity.hpp"
#include "ome/types/tick_size.hpp"
#include "ome/types/ticks.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

namespace {

constexpr uint8_t CENT_TICK_SIZE = 0;

TEST(PriceQuantityMath, MultiplyScalesTicks) {
    const Price price(Ticks(100), TickSize(CENT_TICK_SIZE));
    const Price result = PriceQuantityMath::multiply(price, Quantity(3));
    EXPECT_EQ(result.getTicks(), Ticks(300));
}

TEST(PriceQuantityMath, MultiplyByZeroQuantityIsZero) {
    const Price price(Ticks(100), TickSize(CENT_TICK_SIZE));
    const Price result = PriceQuantityMath::multiply(price, Quantity(0));
    EXPECT_EQ(result.getTicks(), Ticks(0));
}

TEST(PriceQuantityMath, DivideScalesTicksDown) {
    const Price price(Ticks(300), TickSize(CENT_TICK_SIZE));
    const Price result = PriceQuantityMath::divide(price, Quantity(3));
    EXPECT_EQ(result.getTicks(), Ticks(100));
}

TEST(PriceQuantityMath, DivideTruncatesTowardsZero) {
    const Price price(Ticks(10), TickSize(CENT_TICK_SIZE));
    const Price result = PriceQuantityMath::divide(price, Quantity(3));
    EXPECT_EQ(result.getTicks(), Ticks(3));
}

TEST(PriceQuantityMath, DivideRejectsZeroQuantity) {
    const Price price(Ticks(100), TickSize(CENT_TICK_SIZE));
    EXPECT_THROW(PriceQuantityMath::divide(price, Quantity(0)), std::invalid_argument);
}

TEST(PriceQuantityMath, PreservesTickSize) {
    const Price price(Ticks(100), TickSize(1));
    const Price result = PriceQuantityMath::multiply(price, Quantity(2));
    EXPECT_EQ(result.getTickSize(), TickSize(1));
}

} // namespace
