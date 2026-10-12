#include "ome/book/order_book.hpp"
#include "ome/matching/matching_engine.hpp"
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
    EXPECT_EQ(result.status, SubmitStatus::Rested);
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
    EXPECT_EQ(result.status, SubmitStatus::Filled);
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
    EXPECT_EQ(result.status, SubmitStatus::Rested);
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
    EXPECT_EQ(result.status, SubmitStatus::Filled);
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
    EXPECT_EQ(result.status, SubmitStatus::Filled);
    ASSERT_EQ(result.trades.size(), 2U);
    expectTrade(result.trades[0], 1, 3, 150, 5, Side::Buy);
    expectTrade(result.trades[1], 2, 3, 150, 2, Side::Buy);
    EXPECT_EQ(book.levelQuantity(Side::Sell, Ticks(150)), Quantity(3));
}

TEST(MatchingEngine, CrossingIntoEmptyFarSideRestsWholeOrder) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);

    SubmitResult result = engine.submitLimit(makeOrder(1, 150, 10, Side::Sell));
    EXPECT_EQ(result.status, SubmitStatus::Rested);
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
    EXPECT_EQ(result.status, SubmitStatus::Filled);
    ASSERT_EQ(result.trades.size(), 1U);
    expectTrade(result.trades[0], 1, 2, 150, 10, Side::Sell);
    EXPECT_EQ(book.size(), 0U);
}

TEST(MatchingEngine, TradePriceIsMakersNotTakers) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);
    book.insert(makeOrder(1, 148, 10, Side::Sell)); // maker willing to sell cheaper than taker's limit

    SubmitResult result = engine.submitLimit(makeOrder(2, 150, 10, Side::Buy)); // taker willing to pay up to 150
    EXPECT_EQ(result.status, SubmitStatus::Filled);
    ASSERT_EQ(result.trades.size(), 1U);
    EXPECT_EQ(result.trades[0].price, Ticks(148)); // price improvement: maker's price, not taker's
}

TEST(MatchingEngine, RemainderRejectedOutOfBandIsDistinctFromFullyFilled) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);

    SubmitResult result = engine.submitLimit(makeOrder(1, BAND_MAX + 1, 10, Side::Buy));
    EXPECT_EQ(result.status, SubmitStatus::RestRejected);
    EXPECT_TRUE(result.trades.empty());
    EXPECT_EQ(result.remaining, Quantity(10)); // unmatched, not fully filled
    EXPECT_EQ(result.resting, nullptr);        // rejected, not resting
    EXPECT_EQ(book.size(), 0U);
}

Order makeTyped(const uint64_t id, const int64_t ticks, const uint64_t qty, const Side side, const OrderType type) {
    return Order{OrderId(id), Price(Ticks(ticks), TickSize(CENT_TICK_SIZE)), Quantity(qty), side, type};
}

TEST(MatchingEngine, SubmitLimitRestsRemainderSameAsSubmitLimit) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);
    book.insert(makeOrder(1, 150, 10, Side::Sell));

    SubmitResult result = engine.submit(makeTyped(2, 150, 15, Side::Buy, OrderType::Limit));
    EXPECT_EQ(result.status, SubmitStatus::Rested);
    ASSERT_EQ(result.trades.size(), 1U);
    expectTrade(result.trades[0], 1, 2, 150, 10, Side::Buy);
    EXPECT_EQ(result.remaining, Quantity(5));
    ASSERT_NE(result.resting, nullptr);
    EXPECT_EQ(book.bestBid(), Ticks(150));
}

TEST(MatchingEngine, MarketIgnoresPriceSweepsAndCancelsRemainder) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);
    book.insert(makeOrder(1, 150, 5, Side::Sell));
    book.insert(makeOrder(2, 160, 5, Side::Sell));

    // price field 100 would cross nothing as a limit; market ignores it.
    SubmitResult result = engine.submit(makeTyped(3, 100, 12, Side::Buy, OrderType::Market));
    EXPECT_EQ(result.status, SubmitStatus::Cancelled);
    ASSERT_EQ(result.trades.size(), 2U);
    expectTrade(result.trades[0], 1, 3, 150, 5, Side::Buy);
    expectTrade(result.trades[1], 2, 3, 160, 5, Side::Buy);
    EXPECT_EQ(result.remaining, Quantity(2));
    EXPECT_EQ(result.resting, nullptr);
    EXPECT_EQ(book.size(), 0U); // remainder cancelled, never rests
}

