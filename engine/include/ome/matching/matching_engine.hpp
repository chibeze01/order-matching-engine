#ifndef ORDER_MATCHING_ENGINE_MATCHING_ENGINE_HPP
#define ORDER_MATCHING_ENGINE_MATCHING_ENGINE_HPP

#include "ome/book/order_book.hpp"
#include "ome/events/event_writer.hpp"
#include "ome/matching/trade.hpp"
#include "ome/types/order.hpp"

#include <cstdint>
#include <optional>
#include <vector>

// What happened to an order's unmatched quantity. Every submit ends in
// exactly one status, reported after its trades: fills first, then the
// final disposition.
//   Filled       -- remaining == 0, nothing left over.
//   Rested       -- limit order's remainder now rests; resting is its handle.
//   Cancelled    -- Market/IOC remainder, or a whole FOK that was killed.
//                   Never rests, by design of the order type.
//   RestRejected -- limit order's remainder could not rest (out-of-band
//                   price or exhausted pool): a book limit, not a choice.
enum class SubmitStatus : uint8_t { Filled, Rested, Cancelled, RestRejected };

// Result of submitting an order. remaining is the quantity left unmatched;
// status says what became of it. resting is non-null only when status is
// Rested.
struct SubmitResult {
    std::vector<Trade> trades;
    OrderNode *resting = nullptr;
    Quantity remaining{0};
    SubmitStatus status = SubmitStatus::Filled;
};

// Matches incoming orders against an OrderBook at price-time priority.
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

    // Dispatches on order.type. Limit behaves as submitLimit. Market ignores
    // order.price and walks the far side until filled or empty. IOC matches
    // what crosses at its limit. FOK fills entirely or not at all, decided
    // by a read-only pre-check so a kill leaves the book untouched. Market,
    // IOC and FOK never rest: any remainder is cancelled.
    SubmitResult submit(const Order &order);

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
    static Side restingSide(Side taker_side);
    bool crossesBook(Side taker_side, Ticks taker_price) const;

    // Shared fill loop: consumes the far side from best, FIFO within each
    // level, until order.quantity is exhausted, the side is empty, or (when
    // limit is set) the best price no longer crosses limit. Appends one
    // trade per fill and returns the unmatched quantity. Never rests.
    uint64_t match(const Order &order, std::optional<Ticks> limit, std::vector<Trade> &trades);

    OrderBook &book;
    EventWriter *events;
};

#endif // ORDER_MATCHING_ENGINE_MATCHING_ENGINE_HPP
