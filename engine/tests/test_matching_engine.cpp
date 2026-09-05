#include "ome/matching/matching_engine.hpp"
#include "ome/book/order_book.hpp"
#include "ome/types/order.hpp"

#include "book_fuzz_generator.hpp" // Rng, boundedRand -- deterministic, cross-platform-stable draws
#include "naive_matcher.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace {

constexpr uint8_t CENT_TICK_SIZE = 0;
constexpr int64_t BAND_MIN = 100;
constexpr int64_t BAND_MAX = 200;
constexpr std::size_t CAPACITY = 64;

Order makeOrder(const uint64_t id, const int64_t ticks, const uint64_t qty, const Side side) {
    return Order{OrderId(id), Price(Ticks(ticks), TickSize(CENT_TICK_SIZE)), Quantity(qty), side, OrderType::Limit};
}

OrderBook makeBook(const std::size_t capacity = CAPACITY) {
    return OrderBook(Ticks(BAND_MIN), Ticks(BAND_MAX), capacity);
}

void expectTrade(const Trade &trade, const uint64_t maker_id, const uint64_t taker_id, const int64_t price,
                  const uint64_t qty, const Side aggressor_side) {
    EXPECT_EQ(trade.maker_id, OrderId(maker_id));
    EXPECT_EQ(trade.taker_id, OrderId(taker_id));
    EXPECT_EQ(trade.price, Ticks(price));
    EXPECT_EQ(trade.quantity, Quantity(qty));
    EXPECT_EQ(trade.aggressor_side, aggressor_side);
}

TEST(MatchingEngine, NoCrossRestsWholeOrder) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);

    SubmitResult result = engine.submitLimit(makeOrder(1, 150, 10, Side::Buy));
    EXPECT_TRUE(result.trades.empty());
    EXPECT_EQ(result.remaining, Quantity(10));
    ASSERT_NE(result.resting, nullptr);
    EXPECT_EQ(result.resting->quantity, 10U);
    EXPECT_EQ(book.size(), 1U);
}

TEST(MatchingEngine, ExactFillLeavesNoRemainder) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);
    book.insert(makeOrder(1, 150, 10, Side::Sell));

    SubmitResult result = engine.submitLimit(makeOrder(2, 150, 10, Side::Buy));
    ASSERT_EQ(result.trades.size(), 1U);
    expectTrade(result.trades[0], 1, 2, 150, 10, Side::Buy);
    EXPECT_EQ(result.remaining, Quantity(0));
    EXPECT_EQ(result.resting, nullptr);
    EXPECT_EQ(book.size(), 0U);
}

TEST(MatchingEngine, PartialFillRestsRemainder) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);
    book.insert(makeOrder(1, 150, 10, Side::Sell));

    SubmitResult result = engine.submitLimit(makeOrder(2, 150, 15, Side::Buy));
    ASSERT_EQ(result.trades.size(), 1U);
    expectTrade(result.trades[0], 1, 2, 150, 10, Side::Buy);
    EXPECT_EQ(result.remaining, Quantity(5));
    ASSERT_NE(result.resting, nullptr);
    EXPECT_EQ(result.resting->quantity, 5U);
    EXPECT_EQ(result.resting->price_ticks, 150);
    EXPECT_EQ(result.resting->side, Side::Buy);
    EXPECT_EQ(book.size(), 1U);
}

TEST(MatchingEngine, MultiLevelSweepFillsBestPriceFirst) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);
    book.insert(makeOrder(1, 150, 5, Side::Sell));
    book.insert(makeOrder(2, 151, 5, Side::Sell));
    book.insert(makeOrder(3, 152, 5, Side::Sell));

    SubmitResult result = engine.submitLimit(makeOrder(4, 152, 12, Side::Buy));
    ASSERT_EQ(result.trades.size(), 3U);
    expectTrade(result.trades[0], 1, 4, 150, 5, Side::Buy);
    expectTrade(result.trades[1], 2, 4, 151, 5, Side::Buy);
    expectTrade(result.trades[2], 3, 4, 152, 2, Side::Buy);
    EXPECT_EQ(result.remaining, Quantity(0));
    EXPECT_EQ(result.resting, nullptr);
    EXPECT_EQ(book.levelQuantity(Side::Sell, Ticks(152)), Quantity(3));
}

TEST(MatchingEngine, FifoWithinLevelPreserved) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);
    book.insert(makeOrder(1, 150, 5, Side::Sell));
    book.insert(makeOrder(2, 150, 5, Side::Sell));

    SubmitResult result = engine.submitLimit(makeOrder(3, 150, 7, Side::Buy));
    ASSERT_EQ(result.trades.size(), 2U);
    expectTrade(result.trades[0], 1, 3, 150, 5, Side::Buy);
    expectTrade(result.trades[1], 2, 3, 150, 2, Side::Buy);
    EXPECT_EQ(book.levelQuantity(Side::Sell, Ticks(150)), Quantity(3));
}

