#include "book_fuzz_generator.hpp"

uint64_t boundedRand(Rng &rng, const uint64_t bound) { return bound == 0 ? 0 : rng.next() % bound; }

namespace {

// Draws a price 1..1000 ticks past band_max to exercise OrderBook's
// out-of-band rejection path, which no in-band draw could ever reach.
// Only pokes above the band, not below: Price itself rejects negative
// ticks (a separate, earlier check than OrderBook's band check), and
// band_min is 0 in the shipped config, so "below the band" and "negative"
// are the same thing here -- there's no room to go below band_min while
// staying non-negative.
int64_t outOfBandPrice(Rng &rng, const FuzzGenConfig &config) {
    const int64_t offset = 1 + static_cast<int64_t>(boundedRand(rng, 1000));
    return config.band_max + offset;
}

// Draws a price from the given side's sub-range. Falls back to range_min if
// the range is misconfigured (range_max < range_min) rather than wrapping a
// signed-to-unsigned width computation into undefined behavior.
int64_t inBandPrice(Rng &rng, const int64_t range_min, const int64_t range_max) {
    const int64_t span = range_max - range_min;
    const uint64_t width = span >= 0 ? static_cast<uint64_t>(span) + 1 : 1;
    return range_min + static_cast<int64_t>(boundedRand(rng, width));
}

} // namespace

FuzzOp nextFuzzOp(Rng &rng, const FuzzGenConfig &config) {
    FuzzOp op;
    op.kind = boundedRand(rng, 100) < config.add_weight_percent ? FuzzOpKind::Add : FuzzOpKind::Cancel;
    op.order_id = boundedRand(rng, config.id_space);

    if (op.kind == FuzzOpKind::Add) {
        op.side = boundedRand(rng, 2) == 0 ? Side::Buy : Side::Sell;

        if (boundedRand(rng, 100) < config.out_of_band_weight_percent) {
            op.price_ticks = outOfBandPrice(rng, config);
        } else {
            const int64_t range_min = op.side == Side::Buy ? config.bid_min : config.ask_min;
            const int64_t range_max = op.side == Side::Buy ? config.bid_max : config.ask_max;
            op.price_ticks = inBandPrice(rng, range_min, range_max);
        }

        op.quantity = 1 + boundedRand(rng, config.max_quantity);
    }

    return op;
}
