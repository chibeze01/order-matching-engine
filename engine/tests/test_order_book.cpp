#include "ome/book/order_book.hpp"
#include "ome/types/order.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
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

TEST(OrderBook, ConstructorRejectsBadBand) {
    EXPECT_THROW(OrderBook(Ticks(-1), Ticks(10), CAPACITY), std::invalid_argument);
    EXPECT_THROW(OrderBook(Ticks(10), Ticks(9), CAPACITY), std::invalid_argument);
    EXPECT_THROW(OrderBook(Ticks(10), Ticks(20), 0), std::invalid_argument);
}

TEST(OrderBook, InsertReturnsHandleAndUpdatesSize) {
    OrderBook book = makeBook();
    OrderNode *handle = book.insert(makeOrder(1, 150, 10, Side::Buy));
    ASSERT_NE(handle, nullptr);
    EXPECT_EQ(handle->order_id, 1U);
    EXPECT_EQ(handle->price_ticks, 150);
    EXPECT_EQ(handle->quantity, 10U);
    EXPECT_EQ(handle->side, Side::Buy);
    EXPECT_EQ(book.size(), 1U);
}

TEST(OrderBook, RemoveUpdatesSize) {
    OrderBook book = makeBook();
    OrderNode *handle = book.insert(makeOrder(1, 150, 10, Side::Buy));
    ASSERT_NE(handle, nullptr);
    book.remove(handle);
    EXPECT_EQ(book.size(), 0U);
    EXPECT_EQ(book.levelOrderCount(Side::Buy, Ticks(150)), 0U);
}

TEST(OrderBook, BestBidAskEmptyBookNullopt) {
    OrderBook book = makeBook();
    EXPECT_EQ(book.bestBid(), std::nullopt);
    EXPECT_EQ(book.bestAsk(), std::nullopt);
}

TEST(OrderBook, BestBidTracksHighestBuy) {
    OrderBook book = makeBook();
    book.insert(makeOrder(1, 150, 10, Side::Buy));
    EXPECT_EQ(book.bestBid(), Ticks(150));
    book.insert(makeOrder(2, 140, 10, Side::Buy));
    EXPECT_EQ(book.bestBid(), Ticks(150));
    book.insert(makeOrder(3, 160, 10, Side::Buy));
    EXPECT_EQ(book.bestBid(), Ticks(160));
}

TEST(OrderBook, BestAskTracksLowestSell) {
    OrderBook book = makeBook();
    book.insert(makeOrder(1, 150, 10, Side::Sell));
    EXPECT_EQ(book.bestAsk(), Ticks(150));
    book.insert(makeOrder(2, 160, 10, Side::Sell));
    EXPECT_EQ(book.bestAsk(), Ticks(150));
    book.insert(makeOrder(3, 140, 10, Side::Sell));
    EXPECT_EQ(book.bestAsk(), Ticks(140));
}

TEST(OrderBook, BestQuoteFallsToNextOccupiedLevelOnEmptyingBest) {
    OrderBook book = makeBook();
    book.insert(makeOrder(1, 140, 10, Side::Buy)); // gap between 140 and 160
    OrderNode *best_bid = book.insert(makeOrder(2, 160, 10, Side::Buy));
    book.insert(makeOrder(3, 170, 10, Side::Sell));
    OrderNode *best_ask = book.insert(makeOrder(4, 155, 10, Side::Sell));
    ASSERT_NE(best_bid, nullptr);
    ASSERT_NE(best_ask, nullptr);

    book.remove(best_bid);
    EXPECT_EQ(book.bestBid(), Ticks(140));
    book.remove(best_ask);
    EXPECT_EQ(book.bestAsk(), Ticks(170));
}

TEST(OrderBook, BestQuoteNulloptAfterSideEmptiedAndRestoredOnReinsert) {
    OrderBook book = makeBook();
    OrderNode *bid = book.insert(makeOrder(1, 150, 10, Side::Buy));
    ASSERT_NE(bid, nullptr);
    book.remove(bid);
    EXPECT_EQ(book.bestBid(), std::nullopt);

    book.insert(makeOrder(2, 145, 10, Side::Buy));
    EXPECT_EQ(book.bestBid(), Ticks(145));
}

TEST(OrderBook, FifoOrderPreservedWithinLevel) {
    OrderBook book = makeBook();
    book.insert(makeOrder(1, 150, 10, Side::Buy));
    book.insert(makeOrder(2, 150, 20, Side::Buy));
    book.insert(makeOrder(3, 150, 30, Side::Buy));
    EXPECT_EQ(levelIds(book, Side::Buy, 150), (std::vector<uint64_t>{1, 2, 3}));
}

TEST(OrderBook, RemoveMiddleOfLevelPreservesFifo) {
    OrderBook book = makeBook();
    book.insert(makeOrder(1, 150, 10, Side::Buy));
    OrderNode *middle = book.insert(makeOrder(2, 150, 20, Side::Buy));
    book.insert(makeOrder(3, 150, 30, Side::Buy));
    ASSERT_NE(middle, nullptr);
    book.remove(middle);
    EXPECT_EQ(levelIds(book, Side::Buy, 150), (std::vector<uint64_t>{1, 3}));
}

