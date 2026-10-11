#ifndef ORDER_MATCHING_ENGINE_NAIVE_MATCHER_HPP
#define ORDER_MATCHING_ENGINE_NAIVE_MATCHER_HPP

#include "ome/matching/trade.hpp"
#include "ome/types/order.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <vector>

// Deliberately naive reference matcher (SPA-7): plain std::map ladders
// walked by brute force on every submit, no pooling and no cancel index, so
// it is obviously correct by inspection and gives MatchingEngine's
// OrderBook-backed walk something independent to be checked against in the
// differential test. Mirrors MatchingEngine::submitLimit's semantics
// exactly: same trade fields, same maker's-price rule, same FIFO-within-level
// order, remainder rests at the order's own limit price.
class NaiveMatcher {
  public:
    struct Resting {
        uint64_t order_id;
        uint64_t quantity;
    };

    std::vector<Trade> submitLimit(const Order &order);

    const std::map<int64_t, std::deque<Resting>, std::greater<>> &bids() const { return bids_; }
    const std::map<int64_t, std::deque<Resting>> &asks() const { return asks_; }

  private:
    std::map<int64_t, std::deque<Resting>, std::greater<>> bids_; // best = begin()
    std::map<int64_t, std::deque<Resting>> asks_;                 // best = begin()
};

#endif // ORDER_MATCHING_ENGINE_NAIVE_MATCHER_HPP
