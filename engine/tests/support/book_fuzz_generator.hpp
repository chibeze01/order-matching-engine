#ifndef ORDER_MATCHING_ENGINE_BOOK_FUZZ_GENERATOR_HPP
#define ORDER_MATCHING_ENGINE_BOOK_FUZZ_GENERATOR_HPP

#include "ome/rng.hpp"
#include "ome/types/side.hpp"

#include <cstdint>

enum class FuzzOpKind : uint8_t { Add, Cancel };

struct FuzzOp {
    FuzzOpKind kind = FuzzOpKind::Add;
    uint64_t order_id = 0;
    Side side = Side::Buy;   // Add only
    int64_t price_ticks = 0; // Add only
    uint64_t quantity = 0;   // Add only
};

// Tunables for the random op generator. bid_max must be < ask_min so the
// book can never structurally cross -- matching isn't implemented yet
// (that's M2), so "best bid < best ask" has to hold by construction here
// rather than by any enforcement inside OrderBook itself.
struct FuzzGenConfig {
    int64_t bid_min;
    int64_t bid_max;
    int64_t ask_min;
    int64_t ask_max;
    uint64_t id_space; // ids drawn mod this; kept small relative to the
                       // op count so duplicate-id-on-add and
                       // unknown/already-cancelled-id-on-cancel both
                       // fire regularly
    uint64_t max_quantity;
    uint64_t add_weight_percent; // 0..100 chance an op is Add rather than Cancel
};

// Draws via rng.next() % bound only, never std::uniform_int_distribution --
// a distribution's internal algorithm isn't guaranteed identical across
// standard library implementations for the same seed, which would silently
// break reproducing a failure by seed across machines (see ome/rng.hpp).
FuzzOp nextFuzzOp(Rng &rng, const FuzzGenConfig &config);

#endif // ORDER_MATCHING_ENGINE_BOOK_FUZZ_GENERATOR_HPP
