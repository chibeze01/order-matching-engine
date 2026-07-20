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

The cancel index is a hash map from order id to node pointer: open addressing
with linear probing into a power-of-two table sized at construction to at least
twice the pool capacity, so load never exceeds 50% and no rehash can happen on
the hot path. Deletion uses backward shift rather than tombstones, so probe
chains stay short no matter how long the insert/cancel churn runs. The nodes
themselves are intrusive: prev/next links live inside the pooled order node, so
unlinking touches no other structure and frees no memory — the node just goes
back on the pool's free list.
