#include "ome/events/event_writer.hpp"

#include "ome/book/order_book.hpp"

#include <optional>
#include <stdexcept>

EventWriter::EventWriter(const EventStreamHeader &header_, const std::size_t capacity_bytes)
    : header(header_), buffer(capacity_bytes) {
    if (capacity_bytes < EVENT_STREAM_HEADER_SIZE) {
        throw std::invalid_argument("EventWriter: capacity must hold the stream header");
    }
    encodeStreamHeader(buffer.data(), header);
    used = EVENT_STREAM_HEADER_SIZE;
}

char *EventWriter::reserve(const std::size_t size) {
    if (overflow || size > buffer.size() - used) {
        overflow = true;
        return nullptr;
    }
    char *slot = buffer.data() + used;
    used += size;
    return slot;
}

void EventWriter::orderAdded(const OrderAddedEvent &event) {
    if (char *slot = reserve(ORDER_ADDED_SIZE)) {
        encodeOrderAdded(slot, ++last_seq, timestamp, event);
    }
}

void EventWriter::orderExecuted(const OrderExecutedEvent &event) {
    OrderExecutedEvent executed = event;
    executed.match_id = ++last_match_id;
    if (char *slot = reserve(ORDER_EXECUTED_SIZE)) {
        encodeOrderExecuted(slot, ++last_seq, timestamp, executed);
    }
    const TradeExecutedEvent trade{executed.match_id, executed.maker_order_id, executed.taker_order_id,
                                   executed.price,    executed.quantity,       executed.aggressor_side};
    if (char *slot = reserve(TRADE_EXECUTED_SIZE)) {
        encodeTradeExecuted(slot, ++last_seq, timestamp, trade);
    }
}

void EventWriter::orderCancelled(const OrderCancelledEvent &event) {
    if (char *slot = reserve(ORDER_CANCELLED_SIZE)) {
        encodeOrderCancelled(slot, ++last_seq, timestamp, event);
    }
}

void EventWriter::orderReplaced(const OrderReplacedEvent &event) {
    if (char *slot = reserve(ORDER_REPLACED_SIZE)) {
        encodeOrderReplaced(slot, ++last_seq, timestamp, event);
    }
}

void EventWriter::bookSnapshot(const OrderBook &book) {
    const std::size_t start = used;
    if (reserve(BOOK_SNAPSHOT_FIXED_SIZE) == nullptr) {
        return;
    }

    // Levels are written first and the counts patched in afterwards, so the
    // band is walked once. A snapshot that runs out of room part-way is
    // rolled back whole: the buffer only ever holds complete messages.
    uint32_t bid_count = 0;
    uint32_t ask_count = 0;
    const auto appendLevel = [&](const Side side, const int64_t ticks, uint32_t &count) {
        const uint64_t quantity = book.levelQuantity(side, Ticks(ticks)).getValue();
        if (quantity == 0) {
            return true;
        }
        char *slot = reserve(SNAPSHOT_LEVEL_SIZE);
        if (slot == nullptr) {
            return false;
        }
        encodeSnapshotLevel(slot, SnapshotLevel{ticks, quantity, book.levelOrderCount(side, Ticks(ticks))});
        ++count;
        return true;
    };

    if (const std::optional<Ticks> best_bid = book.bestBid()) {
        for (int64_t ticks = best_bid->getValue(); ticks >= header.min_tick; --ticks) {
            if (!appendLevel(Side::Buy, ticks, bid_count)) {
                used = start;
                return;
            }
        }
    }
    if (const std::optional<Ticks> best_ask = book.bestAsk()) {
        for (int64_t ticks = best_ask->getValue(); ticks <= header.max_tick; ++ticks) {
            if (!appendLevel(Side::Sell, ticks, ask_count)) {
                used = start;
                return;
            }
        }
    }

    encodeBookSnapshotFixed(buffer.data() + start, last_seq, timestamp, used - start, bid_count, ask_count);
}
