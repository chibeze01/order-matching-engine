#ifndef ORDER_MATCHING_ENGINE_REPLAY_ENGINE_HPP
#define ORDER_MATCHING_ENGINE_REPLAY_ENGINE_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// Result of replaying one input log to completion. The final book summary is
// carried alongside the hash so callers can see the state the hash stands
// for, rather than only an opaque fingerprint.
struct ReplayResult {
    uint64_t command_count = 0;
    uint64_t final_hash = 0;
    std::size_t resting_count = 0;
    std::optional<int64_t> best_bid_ticks;
    std::optional<int64_t> best_ask_ticks;
};

// Result of replaying two input logs in lockstep and comparing state after
// every command.
struct CompareResult {
    bool diverged = false;
    uint64_t first_diverging_seq = 0; // valid only if diverged
    uint64_t final_hash_a = 0;
    uint64_t final_hash_b = 0;
};

// Replays base_path (following rotation) against a freshly constructed
// OrderBook using the log's own header config, folding a running state hash
// after every command. The hash mixes in each command's outcome (success,
// and the resulting price level's quantity/order count) so it fingerprints
// actual book state, not just the input bytes.
ReplayResult replayLog(const std::string &base_path);

// Replays two logs side by side against separate books, comparing the
// running hash after each command. Reports the first sequence number where
// they disagree; logs of different lengths diverge at whichever side has a
// next command the other lacks.
CompareResult compareLogs(const std::string &path_a, const std::string &path_b);

#endif // ORDER_MATCHING_ENGINE_REPLAY_ENGINE_HPP