TEST(OrderBook, RemoveHeadOfLevelPreservesFifo) {
    OrderBook book = makeBook();
    OrderNode *head = book.insert(makeOrder(1, 150, 10, Side::Buy));
    book.insert(makeOrder(2, 150, 20, Side::Buy));
    book.insert(makeOrder(3, 150, 30, Side::Buy));
    ASSERT_NE(head, nullptr);
    book.remove(head);
    EXPECT_EQ(levelIds(book, Side::Buy, 150), (std::vector<uint64_t>{2, 3}));
}

TEST(OrderBook, RemoveTailOfLevelPreservesFifo) {
    OrderBook book = makeBook();
    book.insert(makeOrder(1, 150, 10, Side::Buy));
    book.insert(makeOrder(2, 150, 20, Side::Buy));
    OrderNode *tail = book.insert(makeOrder(3, 150, 30, Side::Buy));
    ASSERT_NE(tail, nullptr);
    book.remove(tail);
    EXPECT_EQ(levelIds(book, Side::Buy, 150), (std::vector<uint64_t>{1, 2}));

    book.insert(makeOrder(4, 150, 40, Side::Buy));
    EXPECT_EQ(levelIds(book, Side::Buy, 150), (std::vector<uint64_t>{1, 2, 4}));
}

TEST(OrderBook, LevelAggregatesTrackInsertsAndRemoves) {
    OrderBook book = makeBook();
    book.insert(makeOrder(1, 150, 10, Side::Buy));
    OrderNode *second = book.insert(makeOrder(2, 150, 20, Side::Buy));
    EXPECT_EQ(book.levelQuantity(Side::Buy, Ticks(150)), Quantity(30));
    EXPECT_EQ(book.levelOrderCount(Side::Buy, Ticks(150)), 2U);

    ASSERT_NE(second, nullptr);
    book.remove(second);
    EXPECT_EQ(book.levelQuantity(Side::Buy, Ticks(150)), Quantity(10));
    EXPECT_EQ(book.levelOrderCount(Side::Buy, Ticks(150)), 1U);
}

TEST(OrderBook, InsertAtBandEdgesAccepted) {
    OrderBook book = makeBook();
    EXPECT_NE(book.insert(makeOrder(1, BAND_MIN, 10, Side::Buy)), nullptr);
    EXPECT_NE(book.insert(makeOrder(2, BAND_MAX, 10, Side::Sell)), nullptr);
    EXPECT_EQ(book.bestBid(), Ticks(BAND_MIN));
    EXPECT_EQ(book.bestAsk(), Ticks(BAND_MAX));
}

TEST(OrderBook, InsertOutsideBandRejected) {
    OrderBook book = makeBook();
    EXPECT_EQ(book.insert(makeOrder(1, BAND_MIN - 1, 10, Side::Buy)), nullptr);
    EXPECT_EQ(book.insert(makeOrder(2, BAND_MAX + 1, 10, Side::Sell)), nullptr);
    EXPECT_EQ(book.size(), 0U);
    EXPECT_EQ(book.bestBid(), std::nullopt);
    EXPECT_EQ(book.bestAsk(), std::nullopt);
}

TEST(OrderBook, PoolExhaustionRejectsInsert) {
    OrderBook book = makeBook(2);
    EXPECT_NE(book.insert(makeOrder(1, 150, 10, Side::Buy)), nullptr);
    EXPECT_NE(book.insert(makeOrder(2, 151, 10, Side::Buy)), nullptr);
    EXPECT_EQ(book.insert(makeOrder(3, 152, 10, Side::Buy)), nullptr);
    EXPECT_EQ(book.size(), 2U);
}

TEST(OrderBook, NodeReusedAfterRemove) {
    OrderBook book = makeBook(2);
    OrderNode *first = book.insert(makeOrder(1, 150, 10, Side::Buy));
    book.insert(makeOrder(2, 151, 10, Side::Buy));
    ASSERT_NE(first, nullptr);
    book.remove(first);
    EXPECT_NE(book.insert(makeOrder(3, 152, 10, Side::Buy)), nullptr);
    EXPECT_EQ(book.size(), 2U);
}

TEST(OrderBook, BothSidesCoexistAtSamePrice) {
    OrderBook book = makeBook();
    book.insert(makeOrder(1, 150, 10, Side::Buy));
    book.insert(makeOrder(2, 150, 20, Side::Sell));
    EXPECT_EQ(book.levelQuantity(Side::Buy, Ticks(150)), Quantity(10));
    EXPECT_EQ(book.levelQuantity(Side::Sell, Ticks(150)), Quantity(20));
    EXPECT_EQ(book.levelOrderCount(Side::Buy, Ticks(150)), 1U);
    EXPECT_EQ(book.levelOrderCount(Side::Sell, Ticks(150)), 1U);
    EXPECT_EQ(book.bestBid(), Ticks(150));
    EXPECT_EQ(book.bestAsk(), Ticks(150));
}

TEST(OrderBook, LevelReadsOutsideBandReportEmpty) {
    OrderBook book = makeBook();
    EXPECT_EQ(book.levelQuantity(Side::Buy, Ticks(BAND_MAX + 1)), Quantity(0));
    EXPECT_EQ(book.levelOrderCount(Side::Buy, Ticks(BAND_MAX + 1)), 0U);
    EXPECT_EQ(book.levelFront(Side::Buy, Ticks(BAND_MAX + 1)), nullptr);
}

} // namespace
