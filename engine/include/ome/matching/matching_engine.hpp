#ifndef ORDER_MATCHING_ENGINE_MATCHING_ENGINE_HPP
#define ORDER_MATCHING_ENGINE_MATCHING_ENGINE_HPP

#include "ome/book/order_book.hpp"
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
// Self-match is allowed in v1: a taker can cross against its own resting
// order with no special handling. TODO(SPA-7+): self-match prevention flag.
class MatchingEngine {
  public:
    explicit MatchingEngine(OrderBook &book_) : book(book_) {}

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

  private:
    static bool crosses(Side taker_side, Ticks taker_price, Ticks resting_price);
    static Side restingSide(Side taker_side);

    // Shared fill loop: consumes the far side from best, FIFO within each
    // level, until order.quantity is exhausted, the side is empty, or (when
    // limit is set) the best price no longer crosses limit. Appends one
    // trade per fill and returns the unmatched quantity. Never rests.
    uint64_t match(const Order &order, std::optional<Ticks> limit, std::vector<Trade> &trades);

    OrderBook &book;
};

#endif // ORDER_MATCHING_ENGINE_MATCHING_ENGINE_HPP
