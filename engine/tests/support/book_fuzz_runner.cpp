#include "book_fuzz_runner.hpp"

#include "ome/rng.hpp"
#include "ome/types/order.hpp"
#include "ome/types/order_id.hpp"
#include "ome/types/price.hpp"
#include "ome/types/quantity.hpp"
#include "ome/types/tick_size.hpp"

namespace {

constexpr uint8_t FUZZ_TICK_SIZE = 0; // arbitrary: the fuzzer only ever deals in ticks, never decimal prices

Order makeOrder(const FuzzOp &op) {
    return Order{OrderId(op.order_id), Price(Ticks(op.price_ticks), TickSize(FUZZ_TICK_SIZE)), Quantity(op.quantity),
                 op.side, OrderType::Limit};
}

} // namespace

FuzzRunConfig defaultFuzzConfig(const uint64_t seed, const uint64_t op_count) {
    FuzzRunConfig config;
    config.seed = seed;
    config.op_count = op_count;
    config.capacity = 4096;
    config.min_tick = 0;
    config.max_tick = 9999;
    config.gen = FuzzGenConfig{
        .bid_min = 0,
        .bid_max = 4999,
        .ask_min = 5000,
        .ask_max = 9999,
        .id_space = 8192,
        .max_quantity = 1000,
        .add_weight_percent = 60,
    };
    config.full_sweep_interval = 10000;
    return config;
}

FuzzRunResult runFuzz(const FuzzRunConfig &config) {
    OrderBook book(Ticks(config.min_tick), Ticks(config.max_tick), config.capacity);
    NaiveOrderBook naive(Ticks(config.min_tick), Ticks(config.max_tick), config.capacity);
    Rng rng(config.seed);

    for (uint64_t op_index = 0; op_index < config.op_count; ++op_index) {
        const FuzzOp op = nextFuzzOp(rng, config.gen);

        bool real_result = false;
        bool naive_result = false;
        // Ticks has a const member (no assignment operator), which makes
        // optional<pair<Side, Ticks>> non-assignable too -- built with
        // emplace() (construction) below rather than operator=.
        std::optional<std::pair<Side, Ticks>> touched_level = std::nullopt;

        if (op.kind == FuzzOpKind::Add) {
            const Order order = makeOrder(op);
            real_result = book.insert(order) != nullptr;
            naive_result = naive.insert(order);
            if (real_result) {
                touched_level.emplace(op.side, Ticks(op.price_ticks));
            }
        } else {
            // pre-op: OrderBook has no equivalent "where is this id resting" query
            const std::optional<std::pair<Side, Ticks>> location = naive.locate(OrderId(op.order_id));
            if (location.has_value()) {
                touched_level.emplace(location->first, location->second);
            }
            real_result = book.cancel(OrderId(op.order_id));
            naive_result = naive.cancel(OrderId(op.order_id));
        }

        if (std::optional<InvariantFailure> failure =
                checkAfterOp(book, naive, op, real_result, naive_result, touched_level, op_index, config.seed)) {
            return FuzzRunResult{false, op_index + 1, failure};
        }

        const bool due_for_sweep = config.full_sweep_interval != 0 && (op_index + 1) % config.full_sweep_interval == 0;
        if (due_for_sweep) {
            if (std::optional<InvariantFailure> failure = checkFullSweep(book, naive, op_index, config.seed)) {
                return FuzzRunResult{false, op_index + 1, failure};
            }
        }
    }

    if (std::optional<InvariantFailure> failure = checkFullSweep(book, naive, config.op_count, config.seed)) {
        return FuzzRunResult{false, config.op_count, failure};
    }

    return FuzzRunResult{true, config.op_count, std::nullopt};
}
