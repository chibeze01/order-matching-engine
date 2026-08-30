#include "book_fuzz_generator.hpp"

namespace {
uint64_t boundedRand(Rng &rng, const uint64_t bound) { return rng.next() % bound; }
} // namespace

FuzzOp nextFuzzOp(Rng &rng, const FuzzGenConfig &config) {
    FuzzOp op;
    op.kind = boundedRand(rng, 100) < config.add_weight_percent ? FuzzOpKind::Add : FuzzOpKind::Cancel;
    op.order_id = boundedRand(rng, config.id_space);

    if (op.kind == FuzzOpKind::Add) {
        op.side = boundedRand(rng, 2) == 0 ? Side::Buy : Side::Sell;
        const int64_t range_min = op.side == Side::Buy ? config.bid_min : config.ask_min;
        const int64_t range_max = op.side == Side::Buy ? config.bid_max : config.ask_max;
        const uint64_t width = static_cast<uint64_t>(range_max - range_min) + 1;
        op.price_ticks = range_min + static_cast<int64_t>(boundedRand(rng, width));
        op.quantity = 1 + boundedRand(rng, config.max_quantity);
    }

    return op;
}
