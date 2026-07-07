# Design decisions

The load-bearing choices behind the matching engine, with the reasoning that
justifies each one. Kept short here and expanded as the engine takes shape.

## Status

Stub. Headings are in place from SPA-1; the rationale under each fills in as the
corresponding code lands.

## Integer-tick prices

Prices are integer tick counts, never floating point. Floating point makes
equality and price-time ordering fragile and invites rounding drift across the
engine, gateway, and analysis. An integer tick is exact, cheap to compare, and
maps cleanly onto the wire format.

Convention (from SPA-2, `engine/include/ome/types.hpp`):

- `Price` is a strong typedef holding a non-negative `int64_t` tick count.
- Decimal prices cross the boundary as fixed-point **nanos** (1e-9 price
  units), so 101.25 is `101'250'000'000`. Conversion is pure integer
  arithmetic; no floating point exists anywhere in the core.
- `TickSize` is explicit at every conversion site (a 0.01 tick is
  `{10'000'000}` nanos). No hidden global tick size.
- `price_from_nanos` rounds to the nearest tick (halves up) and rejects
  negative prices and non-positive tick sizes by returning `nullopt`.
  `price_to_nanos` is exact.

Why not floats: `0.1 + 0.2 != 0.3` style comparison bugs corrupt price-time
ordering, and rounding drift across engine, gateway, and analysis breaks
determinism — replaying the same input log must produce bit-identical books.

## Price-time priority

Orders match by best price first, then by arrival time within a price level.
This is the standard continuous-auction rule real venues use, so simulated flow
behaves the way microstructure results expect.

TODO(SPA-3+): document the level and queue data structures.

## O(1) cancels

Cancels are O(1). Real order flow is cancel-dominated, so cancel cost sits on the
hot path far more than fills do. Every resting order carries a handle straight to
its node in its price level, so removal is a constant-time unlink with no search.

TODO(SPA-4+): document the handle map and the intrusive list layout.
