#include "ome/types/price.hpp"
#include "ome/types/tick_size.hpp"
#include "ome/types/ticks.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

namespace {

constexpr uint8_t CENT_TICK_SIZE = 0;
constexpr uint8_t MILLI_TICK_SIZE = 1;

TEST(Price, ConstructionAcceptsNonNegativeTicks) {
    Price price(Ticks(0), TickSize(CENT_TICK_SIZE));
    EXPECT_EQ(price.getTicks(), Ticks(0));
}

TEST(Price, ConstructionRejectsNegativeTicks) {
    EXPECT_THROW(Price(Ticks(-1), TickSize(CENT_TICK_SIZE)), std::invalid_argument);
}

TEST(Price, FromDecimalCentTickExactConversion) {
    const Price price = Price::fromDecimal("123.45", TickSize(CENT_TICK_SIZE));
    EXPECT_EQ(price.getTicks(), Ticks(12345));
}

TEST(Price, FromDecimalMilliTickExactConversion) {
    const Price price = Price::fromDecimal("123.45", TickSize(MILLI_TICK_SIZE));
    EXPECT_EQ(price.getTicks(), Ticks(123450));
}

TEST(Price, FromDecimalWholeNumberConvertsExactly) {
    const Price price = Price::fromDecimal("42", TickSize(CENT_TICK_SIZE));
    EXPECT_EQ(price.getTicks(), Ticks(4200));
}

TEST(Price, FromDecimalZeroIsValid) {
    const Price price = Price::fromDecimal("0", TickSize(CENT_TICK_SIZE));
    EXPECT_EQ(price.getTicks(), Ticks(0));
}

TEST(Price, FromDecimalRejectsPrecisionBeyondTickSize) {
    // 3 decimal places requested but cent tick only supports 2.
    EXPECT_THROW(Price::fromDecimal("123.456", TickSize(CENT_TICK_SIZE)), std::invalid_argument);
}

TEST(Price, ToDecimalRoundTripsCentTick) {
    const Price price(Ticks(12345), TickSize(CENT_TICK_SIZE));
    EXPECT_EQ(price.toDecimal(), "123.45");
}

TEST(Price, ToDecimalRoundTripsMilliTick) {
    const Price price(Ticks(123450), TickSize(MILLI_TICK_SIZE));
    EXPECT_EQ(price.toDecimal(), "123.450");
}

TEST(Price, ToDecimalPadsLeadingZerosBelowOneUnit) {
    // 5 ticks at 0.01 each is 0.05, not 05 or .5.
    const Price price(Ticks(5), TickSize(CENT_TICK_SIZE));
    EXPECT_EQ(price.toDecimal(), "0.05");
}

TEST(Price, ToDecimalZeroTicks) {
    const Price price(Ticks(0), TickSize(CENT_TICK_SIZE));
    EXPECT_EQ(price.toDecimal(), "0.00");
}

TEST(Price, ArithmeticAddSameTickSize) {
    const Price a(Ticks(100), TickSize(CENT_TICK_SIZE));
    const Price b(Ticks(50), TickSize(CENT_TICK_SIZE));
    EXPECT_EQ((a + b).getTicks(), Ticks(150));
}

TEST(Price, ArithmeticSubtractSameTickSize) {
    const Price a(Ticks(100), TickSize(CENT_TICK_SIZE));
    const Price b(Ticks(30), TickSize(CENT_TICK_SIZE));
    EXPECT_EQ((a - b).getTicks(), Ticks(70));
}

TEST(Price, ArithmeticSubtractRejectsNegativeResult) {
    const Price a(Ticks(30), TickSize(CENT_TICK_SIZE));
    const Price b(Ticks(100), TickSize(CENT_TICK_SIZE));
    EXPECT_THROW(a - b, std::invalid_argument);
}

TEST(Price, ArithmeticAddRejectsMismatchedTickSize) {
    const Price a(Ticks(100), TickSize(CENT_TICK_SIZE));
    const Price b(Ticks(100), TickSize(MILLI_TICK_SIZE));
    EXPECT_THROW(a + b, std::invalid_argument);
}

TEST(Price, ArithmeticSubtractRejectsMismatchedTickSize) {
    const Price a(Ticks(100), TickSize(CENT_TICK_SIZE));
    const Price b(Ticks(100), TickSize(MILLI_TICK_SIZE));
    EXPECT_THROW(a - b, std::invalid_argument);
}

TEST(Price, ComparisonOrdersByTicks) {
    const Price a(Ticks(99), TickSize(CENT_TICK_SIZE));
    const Price b(Ticks(100), TickSize(CENT_TICK_SIZE));
    EXPECT_LT(a, b);
    EXPECT_LE(a, b);
    EXPECT_GT(b, a);
    EXPECT_GE(b, a);
}

TEST(Price, ComparisonRejectsMismatchedTickSize) {
    const Price a(Ticks(100), TickSize(CENT_TICK_SIZE));
    const Price b(Ticks(100), TickSize(MILLI_TICK_SIZE));
    EXPECT_THROW(a < b, std::invalid_argument);
    EXPECT_THROW(a <= b, std::invalid_argument);
    EXPECT_THROW(a > b, std::invalid_argument);
    EXPECT_THROW(a >= b, std::invalid_argument);
}

TEST(Price, EqualityComparesTicksAndTickSize) {
    EXPECT_EQ(Price(Ticks(100), TickSize(CENT_TICK_SIZE)), Price(Ticks(100), TickSize(CENT_TICK_SIZE)));
    EXPECT_NE(Price(Ticks(100), TickSize(CENT_TICK_SIZE)), Price(Ticks(101), TickSize(CENT_TICK_SIZE)));
    EXPECT_NE(Price(Ticks(100), TickSize(CENT_TICK_SIZE)), Price(Ticks(100), TickSize(MILLI_TICK_SIZE)));
}

} // namespace
