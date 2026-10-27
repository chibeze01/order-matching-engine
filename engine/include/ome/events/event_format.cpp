#include "ome/events/event_format.hpp"

#include <utility>

namespace {

// Same byte-by-byte little-endian packing as the input log (log_format.cpp),
// so neither format depends on host struct layout or endianness.

void putU8(char *buf, std::size_t &offset, const uint8_t v) { buf[offset++] = static_cast<char>(v); }

void putZeros(char *buf, std::size_t &offset, const std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        buf[offset++] = 0;
    }
}

void putU32LE(char *buf, std::size_t &offset, const uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        buf[offset++] = static_cast<char>((v >> (8 * i)) & 0xFF);
    }
}

void putU64LE(char *buf, std::size_t &offset, const uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        buf[offset++] = static_cast<char>((v >> (8 * i)) & 0xFF);
    }
}

void putI64LE(char *buf, std::size_t &offset, const int64_t v) { putU64LE(buf, offset, static_cast<uint64_t>(v)); }

void putSide(char *buf, std::size_t &offset, const Side side) { putU8(buf, offset, side == Side::Buy ? 0 : 1); }

uint8_t getU8(const char *buf, std::size_t &offset) { return static_cast<uint8_t>(buf[offset++]); }

uint32_t getU32LE(const char *buf, std::size_t &offset) {
    uint32_t result = 0;
    for (int i = 0; i < 4; ++i) {
        result |= static_cast<uint32_t>(static_cast<uint8_t>(buf[offset++])) << (8 * i);
    }
    return result;
}

uint64_t getU64LE(const char *buf, std::size_t &offset) {
    uint64_t result = 0;
    for (int i = 0; i < 8; ++i) {
        result |= static_cast<uint64_t>(static_cast<uint8_t>(buf[offset++])) << (8 * i);
    }
    return result;
}

int64_t getI64LE(const char *buf, std::size_t &offset) { return static_cast<int64_t>(getU64LE(buf, offset)); }

Side getSide(const char *buf, std::size_t &offset) { return getU8(buf, offset) == 0 ? Side::Buy : Side::Sell; }

void putHeader(char *buf, std::size_t &offset, const std::size_t length, const EventType type, const uint64_t seq,
               const uint64_t timestamp) {
    putU32LE(buf, offset, static_cast<uint32_t>(length));
    putU8(buf, offset, static_cast<uint8_t>(type));
    putZeros(buf, offset, 3); // reserved
    putU64LE(buf, offset, seq);
    putU64LE(buf, offset, timestamp);
}

SnapshotLevel getSnapshotLevel(const char *buf, std::size_t &offset) {
    SnapshotLevel level;
    level.price = getI64LE(buf, offset);
    level.quantity = getU64LE(buf, offset);
    level.order_count = getU64LE(buf, offset);
    return level;
}

// Minimum total length for each known type. A message claiming less is
// malformed; one claiming more is accepted (trailing bytes ignored).
std::size_t minimumLength(const EventType type) {
    switch (type) {
    case EventType::OrderAdded:
        return ORDER_ADDED_SIZE;
    case EventType::OrderExecuted:
        return ORDER_EXECUTED_SIZE;
    case EventType::OrderCancelled:
        return ORDER_CANCELLED_SIZE;
    case EventType::OrderReplaced:
        return ORDER_REPLACED_SIZE;
    case EventType::TradeExecuted:
        return TRADE_EXECUTED_SIZE;
    case EventType::BookSnapshot:
        return BOOK_SNAPSHOT_FIXED_SIZE;
    }
    return EVENT_HEADER_SIZE; // unknown type: only the header is required
}

} // namespace

void encodeStreamHeader(char *buf, const EventStreamHeader &header) {
    std::size_t offset = 0;
    for (const char c : EVENT_MAGIC) {
        buf[offset++] = c;
    }
    putU32LE(buf, offset, header.version);
    putU32LE(buf, offset, header.tick_size);
    putI64LE(buf, offset, header.min_tick);
    putI64LE(buf, offset, header.max_tick);
}

