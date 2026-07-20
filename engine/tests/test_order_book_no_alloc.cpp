// Verifies the OrderBook hot path (insert, remove, best-quote reads, level
// iteration) performs no heap allocation. Global operator new/delete are
// replaced binary-wide with counting versions; this must be the only TU that
// defines them.

#include "ome/book/order_book.hpp"
#include "ome/types/order.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define OME_ASAN 1
#endif
#elif defined(__SANITIZE_ADDRESS__)
#define OME_ASAN 1
#endif

// The Windows ASan runtime provides operator new/delete itself, so replacing
// them is a duplicate-symbol link error there. The test skips in that one
// configuration; ELF ASan builds (CI) and non-ASan builds count for real.
#if defined(_WIN32) && defined(OME_ASAN)
#define OME_ALLOC_COUNTING 0
#else
#define OME_ALLOC_COUNTING 1
#endif

namespace {
std::size_t allocation_count = 0;
} // namespace

#if OME_ALLOC_COUNTING

// The full replacement set (plain, nothrow, array) must be provided together:
// with only a partial set, an allocation through an unreplaced form (gtest uses
// nothrow new for its temporary sort buffer) gets freed by a replaced delete,
// and ASan aborts with alloc-dealloc-mismatch.

namespace {
void *countingAllocate(const std::size_t size) noexcept {
    ++allocation_count;
    return std::malloc(size == 0 ? 1 : size);
}
} // namespace

void *operator new(const std::size_t size) {
    void *ptr = countingAllocate(size);
    if (ptr == nullptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

void *operator new[](const std::size_t size) {
    void *ptr = countingAllocate(size);
    if (ptr == nullptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

void *operator new(const std::size_t size, const std::nothrow_t & /*tag*/) noexcept { return countingAllocate(size); }

void *operator new[](const std::size_t size, const std::nothrow_t & /*tag*/) noexcept { return countingAllocate(size); }

void operator delete(void *ptr) noexcept { std::free(ptr); }

void operator delete[](void *ptr) noexcept { std::free(ptr); }

void operator delete(void *ptr, std::size_t) noexcept { std::free(ptr); }

void operator delete[](void *ptr, std::size_t) noexcept { std::free(ptr); }

void operator delete(void *ptr, const std::nothrow_t & /*tag*/) noexcept { std::free(ptr); }

void operator delete[](void *ptr, const std::nothrow_t & /*tag*/) noexcept { std::free(ptr); }

#endif // OME_ALLOC_COUNTING

namespace {

constexpr uint8_t CENT_TICK_SIZE = 0;

Order makeOrder(const uint64_t id, const int64_t ticks, const uint64_t qty, const Side side) {
    return Order{OrderId(id), Price(Ticks(ticks), TickSize(CENT_TICK_SIZE)), Quantity(qty), side, OrderType::Limit};
}

TEST(OrderBookNoAlloc, HotPathDoesNotAllocate) {
#if !OME_ALLOC_COUNTING
    GTEST_SKIP() << "global operator new replacement collides with the Windows ASan runtime";
#endif
    OrderBook book(Ticks(100), Ticks(200), 8); // construction may allocate; hot path below must not

    // Plain scalars only inside the measured region: gtest assertions can
    // allocate, so all checks run after the second snapshot.
    const std::size_t allocations_before = allocation_count;

    OrderNode *bid_a = book.insert(makeOrder(1, 150, 10, Side::Buy));
    OrderNode *bid_b = book.insert(makeOrder(2, 150, 20, Side::Buy));
    OrderNode *bid_c = book.insert(makeOrder(3, 150, 30, Side::Buy));
    OrderNode *bid_low = book.insert(makeOrder(4, 140, 40, Side::Buy));
    OrderNode *ask_a = book.insert(makeOrder(5, 160, 50, Side::Sell));
    OrderNode *rejected = book.insert(makeOrder(6, 300, 60, Side::Buy));

    const bool best_ok = book.bestBid() == Ticks(150) && book.bestAsk() == Ticks(160);

    uint64_t fifo_ids = 0;
    for (const OrderNode *node = book.levelFront(Side::Buy, Ticks(150)); node != nullptr; node = node->next) {
        fifo_ids = fifo_ids * 10 + node->order_id;
    }
    const uint64_t level_quantity = book.levelQuantity(Side::Buy, Ticks(150)).getValue();
    const std::size_t level_count = book.levelOrderCount(Side::Buy, Ticks(150));

    book.remove(bid_b); // middle
    book.remove(bid_a); // head
    book.remove(bid_c); // tail: empties best bid level, triggers downward scan
    const bool bid_fell_back = book.bestBid() == Ticks(140);

    OrderNode *reused = book.insert(makeOrder(7, 150, 70, Side::Buy)); // from free list
    book.remove(reused);
    book.remove(bid_low);
    book.remove(ask_a);
    const bool emptied = book.size() == 0 && !book.bestBid().has_value() && !book.bestAsk().has_value();

    const std::size_t allocations_after = allocation_count;

    EXPECT_EQ(allocations_after, allocations_before);
    EXPECT_NE(bid_a, nullptr);
    EXPECT_NE(reused, nullptr);
    EXPECT_EQ(rejected, nullptr);
    EXPECT_TRUE(best_ok);
    EXPECT_EQ(fifo_ids, 123U);
    EXPECT_EQ(level_quantity, 60U);
    EXPECT_EQ(level_count, 3U);
    EXPECT_TRUE(bid_fell_back);
    EXPECT_TRUE(emptied);
}

} // namespace