TEST(MatchingEngine, IocMatchesWhatCrossesAndCancelsRemainder) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);
    book.insert(makeOrder(1, 150, 5, Side::Sell));
    book.insert(makeOrder(2, 151, 5, Side::Sell));

    SubmitResult result = engine.submit(makeTyped(3, 150, 8, Side::Buy, OrderType::IOC));
    EXPECT_EQ(result.status, SubmitStatus::Cancelled);
    ASSERT_EQ(result.trades.size(), 1U);
    expectTrade(result.trades[0], 1, 3, 150, 5, Side::Buy);
    EXPECT_EQ(result.remaining, Quantity(3));
    EXPECT_EQ(result.resting, nullptr);
    EXPECT_EQ(book.size(), 1U); // only the untouched 151 ask
    EXPECT_FALSE(book.bestBid().has_value());
}

TEST(MatchingEngine, FokFillsWhenWholeQuantityCrosses) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);
    book.insert(makeOrder(1, 150, 5, Side::Sell));
    book.insert(makeOrder(2, 151, 5, Side::Sell));

    SubmitResult result = engine.submit(makeTyped(3, 151, 10, Side::Buy, OrderType::FOK));
    EXPECT_EQ(result.status, SubmitStatus::Filled);
    ASSERT_EQ(result.trades.size(), 2U);
    EXPECT_EQ(result.remaining, Quantity(0));
    EXPECT_EQ(result.resting, nullptr);
    EXPECT_EQ(book.size(), 0U);
}

TEST(MatchingEngine, FokKillLeavesBookUntouched) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);
    book.insert(makeOrder(1, 150, 5, Side::Sell));
    book.insert(makeOrder(2, 151, 5, Side::Sell));

    SubmitResult result = engine.submit(makeTyped(3, 151, 11, Side::Buy, OrderType::FOK)); // one lot short
    EXPECT_EQ(result.status, SubmitStatus::Cancelled);
    EXPECT_TRUE(result.trades.empty());
    EXPECT_EQ(result.remaining, Quantity(11));
    EXPECT_EQ(result.resting, nullptr);
    EXPECT_EQ(book.size(), 2U);
    EXPECT_EQ(book.levelQuantity(Side::Sell, Ticks(150)), Quantity(5));
    EXPECT_EQ(book.levelQuantity(Side::Sell, Ticks(151)), Quantity(5));
}

// FNV-1a over every resting order in band, both sides, in FIFO order:
// side, price, id and quantity per order, plus the best quotes and size.
// A full snapshot fingerprint, unlike the replay hash (which folds logged
// commands), so "book untouched" can be asserted as one equality.
uint64_t bookStateHash(const OrderBook &book) {
    constexpr uint64_t FNV_OFFSET_BASIS = 0xCBF29CE484222325ULL;
    constexpr uint64_t FNV_PRIME = 0x100000001B3ULL;
    uint64_t hash = FNV_OFFSET_BASIS;
    const auto mix = [&hash](const uint64_t value) {
        for (int i = 0; i < 8; ++i) {
            hash ^= (value >> (8 * i)) & 0xFF;
            hash *= FNV_PRIME;
        }
    };
    for (const Side side : {Side::Buy, Side::Sell}) {
        for (int64_t ticks = BAND_MIN; ticks <= BAND_MAX; ++ticks) {
            for (const OrderNode *node = book.levelFront(side, Ticks(ticks)); node != nullptr; node = node->next) {
                mix(static_cast<uint64_t>(side));
                mix(static_cast<uint64_t>(ticks));
                mix(node->order_id);
                mix(node->quantity);
            }
        }
    }
    const std::optional<Ticks> best_bid = book.bestBid();
    const std::optional<Ticks> best_ask = book.bestAsk();
    mix(best_bid.has_value() ? static_cast<uint64_t>(best_bid->getValue()) : 0);
    mix(best_ask.has_value() ? static_cast<uint64_t>(best_ask->getValue()) : 0);
    mix(book.size());
    return hash;
}

// Two-sided book with depth beyond the inside, used by the FOK/IOC
// untouched-book checks: asks 150x5, 150x5, 152x10, 155x20; bids 148x5,
// 147x10, 145x20.
void seedTwoSidedBook(OrderBook &book) {
    book.insert(makeOrder(1, 150, 5, Side::Sell));
    book.insert(makeOrder(2, 150, 5, Side::Sell));
    book.insert(makeOrder(3, 152, 10, Side::Sell));
    book.insert(makeOrder(4, 155, 20, Side::Sell));
    book.insert(makeOrder(5, 148, 5, Side::Buy));
    book.insert(makeOrder(6, 147, 10, Side::Buy));
    book.insert(makeOrder(7, 145, 20, Side::Buy));
}

