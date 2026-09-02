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

## Deterministic input log and replay

Same input log + same seed must reproduce identical book state, every time —
that's the debugging superpower a matching engine needs before matching gets
complicated. Two decisions make it work:

**Binary format, not the engine's C++ types.** Every command (add/cancel/modify)
is a fixed-width, 36-byte little-endian record behind a 48-byte header
(magic + version + book config + seed). Fields are packed byte-by-byte rather
than memcpy'd from a C struct, so there's no struct-padding or endianness
ambiguity for a second reader to reverse-engineer — the layout in
`engine/include/ome/replay/log_format.hpp` is the whole contract. That's what
lets the Python analysis tooling parse the same log without linking the
engine.

**The book-state hash is a state fingerprint, not an input checksum.** Replay
folds an FNV-1a hash over each command's outcome — success/failure, the
touched price level's quantity and order count, and the book-wide best bid,
best ask and resting count — not just the raw command bytes. The book-wide
part is what makes it a fingerprint rather than a per-level checksum: without
it, damage to a level nobody touched this step, or a broken best-bid/best-ask
index, would hide until some later command happened to land on it. The fold is
O(1) per command (level and best-quote lookups are already O(1)), so hashing
costs nothing extra at 10M+ events.

**Rotation is file-count, not a size cap.** `LogWriter` closes and reopens a
new part (`base_path`, `base_path.1`, `base_path.2`, ...) every
`max_records_per_file` records, each part repeating the header so it's
independently parseable. `LogReader` streams one record at a time and follows
parts transparently — neither side ever holds the whole log in memory.

**Corruption is reported, never skipped.** A log cut short mid-record is a
different thing from one that ended cleanly, so `readCommand` distinguishes
them by byte count and `LogReader` throws rather than stopping quietly — a
determinism harness that accepts a truncated log would hand back a confident
hash for an incomplete run, which is worse than no hash at all. The same
applies to sequence gaps and to part files whose headers disagree. On the
write side `LogWriter` checks the stream after every record, so a disk that
fills up part-way through a long run fails loudly instead of producing a
silently short log. Records are still buffered rather than flushed per write —
per-record flushing would dominate the cost at 10M+ events, and the reader's
truncation check is what catches an interrupted writer.

**A modify that cannot be re-inserted is rejected, not dropped.** Replay
applies a modify as cancel-then-insert, so it validates the target price
against the band *before* cancelling. Once the order is out of the book the
re-insert can only fail on an out-of-band price — the id has just been freed
from the cancel index and the node returned to the pool — so the up-front
check is sufficient to guarantee the re-insert succeeds. Cancelling first and
discovering the failure afterwards would leave the order resting at neither
the old price nor the new one.

## Book invariant suite and fuzz tests

M2's matching logic needs a book everyone trusts underneath it, so before
that lands the book gets checked against a second, independent
implementation rather than just its own unit tests. `NaiveOrderBook`
(`engine/tests/support/naive_order_book.hpp`) rebuilds the same resting-book
semantics from `std::map`/`std::deque`/`std::unordered_map` — no pooling, no
open addressing — so it is obviously correct by inspection and gives
`OrderBook`'s hot-path tricks something to disagree with if they're wrong.
A seeded generator drives random add/cancel ops through both books; after
every op, success/failure, resting count, best bid/ask, and the touched
level's FIFO order and aggregates are asserted equal between the two —
"cancel index consistent with book contents" cashes out to bool-for-bool
agreement between `OrderBook::cancel` and the naive book's plain
hash-lookup-and-erase. A minority of ops deliberately poke past the band
edge or cancel via `OrderBook::remove(handle)` instead of `cancel(id)`, so
the suite doesn't just fuzz the paths that are easy to reach by accident —
out-of-band rejection and the pooled free-list's direct-handle removal path
each need their own dedicated slice of ops to ever run at all.

A full-book sweep runs periodically on top of the per-op checks, as
defense-in-depth against a level nobody touched this step getting
corrupted. It walks every tick in the book's band, not just the levels the
naive reference currently has orders at — sweeping only naive-occupied
levels would never notice a real-book-only bug that plants stray state at a
price the naive side has nothing resting at.

**Non-crossing by construction, not by enforcement.** `OrderBook` at this
stage never crosses an order against the opposite side — that's M2 — so
nothing stops a naive fuzzer from resting a buy above a resting sell. The
generator instead draws bid prices and ask prices from two disjoint
sub-ranges of the band, bids strictly below asks, so "best bid < best ask
when both exist" holds by construction. This keeps the suite scoped to what
the book actually implements today (FIFO order, level aggregates,
cancel-index consistency) instead of matching semantics that don't exist
yet. It also means narrowing the gap between the two sub-ranges once M2
adds real crossing is not a config tweak: `NaiveOrderBook` has no
match/execution logic and the invariant checks have no concept of a trade,
so both need matching-aware rework before the suite can meaningfully cover
a book that's allowed to cross.

**Two tiers, one shared driver.** A GoogleTest property test runs 32 fixed
seeds at a few thousand ops each on every push, cheap enough to run under
both `debug-asan` and `release`. A standalone CLI (`ome_book_fuzz`) runs the
same driver at 1M+ ops nightly, seeded from `std::random_device` by default
so it explores fresh ground each run, always printing the seed first —
`mt19937_64`'s seed-deterministic, prefix-stable stream means rerunning with
`--seed` reproduces the exact failing sequence, so a failure doesn't need
its own persisted log the way a production replay would.
