#ifndef ORDER_MATCHING_ENGINE_NAIVE_ORDER_BOOK_HPP
#define ORDER_MATCHING_ENGINE_NAIVE_ORDER_BOOK_HPP

#include "ome/types/order.hpp"
#include "ome/types/order_id.hpp"
#include "ome/types/quantity.hpp"
#include "ome/types/side.hpp"
#include "ome/types/ticks.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

// Deliberately naive reference book (SPA-6): plain library containers only,
// no pooling and no open addressing, so it is obviously correct by
// inspection and gives OrderBook's pooled/hashed internals something
// independent to be checked against in the fuzz harness. Mirrors OrderBook's
// public insert/cancel/query surface and its exact rejection rules
// (out-of-band price, duplicate id, capacity reached) so success/failure can
// be compared 1:1 between the two books.
class NaiveOrderBook {
  public:
    NaiveOrderBook(Ticks min_tick_, Ticks max_tick_, std::size_t capacity_);

    bool insert(const Order &order);
    bool cancel(OrderId id);

    std::optional<Ticks> bestBid() const;
    std::optional<Ticks> bestAsk() const;

    Quantity levelQuantity(Side side, Ticks price) const;
    std::size_t levelOrderCount(Side side, Ticks price) const;
    std::vector<uint64_t> levelOrderIds(Side side, Ticks price) const; // FIFO front -> back

    // Prices with at least one resting order, ascending. Lets a full-book
    // sweep walk exactly the occupied levels instead of the whole band.
    std::vector<Ticks> occupiedPrices(Side side) const;

    // Where a live id is currently resting, or nullopt if it isn't live.
    // OrderBook has no equivalent public query; the invariant checker needs
    // this before a cancel mutates state so it knows which level to recheck.
    std::optional<std::pair<Side, Ticks>> locate(OrderId id) const;

    std::size_t size() const { return resting_count; }
    std::size_t capacity() const { return book_capacity; }

  private:
    struct Resting {
        uint64_t order_id;
        uint64_t quantity;
    };

    using Ladder = std::map<int64_t, std::deque<Resting>>; // price -> FIFO queue

    bool inBand(int64_t ticks) const { return ticks >= min_tick && ticks <= max_tick; }
    Ladder &ladder(Side side) { return side == Side::Buy ? bids : asks; }
    const Ladder &ladder(Side side) const { return side == Side::Buy ? bids : asks; }

    int64_t min_tick;
    int64_t max_tick;
    std::size_t book_capacity;
    std::size_t resting_count = 0;
    Ladder bids; // ascending by price; best bid is rbegin()
    Ladder asks; // ascending by price; best ask is begin()
    std::unordered_map<uint64_t, std::pair<Side, int64_t>> id_location;
};

#endif // ORDER_MATCHING_ENGINE_NAIVE_ORDER_BOOK_HPP