TEST(MatchingEngine, BookStateHashSeesSingleLotChange) {
    // Guards the helper itself: if it missed changes, every "hash
    // unchanged" assertion below would pass vacuously.
    OrderBook book = makeBook();
    MatchingEngine engine(book);
    seedTwoSidedBook(book);
    const uint64_t before = bookStateHash(book);
    engine.submit(makeTyped(100, 150, 1, Side::Buy, OrderType::IOC));
    EXPECT_NE(bookStateHash(book), before);
}

// --- Market ---

TEST(MatchingEngine, MarketOnEmptyBookCancelsWholeOrder) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);

    SubmitResult result = engine.submit(makeTyped(1, 150, 10, Side::Buy, OrderType::Market));
    EXPECT_EQ(result.status, SubmitStatus::Cancelled);
    EXPECT_TRUE(result.trades.empty());
    EXPECT_EQ(result.remaining, Quantity(10));
    EXPECT_EQ(result.resting, nullptr);
    EXPECT_EQ(book.size(), 0U);
    EXPECT_FALSE(book.bestBid().has_value()); // price field would have rested a limit here
}

TEST(MatchingEngine, MarketExactFillIsFilled) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);
    seedTwoSidedBook(book);

    SubmitResult result = engine.submit(makeTyped(100, 150, 20, Side::Buy, OrderType::Market));
    EXPECT_EQ(result.status, SubmitStatus::Filled);
    ASSERT_EQ(result.trades.size(), 3U);
    expectTrade(result.trades[0], 1, 100, 150, 5, Side::Buy);
    expectTrade(result.trades[1], 2, 100, 150, 5, Side::Buy);
    expectTrade(result.trades[2], 3, 100, 152, 10, Side::Buy);
    EXPECT_EQ(result.remaining, Quantity(0));
    EXPECT_EQ(book.bestAsk(), Ticks(155));
}

TEST(MatchingEngine, MarketSellSweepsBidsBestFirstAndCancelsRemainder) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);
    seedTwoSidedBook(book);

    SubmitResult result = engine.submit(makeTyped(100, 200, 40, Side::Sell, OrderType::Market));
    EXPECT_EQ(result.status, SubmitStatus::Cancelled);
    ASSERT_EQ(result.trades.size(), 3U);
    expectTrade(result.trades[0], 5, 100, 148, 5, Side::Sell);
    expectTrade(result.trades[1], 6, 100, 147, 10, Side::Sell);
    expectTrade(result.trades[2], 7, 100, 145, 20, Side::Sell);
    EXPECT_EQ(result.remaining, Quantity(5));
    EXPECT_FALSE(book.bestBid().has_value());
    EXPECT_EQ(book.bestAsk(), Ticks(150)); // remainder did not rest as an ask
    EXPECT_EQ(book.size(), 4U);
}

// --- IOC ---

TEST(MatchingEngine, IocNoCrossCancelsWithBookUntouched) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);
    seedTwoSidedBook(book);
    const uint64_t before = bookStateHash(book);

    SubmitResult result = engine.submit(makeTyped(100, 149, 10, Side::Buy, OrderType::IOC));
    EXPECT_EQ(result.status, SubmitStatus::Cancelled);
    EXPECT_TRUE(result.trades.empty());
    EXPECT_EQ(result.remaining, Quantity(10));
    EXPECT_EQ(result.resting, nullptr);
    EXPECT_EQ(bookStateHash(book), before);
}

TEST(MatchingEngine, IocFullFillIsFilled) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);
    seedTwoSidedBook(book);

    SubmitResult result = engine.submit(makeTyped(100, 152, 15, Side::Buy, OrderType::IOC));
    EXPECT_EQ(result.status, SubmitStatus::Filled);
    ASSERT_EQ(result.trades.size(), 3U);
    expectTrade(result.trades[2], 3, 100, 152, 5, Side::Buy);
    EXPECT_EQ(result.remaining, Quantity(0));
    EXPECT_EQ(book.levelQuantity(Side::Sell, Ticks(152)), Quantity(5));
}

