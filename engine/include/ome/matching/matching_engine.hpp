#ifndef ORDER_MATCHING_ENGINE_MATCHING_ENGINE_HPP
#define ORDER_MATCHING_ENGINE_MATCHING_ENGINE_HPP

#include "ome/book/order_book.hpp"
#include "ome/matching/trade.hpp"
#include "ome/types/order.hpp"

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
// Self-match is allowed in v1: a taker can cross against its own resting
// order with no special handling. TODO(SPA-7+): self-match prevention flag.
class MatchingEngine {
  public:
    explicit MatchingEngine(OrderBook &book_) : book(book_) {}

    // Walks the opposite side from best price, filling FIFO within each
    // level, until price no longer crosses or the order's quantity is
    // exhausted. Any remainder rests at the order's own limit price.
    SubmitResult submitLimit(const Order &order);

  private:
    static bool crosses(Side taker_side, Ticks taker_price, Ticks resting_price);

    OrderBook &book;
};

#endif // ORDER_MATCHING_ENGINE_MATCHING_ENGINE_HPP
