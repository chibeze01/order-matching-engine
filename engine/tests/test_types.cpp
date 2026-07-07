#include "ome/types.hpp"

#include <gtest/gtest.h>

namespace {

using ome::kNanosPerUnit;
using ome::Order;
using ome::OrderId;
using ome::OrderType;
using ome::Price;
using ome::price_from_nanos;
using ome::price_to_nanos;
using ome::Quantity;
using ome::Side;
using ome::TickSize;

// 0.01 tick, the common case throughout the project.
constexpr TickSize kCentTick{10'000'000};

TEST(PriceConversion, ExactMultipleConvertsExactly) {
    // 101.25 at a 0.01 tick is exactly 10125 ticks.
    const auto price = price_from_nanos(101'250'000'000, kCentTick);
    ASSERT_TRUE(price.has_value());
    EXPECT_EQ(price->ticks, 10125);
}

TEST(PriceConversion, RoundTripIsExactOnTickGrid) {
    const std::int64_t nanos = 42 * kNanosPerUnit; // 42.00
    const auto price = price_from_nanos(nanos, kCentTick);
    ASSERT_TRUE(price.has_value());
    EXPECT_EQ(price_to_nanos(*price, kCentTick), nanos);
}

TEST(PriceConversion, RoundsDownBelowHalfTick) {
    // 1.004 at a 0.01 tick: 0.4 of a tick above 100, rounds down.
    const auto price = price_from_nanos(1'004'000'000, kCentTick);
    ASSERT_TRUE(price.has_value());
    EXPECT_EQ(price->ticks, 100);
}

TEST(PriceConversion, RoundsUpFromHalfTick) {
    // 1.005 at a 0.01 tick: exactly half a tick, rounds up.
    const auto price = price_from_nanos(1'005'000'000, kCentTick);
    ASSERT_TRUE(price.has_value());
    EXPECT_EQ(price->ticks, 101);
}

TEST(PriceConversion, ZeroIsValid) {
    const auto price = price_from_nanos(0, kCentTick);
    ASSERT_TRUE(price.has_value());
    EXPECT_EQ(price->ticks, 0);
}

TEST(PriceConversion, NegativePriceRejected) { EXPECT_FALSE(price_from_nanos(-1, kCentTick).has_value()); }

TEST(PriceConversion, NonPositiveTickRejected) {
    EXPECT_FALSE(price_from_nanos(100, TickSize{0}).has_value());
    EXPECT_FALSE(price_from_nanos(100, TickSize{-1}).has_value());
}

TEST(Price, OrdersByTicks) {
    EXPECT_LT(Price{99}, Price{100});
    EXPECT_EQ(Price{100}, Price{100});
}

TEST(OrderId, ComparesByValue) {
    EXPECT_LT(OrderId{1}, OrderId{2});
    EXPECT_EQ(OrderId{7}, OrderId{7});
}

TEST(OrderStruct, IsFixedSizeAndTriviallyCopyable) {
    static_assert(sizeof(Order) == 32);
    static_assert(std::is_trivially_copyable_v<Order>);
    const Order order{OrderId{1}, Price{10125}, Quantity{50}, Side::Buy, OrderType::Limit};
    EXPECT_EQ(order.id, OrderId{1});
    EXPECT_EQ(order.price, Price{10125});
    EXPECT_EQ(order.quantity, 50U);
    EXPECT_EQ(order.side, Side::Buy);
    EXPECT_EQ(order.type, OrderType::Limit);
}

} // namespace
