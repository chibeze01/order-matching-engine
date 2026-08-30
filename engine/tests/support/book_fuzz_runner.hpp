#ifndef ORDER_MATCHING_ENGINE_BOOK_FUZZ_RUNNER_HPP
#define ORDER_MATCHING_ENGINE_BOOK_FUZZ_RUNNER_HPP

#include "book_fuzz_generator.hpp"
#include "book_invariants.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

// One end-to-end fuzz run's configuration: book shape (band/capacity), op
// generator tunables, and how often to pay for a full-book sweep on top of
// the per-op touched-level checks (see book_invariants.hpp).
struct FuzzRunConfig {
    uint64_t seed;
    uint64_t op_count;
    std::size_t capacity;
    int64_t min_tick;
    int64_t max_tick;
    FuzzGenConfig gen;
    uint64_t full_sweep_interval; // 0 = only at the end of the run
};

// House defaults: 5000 bid ticks, 5000 ask ticks strictly above them
// (non-crossing by construction), capacity 4096, id space 2x capacity,
// quantity 1..1000, 60% Add / 40% Cancel, full sweep every 10000 ops.
// Callers can tune any field afterwards -- the property test uses a
// tighter sweep interval since its runs are much shorter.
FuzzRunConfig defaultFuzzConfig(uint64_t seed, uint64_t op_count);

struct FuzzRunResult {
    bool ok;
    uint64_t ops_executed;
    std::optional<InvariantFailure> failure;
};

// Builds one OrderBook + one NaiveOrderBook from config, replays op_count
// random ops against both, checking invariants after every op (and doing a
// full sweep periodically, see full_sweep_interval). Stops at the first
// mismatch.
FuzzRunResult runFuzz(const FuzzRunConfig &config);

#endif // ORDER_MATCHING_ENGINE_BOOK_FUZZ_RUNNER_HPP
