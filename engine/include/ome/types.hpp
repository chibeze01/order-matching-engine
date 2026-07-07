#ifndef OME_TYPES_HPP
#define OME_TYPES_HPP

// Core domain vocabulary for the matching engine (SPA-2).
//
// Prices are integer tick counts, never floating point. Decimal prices cross
// the boundary as fixed-point nanos (1e-9 units), so conversion is exact
// integer arithmetic end to end. See docs/design-decisions.md.

#include <compare>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace ome {

// One price unit (e.g. 1.00) expressed in nanos. A decimal price of 101.25 is
// 101'250'000'000 nanos.
inline constexpr std::int64_t kNanosPerUnit = 1'000'000'000;

// Strong typedef: a price as a non-negative count of ticks. Arithmetic on raw
// ticks is deliberate and explicit via .ticks.
struct Price {
    std::int64_t ticks;

    constexpr auto operator<=>(const Price &) const = default;
};

// Tick size in nanos, e.g. a 0.01 tick is {10'000'000}. Explicit at every
// conversion site so no hidden global tick size exists.
struct TickSize {
    std::int64_t nanos;
};

// Converts a fixed-point decimal price (in nanos) to ticks, rounding to the
// nearest tick with halves rounding up. Rejects negative prices and
// non-positive tick sizes.
constexpr std::optional<Price> price_from_nanos(std::int64_t price_nanos, TickSize tick) {
    if (price_nanos < 0 || tick.nanos <= 0) {
        return std::nullopt;
    }
    return Price{(price_nanos + tick.nanos / 2) / tick.nanos};
}

// Converts ticks back to a fixed-point decimal price in nanos.
constexpr std::int64_t price_to_nanos(Price price, TickSize tick) { return price.ticks * tick.nanos; }

using Quantity = std::uint64_t;

// Strong typedef: engine-assigned order identifier. Values are allocated
// monotonically by whoever feeds the engine (the sequencer owns the counter);
// the type itself just guarantees 64 bits and no accidental mixing with other
// integers.
struct OrderId {
    std::uint64_t value;

    constexpr auto operator<=>(const OrderId &) const = default;
};

enum class Side : std::uint8_t { Buy, Sell };

enum class OrderType : std::uint8_t { Limit, Market, IOC, FOK };

// A resting or incoming order. POD-ish and fixed size so nodes pack cleanly
// into the pool (SPA-4) and copy with memcpy semantics.
struct Order {
    OrderId id;
    Price price; // ignored for Market orders
    Quantity quantity;
    Side side;
    OrderType type;
};

static_assert(sizeof(Order) == 32, "Order must stay fixed-size and cache-friendly");
static_assert(std::is_trivially_copyable_v<Order>);

} // namespace ome

#endif // OME_TYPES_HPP
