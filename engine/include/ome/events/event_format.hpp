#ifndef ORDER_MATCHING_ENGINE_EVENT_FORMAT_HPP
#define ORDER_MATCHING_ENGINE_EVENT_FORMAT_HPP

#include "ome/types/side.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

// Wire format for the outbound event stream (v1, SPA-10). This is the
// contract every downstream consumer reads -- the binary event log, the
// WebSocket gateway, the Python analysis tooling and the visualiser -- so it
// is documented field-by-field in docs/event-stream.md and pinned by a
// golden file. After the v1 freeze, changes are additive only: new message
// types, never new or reordered fields in an existing one.
//
// Stream layout: one EventStreamHeader (EVENT_STREAM_HEADER_SIZE bytes), then
// messages back to back. Every message starts with a fixed
// EVENT_HEADER_SIZE-byte header carrying its own total length, so a reader
// can skip message types it does not know. All integers are little-endian,
// packed byte-by-byte (no struct memcpy, no padding ambiguity).

inline constexpr std::size_t EVENT_MAGIC_SIZE = 8;
inline constexpr std::array<char, EVENT_MAGIC_SIZE> EVENT_MAGIC = {'O', 'M', 'E', 'E', 'V', 'T', '\0', '\0'};
inline constexpr uint32_t EVENT_STREAM_VERSION = 1;
inline constexpr std::size_t EVENT_STREAM_HEADER_SIZE = 32;
inline constexpr std::size_t EVENT_HEADER_SIZE = 24;

// Total message sizes, header included.
inline constexpr std::size_t ORDER_ADDED_SIZE = EVENT_HEADER_SIZE + 32;
inline constexpr std::size_t ORDER_EXECUTED_SIZE = EVENT_HEADER_SIZE + 56;
inline constexpr std::size_t ORDER_CANCELLED_SIZE = EVENT_HEADER_SIZE + 32;
inline constexpr std::size_t ORDER_REPLACED_SIZE = EVENT_HEADER_SIZE + 56;
inline constexpr std::size_t TRADE_EXECUTED_SIZE = EVENT_HEADER_SIZE + 48;
inline constexpr std::size_t BOOK_SNAPSHOT_FIXED_SIZE = EVENT_HEADER_SIZE + 8; // plus levels
inline constexpr std::size_t SNAPSHOT_LEVEL_SIZE = 24;

// ASCII type codes, ITCH-style, so a hex dump is readable by eye.
enum class EventType : uint8_t {
    OrderAdded = 'A',
    OrderExecuted = 'E',
    OrderCancelled = 'X',
    OrderReplaced = 'U',
    TradeExecuted = 'P',
    BookSnapshot = 'S',
};

// Written once at the start of a stream (log file, or a gateway connection
// before its snapshot). tick_size lets a consumer turn ticks back into
// decimal prices; the band tells it the range a snapshot can span.
struct EventStreamHeader {
    uint32_t version = EVENT_STREAM_VERSION;
    uint32_t tick_size = 0;
    int64_t min_tick = 0;
    int64_t max_tick = 0;
};

// Common prefix of every message. seq increases by exactly 1 per delta
// message, starting at 1; a BookSnapshot does not consume a number, it
// carries the seq of the last delta it reflects (0 = before any delta).
// timestamp is caller-supplied (simulation or wall time, in ns); the engine
// itself never reads a clock, so replays stay byte-identical.
struct EventHeader {
    uint32_t length = 0;
    EventType type = EventType::OrderAdded;
    uint64_t seq = 0;
    uint64_t timestamp = 0;
};

// Every delta carries enough to update a price-level (aggregated) book on
// its own -- side, price and quantity are always present -- so a consumer
// that started from a BookSnapshot never needs per-order state.

// A new order is now resting. Emitted for a submitted order's unmatched
// remainder only; an order that fully fills on arrival never appears as added.
struct OrderAddedEvent {
    uint64_t order_id = 0;
    int64_t price = 0;
    uint64_t quantity = 0;
    Side side = Side::Buy;
};