TEST(MatchingEngine, CrossingIntoEmptyFarSideRestsWholeOrder) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);

    SubmitResult result = engine.submitLimit(makeOrder(1, 150, 10, Side::Sell));
    EXPECT_TRUE(result.trades.empty());
    EXPECT_EQ(result.remaining, Quantity(10));
    ASSERT_NE(result.resting, nullptr);
    EXPECT_EQ(book.bestAsk(), Ticks(150));
}

TEST(MatchingEngine, SellTakerCrossesBidsWithAggressorSideSell) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);
    book.insert(makeOrder(1, 150, 10, Side::Buy));

    SubmitResult result = engine.submitLimit(makeOrder(2, 150, 10, Side::Sell));
    ASSERT_EQ(result.trades.size(), 1U);
    expectTrade(result.trades[0], 1, 2, 150, 10, Side::Sell);
    EXPECT_EQ(book.size(), 0U);
}

TEST(MatchingEngine, TradePriceIsMakersNotTakers) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);
    book.insert(makeOrder(1, 148, 10, Side::Sell)); // maker willing to sell cheaper than taker's limit

    SubmitResult result = engine.submitLimit(makeOrder(2, 150, 10, Side::Buy)); // taker willing to pay up to 150
    ASSERT_EQ(result.trades.size(), 1U);
    EXPECT_EQ(result.trades[0].price, Ticks(148)); // price improvement: maker's price, not taker's
}

TEST(MatchingEngine, RemainderRejectedOutOfBandIsDistinctFromFullyFilled) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);

    SubmitResult result = engine.submitLimit(makeOrder(1, BAND_MAX + 1, 10, Side::Buy));
    EXPECT_TRUE(result.trades.empty());
    EXPECT_EQ(result.remaining, Quantity(10)); // unmatched, not fully filled
    EXPECT_EQ(result.resting, nullptr);        // rejected, not resting
    EXPECT_EQ(book.size(), 0U);
}

// Differential test (SPA-7): a narrow price band forces heavy crossing, and
// every submit is checked trade-for-trade against NaiveMatcher, plus the
// resulting best bid/ask. Stops at the first divergence via ASSERT so a
// failure points straight at the op that broke it.
TEST(MatchingEngine, DifferentialAgainstNaiveMatcherOverRandomCrossingScenarios) {
    OrderBook book = makeBook(2048);
    MatchingEngine engine(book);
    NaiveMatcher naive;
    Rng rng(0xC0FFEE);

    constexpr uint64_t OP_COUNT = 2000;
    constexpr uint64_t PRICE_SPAN = 21; // BAND_MIN..BAND_MIN+20, well inside [BAND_MIN, BAND_MAX]
    constexpr uint64_t MAX_QTY = 20;

    for (uint64_t i = 1; i <= OP_COUNT; ++i) {
        const Side side = boundedRand(rng, 2) == 0 ? Side::Buy : Side::Sell;
        const int64_t price = BAND_MIN + static_cast<int64_t>(boundedRand(rng, PRICE_SPAN));
        const uint64_t qty = 1 + boundedRand(rng, MAX_QTY);
        const Order order = makeOrder(i, price, qty, side);

        const SubmitResult real_result = engine.submitLimit(order);
        const std::vector<Trade> naive_trades = naive.submitLimit(order);

        ASSERT_EQ(real_result.trades.size(), naive_trades.size()) << "op " << i;
        for (std::size_t t = 0; t < real_result.trades.size(); ++t) {
            ASSERT_EQ(real_result.trades[t].maker_id, naive_trades[t].maker_id) << "op " << i << " trade " << t;
            ASSERT_EQ(real_result.trades[t].taker_id, naive_trades[t].taker_id) << "op " << i << " trade " << t;
            ASSERT_EQ(real_result.trades[t].price, naive_trades[t].price) << "op " << i << " trade " << t;
            ASSERT_EQ(real_result.trades[t].quantity, naive_trades[t].quantity) << "op " << i << " trade " << t;
            ASSERT_EQ(real_result.trades[t].aggressor_side, naive_trades[t].aggressor_side)
                << "op " << i << " trade " << t;
        }

        if (naive.bids().empty()) {
            ASSERT_EQ(book.bestBid(), std::nullopt) << "op " << i;
        } else {
            ASSERT_EQ(book.bestBid(), Ticks(naive.bids().begin()->first)) << "op " << i;
        }
        if (naive.asks().empty()) {
            ASSERT_EQ(book.bestAsk(), std::nullopt) << "op " << i;
        } else {
            ASSERT_EQ(book.bestAsk(), Ticks(naive.asks().begin()->first)) << "op " << i;
        }
    }
}

} // namespace