void encodeBookSnapshotFixed(char *buf, const uint64_t seq, const uint64_t timestamp, const std::size_t length,
                             const uint32_t bid_count, const uint32_t ask_count) {
    std::size_t offset = 0;
    putHeader(buf, offset, length, EventType::BookSnapshot, seq, timestamp);
    putU32LE(buf, offset, bid_count);
    putU32LE(buf, offset, ask_count);
}

void encodeOrderAdded(char *buf, const uint64_t seq, const uint64_t timestamp, const OrderAddedEvent &event) {
    std::size_t offset = 0;
    putHeader(buf, offset, ORDER_ADDED_SIZE, EventType::OrderAdded, seq, timestamp);
    putU64LE(buf, offset, event.order_id);
    putI64LE(buf, offset, event.price);
    putU64LE(buf, offset, event.quantity);
    putSide(buf, offset, event.side);
    putZeros(buf, offset, 7); // reserved
}

void encodeOrderExecuted(char *buf, const uint64_t seq, const uint64_t timestamp, const OrderExecutedEvent &event) {
    std::size_t offset = 0;
    putHeader(buf, offset, ORDER_EXECUTED_SIZE, EventType::OrderExecuted, seq, timestamp);
    putU64LE(buf, offset, event.maker_order_id);
    putU64LE(buf, offset, event.taker_order_id);
    putU64LE(buf, offset, event.match_id);
    putI64LE(buf, offset, event.price);
    putU64LE(buf, offset, event.quantity);
    putU64LE(buf, offset, event.maker_remaining);
    putSide(buf, offset, event.aggressor_side);
    putZeros(buf, offset, 7); // reserved
}

void encodeOrderCancelled(char *buf, const uint64_t seq, const uint64_t timestamp, const OrderCancelledEvent &event) {
    std::size_t offset = 0;
    putHeader(buf, offset, ORDER_CANCELLED_SIZE, EventType::OrderCancelled, seq, timestamp);
    putU64LE(buf, offset, event.order_id);
    putI64LE(buf, offset, event.price);
    putU64LE(buf, offset, event.quantity);
    putSide(buf, offset, event.side);
    putZeros(buf, offset, 7); // reserved
}

void encodeOrderReplaced(char *buf, const uint64_t seq, const uint64_t timestamp, const OrderReplacedEvent &event) {
    std::size_t offset = 0;
    putHeader(buf, offset, ORDER_REPLACED_SIZE, EventType::OrderReplaced, seq, timestamp);
    putU64LE(buf, offset, event.old_order_id);
    putU64LE(buf, offset, event.new_order_id);
    putI64LE(buf, offset, event.old_price);
    putI64LE(buf, offset, event.new_price);
    putU64LE(buf, offset, event.old_quantity);
    putU64LE(buf, offset, event.new_quantity);
    putSide(buf, offset, event.side);
    putZeros(buf, offset, 7); // reserved
}

void encodeTradeExecuted(char *buf, const uint64_t seq, const uint64_t timestamp, const TradeExecutedEvent &event) {
    std::size_t offset = 0;
    putHeader(buf, offset, TRADE_EXECUTED_SIZE, EventType::TradeExecuted, seq, timestamp);
    putU64LE(buf, offset, event.match_id);
    putU64LE(buf, offset, event.maker_order_id);
    putU64LE(buf, offset, event.taker_order_id);
    putI64LE(buf, offset, event.price);
    putU64LE(buf, offset, event.quantity);
    putSide(buf, offset, event.aggressor_side);
    putZeros(buf, offset, 7); // reserved
}

void encodeSnapshotLevel(char *buf, const SnapshotLevel &level) {
    std::size_t offset = 0;
    putI64LE(buf, offset, level.price);
    putU64LE(buf, offset, level.quantity);
    putU64LE(buf, offset, level.order_count);
}

