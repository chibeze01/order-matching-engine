#ifndef ORDER_MATCHING_ENGINE_MATCHING_ENGINE_HPP
#define ORDER_MATCHING_ENGINE_MATCHING_ENGINE_HPP

#include "ome/book/order_book.hpp"
#include "ome/events/event_writer.hpp"
#include "ome/matching/trade.hpp"
#include "ome/types/order.hpp"

#include <optional>
#include <vector>

// Result of submitting a limit order. remaining is the quantity left
// unmatched: 0 means fully filled. If remaining > 0 and resting is nullptr,
// the leftover was rejected trying to rest (out-of-band price or exhausted
// pool) rather than matched away.
struct SubmitResult {
    std::vector<Trade> trades;
    OrderNode *resting = nullptr;
    Quantity remaining{0};
};

// Matches incoming limit orders against an OrderBook at price-time priority.
// Wraps a book it does not own; single-writer, no internal synchronisation,
// same as OrderBook.
//
// If given an EventWriter, every change to the book is emitted to it as it
// happens (see docs/event-stream.md): OrderExecuted + TradeExecuted per
// fill, OrderAdded for a remainder that rests, OrderCancelled, and
// OrderReplaced. Rejected operations change nothing and emit nothing.
//
// Self-match is allowed in v1: a taker can cross against its own resting
// order with no special handling. TODO(SPA-7+): self-match prevention flag.
class MatchingEngine {
  public:
    explicit MatchingEngine(OrderBook &book_, EventWriter *events_ = nullptr) : book(book_), events(events_) {}

    // Walks the opposite side from best price, filling FIFO within each
    // level, until price no longer crosses or the order's quantity is
    // exhausted. Any remainder rests at the order's own limit price.
    SubmitResult submitLimit(const Order &order);

    // Removes a resting order. Returns false if the id is not resting.
    bool cancel(OrderId id);

    // Replaces resting order old_id with order, which loses time priority.
    // Rejected (nullopt, book untouched) if old_id is not resting, order is
    // on the other side, its price is outside the band, its quantity is
    // zero, or its id is already resting under a different order. A
    // replacement that does not cross rests directly (one OrderReplaced
    // event). One that crosses is treated as cancel + submitLimit --
    // OrderCancelled for the old order, then the usual fills and OrderAdded
    // -- since a replace that trades is not a pure book move a consumer
    // could apply in place.
    std::optional<SubmitResult> replace(OrderId old_id, const Order &order);

  private:
    static bool crosses(Side taker_side, Ticks taker_price, Ticks resting_price);
    bool crossesBook(Side taker_side, Ticks taker_price) const;

    OrderBook &book;
    EventWriter *events;
};

#endif // ORDER_MATCHING_ENGINE_MATCHING_ENGINE_HPP