// A resting (maker) order was hit. Book-side view of one fill: the maker's
// level at price loses quantity; maker_remaining is what the maker still has
// resting (0 = it left the book). The maker's side is the opposite of
// aggressor_side. Always immediately followed by its TradeExecuted.
struct OrderExecutedEvent {
    uint64_t maker_order_id = 0;
    uint64_t taker_order_id = 0;
    uint64_t match_id = 0;
    int64_t price = 0;
    uint64_t quantity = 0;
    uint64_t maker_remaining = 0;
    Side aggressor_side = Side::Buy;
};

// A resting order left the book without trading. quantity is the full
// quantity it still had resting.
struct OrderCancelledEvent {
    uint64_t order_id = 0;
    int64_t price = 0;
    uint64_t quantity = 0;
    Side side = Side::Buy;
};

// A resting order was replaced in place by one that does not cross: the old
// order is gone and the new one rests at new_price with new_quantity and
// fresh time priority. Old price/quantity are carried for level-only books.
// A replace that would cross is not sent as this message: see
// MatchingEngine::replace.
struct OrderReplacedEvent {
    uint64_t old_order_id = 0;
    uint64_t new_order_id = 0;
    int64_t old_price = 0;
    int64_t new_price = 0;
    uint64_t old_quantity = 0;
    uint64_t new_quantity = 0;
    Side side = Side::Buy;
};

// Tape view of one fill, for consumers that want trades but not the book.
// match_id links it to the OrderExecuted just before it.
struct TradeExecutedEvent {
    uint64_t match_id = 0;
    uint64_t maker_order_id = 0;
    uint64_t taker_order_id = 0;
    int64_t price = 0;
    uint64_t quantity = 0;
    Side aggressor_side = Side::Buy;
};

struct SnapshotLevel {
    int64_t price = 0;
    uint64_t quantity = 0;
    uint64_t order_count = 0;
};

// Price-level view of the whole book for late joiners. Bids best (highest)
// first, then asks best (lowest) first; only non-empty levels appear.
struct BookSnapshotEvent {
    std::vector<SnapshotLevel> bids;
    std::vector<SnapshotLevel> asks;
};

// One decoded message. std::monostate means a type this reader does not know
// (skipped by its length, per the additive-change policy).
struct DecodedEvent {
    EventHeader header;
    std::variant<std::monostate, OrderAddedEvent, OrderExecutedEvent, OrderCancelledEvent, OrderReplacedEvent,
                 TradeExecutedEvent, BookSnapshotEvent>
        body;
};

// Encoders write exactly the message size (ORDER_ADDED_SIZE etc.) into buf;
// the caller guarantees room. Allocation-free.
void encodeStreamHeader(char *buf, const EventStreamHeader &header);
void encodeOrderAdded(char *buf, uint64_t seq, uint64_t timestamp, const OrderAddedEvent &event);
void encodeOrderExecuted(char *buf, uint64_t seq, uint64_t timestamp, const OrderExecutedEvent &event);
void encodeOrderCancelled(char *buf, uint64_t seq, uint64_t timestamp, const OrderCancelledEvent &event);
void encodeOrderReplaced(char *buf, uint64_t seq, uint64_t timestamp, const OrderReplacedEvent &event);
void encodeTradeExecuted(char *buf, uint64_t seq, uint64_t timestamp, const TradeExecutedEvent &event);
// A snapshot is its fixed part followed by bid_count + ask_count levels;
// length is the whole message, levels included.
void encodeBookSnapshotFixed(char *buf, uint64_t seq, uint64_t timestamp, std::size_t length, uint32_t bid_count,
                             uint32_t ask_count);
void encodeSnapshotLevel(char *buf, const SnapshotLevel &level);

// Returns false on short input, bad magic, or unsupported version.
bool decodeStreamHeader(const char *data, std::size_t size, EventStreamHeader &header);

// Decodes the message at data. Returns the bytes consumed, or 0 if the input
// holds less than one whole message or the message is malformed (length
// shorter than its type requires).
std::size_t decodeEvent(const char *data, std::size_t size, DecodedEvent &event);

#endif // ORDER_MATCHING_ENGINE_EVENT_FORMAT_HPP
