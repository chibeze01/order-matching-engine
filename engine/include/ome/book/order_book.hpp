#ifndef ORDER_MATCHING_ENGINE_ORDER_BOOK_HPP
#define ORDER_MATCHING_ENGINE_ORDER_BOOK_HPP

#include "ome/types/order.hpp"
#include "ome/types/order_id.hpp"
#include "ome/types/quantity.hpp"
#include "ome/types/side.hpp"
#include "ome/types/ticks.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// One resting order inside a price level's FIFO queue. Nodes live in the
// OrderBook's preallocated pool; a pointer to a node is the O(1) cancel handle.
// Fields are raw scalars because the wrapper types (OrderId, Price, Quantity)
// have const members and cannot be reassigned when a pooled node is reused.
struct OrderNode {
    uint64_t order_id = 0;
    int64_t price_ticks = 0;
    uint64_t quantity = 0;
    Side side = Side::Buy;
    OrderNode *prev = nullptr;
    OrderNode *next = nullptr; // doubles as the free-list link when pooled
};

// Result of consumeBestFront: what got taken off the front order, and
// whether that order left the book (fully filled) or is still resting with
// quantity left.
struct ConsumeResult {
    OrderId order_id;
    Ticks price;
    Quantity filled;
    bool order_fully_filled;
};

// Price ladder: one contiguous array of levels per side, indexed by tick offset
// from min_tick, each level an intrusive doubly-linked FIFO queue. All heap
// allocation happens in the constructor; insert/remove never allocate.
// Single-writer: no internal synchronisation.
class OrderBook {
  public:
    // Band is inclusive at both edges. Throws std::invalid_argument if
    // min_tick_ is negative, min_tick_ > max_tick_, or capacity_ is zero.
    OrderBook(Ticks min_tick_, Ticks max_tick_, std::size_t capacity_);

    // Returns the cancel handle, or nullptr if the price is outside the band,
    // the pool is exhausted, or the id is already resting (duplicate ids would
    // corrupt the cancel index). Never throws, never allocates.
    OrderNode *insert(const Order &order);

    // O(1) unlink; also drops the id from the cancel index. handle must be a
    // live node previously returned by insert on this book.
    void remove(OrderNode *handle);

    // Cancel by id: O(1) index lookup + unlink. Returns false if the id is not
    // resting (unknown, already cancelled, or already removed).
    bool cancel(OrderId id);

    // Fills up to max_qty against the front order of side's best level.
    // Precondition: that best level is non-empty (check bestBid()/bestAsk()
    // first) -- the matching engine only calls this while there is a level
    // to cross against. A fully filled front order is unlinked and dropped
    // from the cancel index same as remove(); a partial fill stays at the
    // head of its level with its quantity reduced.
    ConsumeResult consumeBestFront(Side side, Quantity max_qty);

    std::optional<Ticks> bestBid() const;
    std::optional<Ticks> bestAsk() const;

    // Level reads are total: out-of-band or empty levels report zero/empty.
    Quantity levelQuantity(Side side, Ticks price) const;
    std::size_t levelOrderCount(Side side, Ticks price) const;
    const OrderNode *levelFront(Side side, Ticks price) const; // FIFO head; walk ->next

    std::size_t size() const { return resting_count; }
    std::size_t capacity() const { return node_storage.size(); }

  private:
    struct Level {
        OrderNode *head = nullptr;
        OrderNode *tail = nullptr;
        uint64_t total_quantity = 0;
        std::size_t order_count = 0;
    };

    // Open-addressing slot for the id -> node cancel index. node == nullptr
    // marks an empty slot (order ids have no reserved sentinel value).
    struct HandleSlot {
        uint64_t order_id = 0;
        OrderNode *node = nullptr;
    };

    static constexpr std::size_t NO_LEVEL = static_cast<std::size_t>(-1);
    static constexpr std::size_t NOT_FOUND = static_cast<std::size_t>(-1);

    bool inBand(int64_t ticks) const { return ticks >= min_tick && ticks <= max_tick; }
    std::size_t indexOf(int64_t ticks) const { return static_cast<std::size_t>(ticks - min_tick); }
    std::vector<Level> &ladder(Side side) { return side == Side::Buy ? bids : asks; }
    const std::vector<Level> &ladder(Side side) const { return side == Side::Buy ? bids : asks; }
    const Level *levelAt(Side side, Ticks price) const;
    OrderNode *allocateNode();
    void releaseNode(OrderNode *node);
    void refreshBestAfterEmpty(Side side, std::size_t emptied_index);
    void unlinkAndRelease(OrderNode *node);
    std::size_t handleHome(uint64_t order_id) const;
    std::size_t handleFind(uint64_t order_id) const;
    void handleInsert(uint64_t order_id, OrderNode *node);
    void handleErase(std::size_t index);

    int64_t min_tick;
    int64_t max_tick;
    std::vector<Level> bids; // index 0 == min_tick
    std::vector<Level> asks;
    std::size_t best_bid_index = NO_LEVEL; // highest occupied bid level
    std::size_t best_ask_index = NO_LEVEL; // lowest occupied ask level
    std::vector<OrderNode> node_storage;   // sized once in ctor, never resized (handle stability)
    OrderNode *free_head = nullptr;
    std::size_t resting_count = 0;
    // Cancel index: linear-probe open addressing, power-of-2 table sized to at
    // least twice the pool capacity in the ctor, so load stays <= 50% and no
    // rehash can ever happen on the hot path.
    std::vector<HandleSlot> handle_map;
    std::size_t handle_mask = 0;
    unsigned handle_shift = 0;
};

#endif // ORDER_MATCHING_ENGINE_ORDER_BOOK_HPP