bool decodeStreamHeader(const char *data, const std::size_t size, EventStreamHeader &header) {
    if (size < EVENT_STREAM_HEADER_SIZE) {
        return false;
    }
    std::size_t offset = 0;
    for (const char expected : EVENT_MAGIC) {
        if (data[offset++] != expected) {
            return false;
        }
    }
    EventStreamHeader parsed;
    parsed.version = getU32LE(data, offset);
    if (parsed.version != EVENT_STREAM_VERSION) {
        return false;
    }
    parsed.tick_size = getU32LE(data, offset);
    parsed.min_tick = getI64LE(data, offset);
    parsed.max_tick = getI64LE(data, offset);
    header = parsed;
    return true;
}

std::size_t decodeEvent(const char *data, const std::size_t size, DecodedEvent &event) {
    if (size < EVENT_HEADER_SIZE) {
        return 0;
    }
    std::size_t offset = 0;
    EventHeader header;
    header.length = getU32LE(data, offset);
    header.type = static_cast<EventType>(getU8(data, offset));
    offset += 3; // reserved
    header.seq = getU64LE(data, offset);
    header.timestamp = getU64LE(data, offset);
    if (header.length > size || header.length < minimumLength(header.type)) {
        return 0;
    }

    DecodedEvent decoded;
    decoded.header = header;
    switch (header.type) {
    case EventType::OrderAdded: {
        OrderAddedEvent body;
        body.order_id = getU64LE(data, offset);
        body.price = getI64LE(data, offset);
        body.quantity = getU64LE(data, offset);
        body.side = getSide(data, offset);
        decoded.body = body;
        break;
    }
    case EventType::OrderExecuted: {
        OrderExecutedEvent body;
        body.maker_order_id = getU64LE(data, offset);
        body.taker_order_id = getU64LE(data, offset);
        body.match_id = getU64LE(data, offset);
        body.price = getI64LE(data, offset);
        body.quantity = getU64LE(data, offset);
        body.maker_remaining = getU64LE(data, offset);
        body.aggressor_side = getSide(data, offset);
        decoded.body = body;
        break;
    }
    case EventType::OrderCancelled: {
        OrderCancelledEvent body;
        body.order_id = getU64LE(data, offset);
        body.price = getI64LE(data, offset);
        body.quantity = getU64LE(data, offset);
        body.side = getSide(data, offset);
        decoded.body = body;
        break;
    }
    case EventType::OrderReplaced: {
        OrderReplacedEvent body;
        body.old_order_id = getU64LE(data, offset);
        body.new_order_id = getU64LE(data, offset);
        body.old_price = getI64LE(data, offset);
        body.new_price = getI64LE(data, offset);
        body.old_quantity = getU64LE(data, offset);
        body.new_quantity = getU64LE(data, offset);
        body.side = getSide(data, offset);
        decoded.body = body;
        break;
    }
    case EventType::TradeExecuted: {
        TradeExecutedEvent body;
        body.match_id = getU64LE(data, offset);
        body.maker_order_id = getU64LE(data, offset);
        body.taker_order_id = getU64LE(data, offset);
        body.price = getI64LE(data, offset);
        body.quantity = getU64LE(data, offset);
        body.aggressor_side = getSide(data, offset);
        decoded.body = body;
        break;
    }
    case EventType::BookSnapshot: {
        const uint32_t bid_count = getU32LE(data, offset);
        const uint32_t ask_count = getU32LE(data, offset);
        const std::size_t levels = static_cast<std::size_t>(bid_count) + ask_count;
        if (header.length < BOOK_SNAPSHOT_FIXED_SIZE + levels * SNAPSHOT_LEVEL_SIZE) {
            return 0;
        }
        BookSnapshotEvent body;
        body.bids.reserve(bid_count);
        body.asks.reserve(ask_count);
        for (uint32_t i = 0; i < bid_count; ++i) {
            body.bids.push_back(getSnapshotLevel(data, offset));
        }
        for (uint32_t i = 0; i < ask_count; ++i) {
            body.asks.push_back(getSnapshotLevel(data, offset));
        }
        decoded.body = std::move(body);
        break;
    }
    default:
        break; // unknown type: body stays std::monostate, skipped by length
    }
    event = std::move(decoded);
    return header.length;
}
