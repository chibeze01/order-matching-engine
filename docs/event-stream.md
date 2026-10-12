# Event stream

Event stream v1 wire format (SPA-10).

This document defines the ITCH-style binary event stream emitted by the matching
engine: message types, field layouts, and endianness. It is the contract shared
by the binary event log, the WebSocket gateway, the Python analysis tooling, and
the Next.js visualiser, so once it is frozen it does not change without a version
bump.

## Status

v1, pending Engineer B sign-off before freeze. After freeze, changes are
additive only (see [Versioning](#versioning)).

The reference implementation is `engine/include/ome/events/event_format.hpp`
(encode/decode) and `event_writer.hpp` (sequencing, buffer). The golden file
`engine/tests/golden/event_stream_v1.bin` is a 1120-byte stream containing
every message type; its op sequence is in `goldenStream()` in
`engine/tests/test_event_stream.cpp`. Use it to test any other reader.

## Conventions

- **Little-endian**, every integer. Fields are packed byte-by-byte; there is no
  implicit padding. All offsets below are from the start of the message.
- **Fixed width.** Every message type has a fixed size, except `BookSnapshot`,
  which is a fixed part followed by an array of fixed-size levels.
- **Prices are integer ticks** (`i64`). Decimal price = `ticks × tick size`,
  using the tick size from the stream header.
- **Quantities and order ids** are `u64`.
- **Side** is a `u8`: `0` = buy, `1` = sell.
- **Reserved bytes** are written as zero and must be ignored by readers.

## Stream header

A stream starts with one 32-byte header: at the start of a log file, or at the
start of a gateway connection before its snapshot.

| Offset | Size | Type   | Field       | Notes                                   |
| -----: | ---: | ------ | ----------- | --------------------------------------- |
|      0 |    8 | char[] | `magic`     | `"OMEEVT\0\0"`                          |
|      8 |    4 | u32    | `version`   | `1`                                     |
|     12 |    4 | u32    | `tick_size` | Same value the input log header carries |
|     16 |    8 | i64    | `min_tick`  | Book band, inclusive                    |
|     24 |    8 | i64    | `max_tick`  | Book band, inclusive                    |

Messages follow back to back.

## Message header

Every message starts with the same 24 bytes.

| Offset | Size | Type | Field       | Notes                                          |
| -----: | ---: | ---- | ----------- | ---------------------------------------------- |
|      0 |    4 | u32  | `length`    | Total message size in bytes, header included   |
|      4 |    1 | u8   | `type`      | ASCII type code, see below                     |
|      5 |    3 | —    | reserved    |                                                |
|      8 |    8 | u64  | `seq`       | Sequence number                                |
|     16 |    8 | u64  | `timestamp` | Caller-supplied time in ns; `0` if never set   |

**Sequence numbers.** Every delta message (everything except `BookSnapshot`)
takes the next number: the first is `1`, and each one after is exactly one
more than the last, with no gaps. A `BookSnapshot` takes no number of its own:
its `seq` is the seq of the last delta it already reflects (`0` if none). A
consumer that starts from a snapshot applies only deltas with
`seq > snapshot.seq`, and treats any gap as a lost message.

**Timestamps.** The engine never reads a clock; whoever drives it (simulator,
replay) sets the timestamp, so the same input always produces the same bytes.

**Length.** Readers must advance by `length`, not by the size they expect for
the type. That is what lets an old reader skip message types added later.

## Message types

| Code | Type             | Size        | Meaning                                      |
| ---- | ---------------- | ----------- | -------------------------------------------- |
| `A`  | `OrderAdded`     | 56          | An order is now resting                      |
| `E`  | `OrderExecuted`  | 80          | A resting order was hit (book view of a fill)|
| `X`  | `OrderCancelled` | 56          | A resting order left without trading         |
| `U`  | `OrderReplaced`  | 80          | A resting order was replaced in place        |
| `P`  | `TradeExecuted`  | 72          | Trade print (tape view of a fill)            |
| `S`  | `BookSnapshot`   | 32 + 24 × n | Price-level book for late joiners            |

**Every delta can be applied to a price-level book on its own.** Side, price
and quantity are always present, so a consumer that only keeps aggregated
levels (the visualiser, after a `BookSnapshot`) never needs per-order state. A
consumer that tracks individual orders (the analysis tooling, from the start of
a log) has the ids too.

### `A` OrderAdded (56 bytes)

A new order is resting. Sent for the part of a submitted order that did not
match on arrival. An order that fully fills on arrival never appears as
`OrderAdded`; it shows up only as the taker in executions.

| Offset | Size | Type | Field      | Notes                      |
| -----: | ---: | ---- | ---------- | -------------------------- |
|     24 |    8 | u64  | `order_id` |                            |
|     32 |    8 | i64  | `price`    | Ticks                      |
|     40 |    8 | u64  | `quantity` | Quantity now resting       |
|     48 |    1 | u8   | `side`     |                            |
|     49 |    7 | —    | reserved   |                            |

Level book: add `quantity` to (`side`, `price`).

### `E` OrderExecuted (80 bytes)

A resting (maker) order traded against an incoming (taker) order. One per fill:
a taker sweeping three resting orders produces three. Always immediately
followed by the `TradeExecuted` with the same `match_id`.

| Offset | Size | Type | Field             | Notes                                       |
| -----: | ---: | ---- | ----------------- | ------------------------------------------- |
|     24 |    8 | u64  | `maker_order_id`  | The resting order that was hit              |
|     32 |    8 | u64  | `taker_order_id`  | The incoming order                          |
|     40 |    8 | u64  | `match_id`        | Starts at 1, +1 per fill                    |
|     48 |    8 | i64  | `price`           | Ticks; always the maker's price             |
|     56 |    8 | u64  | `quantity`        | Quantity traded in this fill                |
|     64 |    8 | u64  | `maker_remaining` | Maker quantity still resting; 0 = left book |
|     72 |    1 | u8   | `aggressor_side`  | Taker's side; maker is the opposite side    |
|     73 |    7 | —    | reserved          |                                             |

Level book: subtract `quantity` from (opposite of `aggressor_side`, `price`).
If `maker_remaining` is 0, the level's order count drops by one.

### `X` OrderCancelled (56 bytes)

A resting order left the book without trading.

| Offset | Size | Type | Field      | Notes                                 |
| -----: | ---: | ---- | ---------- | ------------------------------------- |
|     24 |    8 | u64  | `order_id` |                                       |
|     32 |    8 | i64  | `price`    | Ticks                                 |
|     40 |    8 | u64  | `quantity` | Everything it still had resting       |
|     48 |    1 | u8   | `side`     |                                       |
|     49 |    7 | —    | reserved   |                                       |

Level book: subtract `quantity` from (`side`, `price`).

### `U` OrderReplaced (80 bytes)

A resting order was replaced by a new one that does not cross the book. The
old order is gone; the new one rests at `new_price` with `new_quantity` and
goes to the back of the queue at that price, even if the price is unchanged.
`new_order_id` may equal `old_order_id`.

| Offset | Size | Type | Field          | Notes                       |
| -----: | ---: | ---- | -------------- | --------------------------- |
|     24 |    8 | u64  | `old_order_id` |                             |
|     32 |    8 | u64  | `new_order_id` |                             |
|     40 |    8 | i64  | `old_price`    | Ticks                       |
|     48 |    8 | i64  | `new_price`    | Ticks                       |
|     56 |    8 | u64  | `old_quantity` | What the old order had left |
|     64 |    8 | u64  | `new_quantity` |                             |
|     72 |    1 | u8   | `side`         | Same for old and new        |
|     73 |    7 | —    | reserved       |                             |

Level book: subtract `old_quantity` from (`side`, `old_price`), add
`new_quantity` to (`side`, `new_price`).

A replacement that would cross the book is not sent as `OrderReplaced`, since
it trades and so cannot be applied as a plain move. It is sent as
`OrderCancelled` for the old order, then the same messages as a newly submitted
order: `OrderExecuted` + `TradeExecuted` per fill, and `OrderAdded` for any
remainder.

### `P` TradeExecuted (72 bytes)

The trade print for one fill, for consumers that want the tape but not the
book (trade-level analysis, a trades panel). Carries the same information as
the `OrderExecuted` just before it, minus `maker_remaining`.

| Offset | Size | Type | Field            | Notes                      |
| -----: | ---: | ---- | ---------------- | -------------------------- |
|     24 |    8 | u64  | `match_id`       | Same as its OrderExecuted  |
|     32 |    8 | u64  | `maker_order_id` |                            |
|     40 |    8 | u64  | `taker_order_id` |                            |
|     48 |    8 | i64  | `price`          | Ticks                      |
|     56 |    8 | u64  | `quantity`       |                            |
|     64 |    1 | u8   | `aggressor_side` |                            |
|     65 |    7 | —    | reserved         |                            |

Level book: no change (already applied by the `OrderExecuted`).

### `S` BookSnapshot (32 + 24 × n bytes)

The whole book, aggregated by price level, so a late joiner (a new WebSocket
client) can start without replaying from the beginning. Only non-empty levels
appear.

| Offset | Size | Type | Field       | Notes                              |
| -----: | ---: | ---- | ----------- | ---------------------------------- |
|     24 |    4 | u32  | `bid_count` | Number of bid levels               |
|     28 |    4 | u32  | `ask_count` | Number of ask levels               |
|     32 |  24n | —    | levels      | `bid_count` bids, then `ask_count` asks |

Bids come best (highest price) first, then asks best (lowest price) first.
`n = bid_count + ask_count`, and `length = 32 + 24 × n`. Each level is:

| Offset | Size | Type | Field         |
| -----: | ---: | ---- | ------------- |
|      0 |    8 | i64  | `price`       |
|      8 |    8 | u64  | `quantity`    |
|     16 |    8 | u64  | `order_count` |

Level book: replace the whole book with this one. Then apply deltas with
`seq > snapshot.seq`.

## Rejected operations

An operation the engine rejects (cancel of an unknown id, an invalid replace,
an order whose remainder cannot rest because its price is outside the band or
the book is full) changes nothing in the book and emits nothing. v1 has no
reject message: the stream describes the book, not the order-entry session.
Fills that happen before a remainder is rejected are still emitted, because
they did change the book.

## Versioning

The `version` field in the stream header is the format version, currently `1`.
A reader rejects a stream whose version it does not support.

Once v1 is frozen, the compatibility policy is:

- **Allowed without a version bump:** new message types, with new type codes.
  Old readers skip them using `length`.
- **Needs a version bump:** anything else. Changing, removing, reordering or
  resizing a field in an existing message; changing the meaning of a field;
  changing the stream header.
- Reserved bytes stay reserved. A new field never goes into them in an existing
  message type, because v1 readers would ignore it silently.

The golden file test enforces this: any change to the bytes of an existing
message makes it fail. Fixing that failure means a version bump, not
regenerating the golden file.
