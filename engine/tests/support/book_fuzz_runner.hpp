#ifndef ORDER_MATCHING_ENGINE_BOOK_FUZZ_RUNNER_HPP
#define ORDER_MATCHING_ENGINE_BOOK_FUZZ_RUNNER_HPP

#include "book_invariants.hpp"

#include <cstdint>
#include <optional>

// One end-to-end fuzz run's configuration. Book shape, op-generator
// tunables, and which of OrderBook's two removal APIs get exercised are all
// fixed house constants inside book_fuzz_runner.cpp -- nothing in this diff
// varies them, so they aren't exposed as knobs here. full_sweep_interval is
// the one field callers actually override (the property test uses a
// tighter interval than the default, since its runs are much shorter).
struct FuzzRunConfig {
    uint64_t seed;
    uint64_t op_count;
    uint64_t full_sweep_interval; // 0 = only at the end of the run
};

// House defaults: 5000 bid ticks, 5000 ask ticks strictly above them
// (non-crossing by construction), capacity 4096, full sweep every 10000
// ops. See book_fuzz_runner.cpp for the rest of the fixed shape (id space,
// quantity range, Add/Cancel/out-of-band/direct-remove weights).
FuzzRunConfig defaultFuzzConfig(uint64_t seed, uint64_t op_count);

struct FuzzRunResult {
    bool ok;
    uint64_t ops_executed;
    std::optional<InvariantFailure> failure;
};

// Builds one OrderBook + one NaiveOrderBook, replays op_count random ops
// against both (exercising both OrderBook::cancel and OrderBook::remove on
// the Cancel side), checking invariants after every op and doing a full
// sweep periodically (see full_sweep_interval). Stops at the first
// mismatch.
FuzzRunResult runFuzz(const FuzzRunConfig &config);

#endif // ORDER_MATCHING_ENGINE_BOOK_FUZZ_RUNNER_HPP