TEST(MatchingEngine, IocSellStopsAtLimitAndCancelsRemainder) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);
    seedTwoSidedBook(book);

    SubmitResult result = engine.submit(makeTyped(100, 147, 30, Side::Sell, OrderType::IOC));
    EXPECT_EQ(result.status, SubmitStatus::Cancelled);
    ASSERT_EQ(result.trades.size(), 2U);
    expectTrade(result.trades[0], 5, 100, 148, 5, Side::Sell);
    expectTrade(result.trades[1], 6, 100, 147, 10, Side::Sell);
    EXPECT_EQ(result.remaining, Quantity(15));
    EXPECT_EQ(book.bestBid(), Ticks(145)); // 145 is beyond the limit: untouched
    EXPECT_EQ(book.levelQuantity(Side::Buy, Ticks(145)), Quantity(20));
    EXPECT_EQ(book.bestAsk(), Ticks(150)); // remainder did not rest as an ask
}

// --- FOK ---

TEST(MatchingEngine, FokWithSurplusFillsAndLeavesRestOfLastLevel) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);
    seedTwoSidedBook(book);

    SubmitResult result = engine.submit(makeTyped(100, 155, 25, Side::Buy, OrderType::FOK));
    EXPECT_EQ(result.status, SubmitStatus::Filled);
    ASSERT_EQ(result.trades.size(), 4U);
    expectTrade(result.trades[3], 4, 100, 155, 5, Side::Buy);
    EXPECT_EQ(result.remaining, Quantity(0));
    EXPECT_EQ(book.levelQuantity(Side::Sell, Ticks(155)), Quantity(15));
}

TEST(MatchingEngine, FokKillLeavesBookStateHashUnchanged) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);
    seedTwoSidedBook(book);
    const uint64_t before = bookStateHash(book);

    // 20 available at or below 152; ask for 21.
    SubmitResult result = engine.submit(makeTyped(100, 152, 21, Side::Buy, OrderType::FOK));
    EXPECT_EQ(result.status, SubmitStatus::Cancelled);
    EXPECT_TRUE(result.trades.empty());
    EXPECT_EQ(result.remaining, Quantity(21));
    EXPECT_EQ(result.resting, nullptr);
    EXPECT_EQ(bookStateHash(book), before);
}

TEST(MatchingEngine, FokKilledWhenDepthExistsOnlyBeyondLimit) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);
    seedTwoSidedBook(book);
    const uint64_t before = bookStateHash(book);

    // Whole ask side (40) would cover 30, but only 10 is at or below 150.
    SubmitResult result = engine.submit(makeTyped(100, 150, 30, Side::Buy, OrderType::FOK));
    EXPECT_EQ(result.status, SubmitStatus::Cancelled);
    EXPECT_TRUE(result.trades.empty());
    EXPECT_EQ(bookStateHash(book), before);
}

TEST(MatchingEngine, FokSellKillLeavesBookStateHashUnchanged) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);
    seedTwoSidedBook(book);
    const uint64_t before = bookStateHash(book);

    // 15 available at or above 147; ask for 16.
    SubmitResult result = engine.submit(makeTyped(100, 147, 16, Side::Sell, OrderType::FOK));
    EXPECT_EQ(result.status, SubmitStatus::Cancelled);
    EXPECT_TRUE(result.trades.empty());
    EXPECT_EQ(result.remaining, Quantity(16));
    EXPECT_EQ(bookStateHash(book), before);
}

TEST(MatchingEngine, FokSellExactFillIsFilled) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);
    seedTwoSidedBook(book);

    SubmitResult result = engine.submit(makeTyped(100, 147, 15, Side::Sell, OrderType::FOK));
    EXPECT_EQ(result.status, SubmitStatus::Filled);
    ASSERT_EQ(result.trades.size(), 2U);
    expectTrade(result.trades[0], 5, 100, 148, 5, Side::Sell);
    expectTrade(result.trades[1], 6, 100, 147, 10, Side::Sell);
    EXPECT_EQ(book.bestBid(), Ticks(145));
}

TEST(MatchingEngine, FokOnEmptyBookKilled) {
    OrderBook book = makeBook();
    MatchingEngine engine(book);

    SubmitResult result = engine.submit(makeTyped(1, 150, 10, Side::Buy, OrderType::FOK));
    EXPECT_EQ(result.status, SubmitStatus::Cancelled);
    EXPECT_TRUE(result.trades.empty());
    EXPECT_EQ(result.remaining, Quantity(10));
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
