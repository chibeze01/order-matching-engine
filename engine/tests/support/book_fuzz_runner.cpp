#include "book_fuzz_runner.hpp"

#include "book_fuzz_generator.hpp"
#include "ome/rng.hpp"
#include "ome/types/order.hpp"
#include "ome/types/order_id.hpp"
#include "ome/types/price.hpp"
#include "ome/types/quantity.hpp"
#include "ome/types/tick_size.hpp"

#include <unordered_map>

namespace {

constexpr uint8_t FUZZ_TICK_SIZE = 0; // arbitrary: the fuzzer only ever deals in ticks, never decimal prices

constexpr int64_t BID_MIN = 0;
constexpr int64_t BID_MAX = 4999;
constexpr int64_t ASK_MIN = 5000;
constexpr int64_t ASK_MAX = 9999;
constexpr std::size_t CAPACITY = 4096;
constexpr uint64_t ID_SPACE = 8192; // 2x capacity: keeps duplicate-add and unknown-cancel paths hot
constexpr uint64_t MAX_QUANTITY = 1000;
constexpr uint64_t ADD_WEIGHT_PERCENT = 60;           // oscillates book size around capacity
constexpr uint64_t OUT_OF_BAND_WEIGHT_PERCENT = 5;    // Add ops that deliberately target a price outside the band
constexpr uint64_t DIRECT_REMOVE_WEIGHT_PERCENT = 30; // Cancel ops exercised via OrderBook::remove(handle)
constexpr uint64_t DEFAULT_FULL_SWEEP_INTERVAL = 10000;

FuzzGenConfig genConfig() {
    return FuzzGenConfig{
        .bid_min = BID_MIN,
        .bid_max = BID_MAX,
        .ask_min = ASK_MIN,
        .ask_max = ASK_MAX,
        .band_max = ASK_MAX,
        .id_space = ID_SPACE,
        .max_quantity = MAX_QUANTITY,
        .add_weight_percent = ADD_WEIGHT_PERCENT,
        .out_of_band_weight_percent = OUT_OF_BAND_WEIGHT_PERCENT,
    };
}

Order makeOrder(const FuzzOp &op) {
    return Order{OrderId(op.order_id), Price(Ticks(op.price_ticks), TickSize(FUZZ_TICK_SIZE)), Quantity(op.quantity),
                 op.side, OrderType::Limit};
}

} // namespace

FuzzRunConfig defaultFuzzConfig(const uint64_t seed, const uint64_t op_count) {
    return FuzzRunConfig{seed, op_count, DEFAULT_FULL_SWEEP_INTERVAL};
}

FuzzRunResult runFuzz(const FuzzRunConfig &config) {
    const FuzzGenConfig gen = genConfig();
    OrderBook book(Ticks(BID_MIN), Ticks(ASK_MAX), CAPACITY);
    NaiveOrderBook naive(Ticks(BID_MIN), Ticks(ASK_MAX), CAPACITY);
    Rng rng(config.seed);
    // Mirrors which ids are currently live and their OrderBook handle, so
    // Cancel ops can sometimes exercise OrderBook::remove(handle) directly
    // instead of always going through OrderBook::cancel(id) -- cancel()
    // alone never reaches the pooled free-list / cancel-index interaction
    // that direct-handle removal does.
    std::unordered_map<uint64_t, OrderNode *> live_handles;

    for (uint64_t op_index = 0; op_index < config.op_count; ++op_index) {
        const FuzzOp op = nextFuzzOp(rng, gen);

        bool real_result = false;
        bool naive_result = false;
        // Ticks has a const member (no assignment operator), which makes
        // optional<pair<Side, Ticks>> non-assignable too -- built with
        // emplace() (construction) below rather than operator=.
        std::optional<std::pair<Side, Ticks>> touched_level = std::nullopt;

        if (op.kind == FuzzOpKind::Add) {
            const Order order = makeOrder(op);
            OrderNode *handle = book.insert(order);
            real_result = handle != nullptr;
            naive_result = naive.insert(order);
            if (real_result) {
                live_handles[op.order_id] = handle;
                touched_level.emplace(op.side, Ticks(op.price_ticks));
            }
        } else {
            // pre-op: OrderBook has no equivalent "where is this id resting" query
            const std::optional<std::pair<Side, Ticks>> location = naive.locate(OrderId(op.order_id));
            if (location.has_value()) {
                touched_level.emplace(location->first, location->second);
            }

            const auto handle_it = live_handles.find(op.order_id);
            const bool use_direct_remove =
                handle_it != live_handles.end() && boundedRand(rng, 100) < DIRECT_REMOVE_WEIGHT_PERCENT;
            if (use_direct_remove) {
                book.remove(handle_it->second);
                real_result = true;
                live_handles.erase(handle_it);
            } else {
                real_result = book.cancel(OrderId(op.order_id));
                if (real_result && handle_it != live_handles.end()) {
                    live_handles.erase(handle_it);
                }
            }
            naive_result = naive.cancel(OrderId(op.order_id));
        }

        if (std::optional<InvariantFailure> failure =
                checkAfterOp(book, naive, op, real_result, naive_result, touched_level, op_index, config.seed)) {
            return FuzzRunResult{false, op_index + 1, failure};
        }

        const bool due_for_sweep = config.full_sweep_interval != 0 && (op_index + 1) % config.full_sweep_interval == 0;
        if (due_for_sweep) {
            if (std::optional<InvariantFailure> failure =
                    checkFullSweep(book, naive, BID_MIN, ASK_MAX, op_index, config.seed)) {
                return FuzzRunResult{false, op_index + 1, failure};
            }
        }
    }

    if (std::optional<InvariantFailure> failure =
            checkFullSweep(book, naive, BID_MIN, ASK_MAX, config.op_count, config.seed)) {
        return FuzzRunResult{false, config.op_count, failure};
    }

    return FuzzRunResult{true, config.op_count, std::nullopt};
}
