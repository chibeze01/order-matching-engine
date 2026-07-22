#include "ome/book/order_book.hpp"
#include "ome/types/order.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

constexpr uint8_t CENT_TICK_SIZE = 0;
constexpr int64_t BAND_MIN = 100;
constexpr int64_t BAND_MAX = 200;
constexpr std::size_t CAPACITY = 16;

Order makeOrder(const uint64_t id, const int64_t ticks, const uint64_t qty, const Side side) {
    return Order{OrderId(id), Price(Ticks(ticks), TickSize(CENT_TICK_SIZE)), Quantity(qty), side, OrderType::Limit};
}

OrderBook makeBook(const std::size_t capacity = CAPACITY) {
    return OrderBook(Ticks(BAND_MIN), Ticks(BAND_MAX), capacity);
}

std::vector<uint64_t> levelIds(const OrderBook &book, const Side side, const int64_t ticks) {
    std::vector<uint64_t> ids;
    for (const OrderNode *node = book.levelFront(side, Ticks(ticks)); node != nullptr; node = node->next) {
        ids.push_back(node->order_id);
    }
    return ids;
}

TEST(OrderBookCancel, CancelHeadOfLevelPreservesFifo) {
    OrderBook book = makeBook();
    book.insert(makeOrder(1, 150, 10, Side::Buy));
    book.insert(makeOrder(2, 150, 20, Side::Buy));
    book.insert(makeOrder(3, 150, 30, Side::Buy));
    EXPECT_TRUE(book.cancel(OrderId(1)));
    EXPECT_EQ(levelIds(book, Side::Buy, 150), (std::vector<uint64_t>{2, 3}));
    EXPECT_EQ(book.levelQuantity(Side::Buy, Ticks(150)), Quantity(50));
}

TEST(OrderBookCancel, CancelMiddleOfLevelPreservesFifo) {
    OrderBook book = makeBook();
    book.insert(makeOrder(1, 150, 10, Side::Buy));
    book.insert(makeOrder(2, 150, 20, Side::Buy));
    book.insert(makeOrder(3, 150, 30, Side::Buy));
    EXPECT_TRUE(book.cancel(OrderId(2)));
    EXPECT_EQ(levelIds(book, Side::Buy, 150), (std::vector<uint64_t>{1, 3}));
}

TEST(OrderBookCancel, CancelTailOfLevelPreservesFifo) {
    OrderBook book = makeBook();
    book.insert(makeOrder(1, 150, 10, Side::Buy));
    book.insert(makeOrder(2, 150, 20, Side::Buy));
    book.insert(makeOrder(3, 150, 30, Side::Buy));
    EXPECT_TRUE(book.cancel(OrderId(3)));
    EXPECT_EQ(levelIds(book, Side::Buy, 150), (std::vector<uint64_t>{1, 2}));

    book.insert(makeOrder(4, 150, 40, Side::Buy));
    EXPECT_EQ(levelIds(book, Side::Buy, 150), (std::vector<uint64_t>{1, 2, 4}));
}

TEST(OrderBookCancel, CancelLastOrderCollapsesLevelAndUpdatesBestQuote) {
    OrderBook book = makeBook();
    book.insert(makeOrder(1, 140, 10, Side::Buy)); // gap below best bid
    book.insert(makeOrder(2, 160, 10, Side::Buy));
    book.insert(makeOrder(3, 155, 10, Side::Sell));
    book.insert(makeOrder(4, 170, 10, Side::Sell));

    EXPECT_TRUE(book.cancel(OrderId(2)));
    EXPECT_EQ(book.levelOrderCount(Side::Buy, Ticks(160)), 0U);
    EXPECT_EQ(book.bestBid(), Ticks(140));

    EXPECT_TRUE(book.cancel(OrderId(3)));
    EXPECT_EQ(book.bestAsk(), Ticks(170));
}

TEST(OrderBookCancel, DoubleCancelRejected) {
    OrderBook book = makeBook();
    book.insert(makeOrder(1, 150, 10, Side::Buy));
    EXPECT_TRUE(book.cancel(OrderId(1)));
    EXPECT_FALSE(book.cancel(OrderId(1)));
    EXPECT_EQ(book.size(), 0U);
}

TEST(OrderBookCancel, CancelUnknownIdRejected) {
    OrderBook book = makeBook();
    EXPECT_FALSE(book.cancel(OrderId(42)));
    book.insert(makeOrder(1, 150, 10, Side::Buy));
    EXPECT_FALSE(book.cancel(OrderId(42)));
    EXPECT_EQ(book.size(), 1U);
}

TEST(OrderBookCancel, DuplicateIdInsertRejected) {
    OrderBook book = makeBook();
    EXPECT_NE(book.insert(makeOrder(1, 150, 10, Side::Buy)), nullptr);
    EXPECT_EQ(book.insert(makeOrder(1, 151, 20, Side::Buy)), nullptr);
    EXPECT_EQ(book.size(), 1U);

    EXPECT_TRUE(book.cancel(OrderId(1))); // id free again after cancel
    EXPECT_NE(book.insert(makeOrder(1, 151, 20, Side::Buy)), nullptr);
}

TEST(OrderBookCancel, RemoveByHandleThenCancelRejected) {
    OrderBook book = makeBook();
    OrderNode *handle = book.insert(makeOrder(1, 150, 10, Side::Buy));
    ASSERT_NE(handle, nullptr);
    book.remove(handle);
    EXPECT_FALSE(book.cancel(OrderId(1)));
}

TEST(OrderBookCancel, CancelReturnsNodeToPool) {
    OrderBook book = makeBook(2);
    book.insert(makeOrder(1, 150, 10, Side::Buy));
    book.insert(makeOrder(2, 151, 10, Side::Buy));
    EXPECT_EQ(book.insert(makeOrder(3, 152, 10, Side::Buy)), nullptr); // pool full
    EXPECT_TRUE(book.cancel(OrderId(1)));
    EXPECT_NE(book.insert(makeOrder(3, 152, 10, Side::Buy)), nullptr);
}

TEST(OrderBookCancel, ChurnKeepsIndexAndPoolCoherent) {
    OrderBook book = makeBook();
    // Far more distinct ids than table slots, with a sliding window of eight
    // resting orders, so probe chains, backward-shift deletion around occupied
    // neighbours, and free-list reuse all get exercised.
    constexpr uint64_t WINDOW = 8;
    for (uint64_t id = 1; id <= 1000; ++id) {
        const int64_t ticks = BAND_MIN + static_cast<int64_t>(id % 101);
        ASSERT_NE(book.insert(makeOrder(id, ticks, 10, Side::Buy)), nullptr);
        if (id > WINDOW) {
            ASSERT_TRUE(book.cancel(OrderId(id - WINDOW)));
            ASSERT_FALSE(book.cancel(OrderId(id - WINDOW)));
        }
    }
    for (uint64_t id = 1000 - WINDOW + 1; id <= 1000; ++id) {
        ASSERT_TRUE(book.cancel(OrderId(id)));
    }
    EXPECT_EQ(book.size(), 0U);
    EXPECT_EQ(book.bestBid(), std::nullopt);

    for (uint64_t id = 1; id <= CAPACITY; ++id) { // pool still fully usable
        EXPECT_NE(book.insert(makeOrder(id, 150, 10, Side::Buy)), nullptr);
    }
    EXPECT_EQ(book.size(), CAPACITY);
}

} // namespace
