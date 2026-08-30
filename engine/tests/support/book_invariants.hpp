#ifndef ORDER_MATCHING_ENGINE_BOOK_INVARIANTS_HPP
#define ORDER_MATCHING_ENGINE_BOOK_INVARIANTS_HPP

#include "book_fuzz_generator.hpp"
#include "naive_order_book.hpp"
#include "ome/book/order_book.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

struct InvariantFailure {
    std::string what;
    uint64_t op_index;
    uint64_t seed;
};

// Formats a failure for a human to read: enough to reproduce (seed, op
// index) and to see what mismatched.
std::string describe(const InvariantFailure &failure);

// Checked after every op -- cheap, touched-level only. touched_level is the
// level to recheck FIFO order and aggregates at: for an accepted Add it's
// (op.side, op.price_ticks); for a Cancel it's the id's pre-op location
// (nullopt if the id wasn't live, i.e. the cancel was expected to fail and
// there's nothing to recheck beyond the result/size/best-quote checks
// already covered here).
std::optional<InvariantFailure> checkAfterOp(const OrderBook &book, const NaiveOrderBook &naive, const FuzzOp &op,
                                             bool real_result, bool naive_result,
                                             std::optional<std::pair<Side, Ticks>> touched_level, uint64_t op_index,
                                             uint64_t seed);

// Walks every occupied level on both sides and rechecks FIFO order and
// aggregates -- O(resting orders). Meant to run periodically, not every op,
// as defense-in-depth against a level nobody touched this step being
// corrupted -- a bug checkAfterOp's touched-level-only checks wouldn't catch
// until something later happened to land on that same level.
std::optional<InvariantFailure> checkFullSweep(const OrderBook &book, const NaiveOrderBook &naive, uint64_t op_index,
                                               uint64_t seed);

#endif // ORDER_MATCHING_ENGINE_BOOK_INVARIANTS_HPP
