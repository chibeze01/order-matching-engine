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

TODO(SPA-2+): document the tick-size convention and the price/tick conversion.

## Price-time priority

Orders match by best price first, then by arrival time within a price level.
This is the standard continuous-auction rule real venues use, so simulated flow
behaves the way microstructure results expect.

The book keeps one contiguous ladder of price levels per side, indexed by tick
offset within a fixed inclusive band set at construction — array indexing beats
tree lookups for the hot levels. Each level is an intrusive doubly-linked FIFO
queue whose nodes come from a pool preallocated at construction, so the hot
path never touches the heap. Best bid and best ask are cached level indices
kept current incrementally, with a directional scan only when the best level
empties. Inserts outside the band or beyond pool capacity are rejected with a
null handle rather than an exception; the matching engine turns that into a
reject on its own terms.

## O(1) cancels

Cancels are O(1). Real order flow is cancel-dominated, so cancel cost sits on the
hot path far more than fills do. Every resting order carries a handle straight to
its node in its price level, so removal is a constant-time unlink with no search.

TODO(SPA-4+): document the handle map and the intrusive list layout.
