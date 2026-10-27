#include "ome/book/order_book.hpp"
#include "ome/events/event_format.hpp"
#include "ome/events/event_writer.hpp"
#include "ome/matching/matching_engine.hpp"
#include "ome/types/order.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

constexpr uint8_t CENT_TICK_SIZE = 0;
constexpr int64_t BAND_MIN = 100;
constexpr int64_t BAND_MAX = 200;
constexpr std::size_t CAPACITY = 64;
constexpr std::size_t BUFFER_BYTES = 64 * 1024;

const EventStreamHeader STREAM_HEADER{EVENT_STREAM_VERSION, 1, BAND_MIN, BAND_MAX};

Order makeOrder(const uint64_t id, const int64_t ticks, const uint64_t qty, const Side side) {
    return Order{OrderId(id), Price(Ticks(ticks), TickSize(CENT_TICK_SIZE)), Quantity(qty), side, OrderType::Limit};
}

OrderBook makeBook() { return OrderBook(Ticks(BAND_MIN), Ticks(BAND_MAX), CAPACITY); }

// Decodes every message in the writer's buffer from start: past the stream
// header for a fresh writer, 0 once it has been clear()ed. Fails the test on
// a malformed or truncated stream.
std::vector<DecodedEvent> decodeAll(const EventWriter &writer, const std::size_t start = EVENT_STREAM_HEADER_SIZE) {
    std::vector<DecodedEvent> events;
    std::size_t offset = start;
    while (offset < writer.size()) {
        DecodedEvent event;
        const std::size_t consumed = decodeEvent(writer.data() + offset, writer.size() - offset, event);
        EXPECT_NE(consumed, 0U) << "malformed message at byte " << offset;
        if (consumed == 0) {
            break;
        }
        events.push_back(std::move(event));
        offset += consumed;
    }
    return events;
}

template <typename T> const T &bodyAs(const DecodedEvent &event) {
    EXPECT_TRUE(std::holds_alternative<T>(event.body));
    return std::get<T>(event.body);
}

uint64_t readU64LE(const char *data) {
    uint64_t result = 0;
    for (int i = 0; i < 8; ++i) {
        result |= static_cast<uint64_t>(static_cast<uint8_t>(data[i])) << (8 * i);
    }
    return result;
}

TEST(EventFormat, StreamHeaderLayout) {
    std::vector<char> buf(EVENT_STREAM_HEADER_SIZE);
    encodeStreamHeader(buf.data(), EventStreamHeader{EVENT_STREAM_VERSION, 5, -3, 0x0102030405060708});

    EXPECT_EQ(std::string(buf.data(), 6), "OMEEVT");
    EXPECT_EQ(buf[6], 0);
    EXPECT_EQ(buf[7], 0);
    EXPECT_EQ(static_cast<uint8_t>(buf[8]), 1U); // version, LE u32
    EXPECT_EQ(buf[9], 0);
    EXPECT_EQ(static_cast<uint8_t>(buf[12]), 5U); // tick_size
    EXPECT_EQ(readU64LE(buf.data() + 16), static_cast<uint64_t>(-3));
    EXPECT_EQ(static_cast<uint8_t>(buf[24]), 0x08U); // little-endian: low byte first
    EXPECT_EQ(static_cast<uint8_t>(buf[31]), 0x01U);

    EventStreamHeader decoded;
    ASSERT_TRUE(decodeStreamHeader(buf.data(), buf.size(), decoded));
    EXPECT_EQ(decoded.tick_size, 5U);
    EXPECT_EQ(decoded.min_tick, -3);
    EXPECT_EQ(decoded.max_tick, 0x0102030405060708);
}

TEST(EventFormat, StreamHeaderRejectsBadMagicVersionAndShortInput) {
    std::vector<char> buf(EVENT_STREAM_HEADER_SIZE);
    encodeStreamHeader(buf.data(), STREAM_HEADER);
    EventStreamHeader decoded;
    EXPECT_FALSE(decodeStreamHeader(buf.data(), buf.size() - 1, decoded));

    std::vector<char> bad_magic = buf;
    bad_magic[0] = 'X';
    EXPECT_FALSE(decodeStreamHeader(bad_magic.data(), bad_magic.size(), decoded));

    std::vector<char> bad_version = buf;
    bad_version[8] = 2;
    EXPECT_FALSE(decodeStreamHeader(bad_version.data(), bad_version.size(), decoded));
}

TEST(EventFormat, OrderAddedFieldOffsets) {
    std::vector<char> buf(ORDER_ADDED_SIZE);
    encodeOrderAdded(buf.data(), 7, 9000, OrderAddedEvent{42, 150, 25, Side::Sell});

    EXPECT_EQ(static_cast<uint8_t>(buf[0]), ORDER_ADDED_SIZE); // length
    EXPECT_EQ(buf[4], 'A');                                    // type
    EXPECT_EQ(buf[5], 0);                                      // reserved
    EXPECT_EQ(readU64LE(buf.data() + 8), 7U);                  // seq
    EXPECT_EQ(readU64LE(buf.data() + 16), 9000U);              // timestamp
    EXPECT_EQ(readU64LE(buf.data() + 24), 42U);                // order_id
    EXPECT_EQ(readU64LE(buf.data() + 32), 150U);               // price
    EXPECT_EQ(readU64LE(buf.data() + 40), 25U);                // quantity
    EXPECT_EQ(buf[48], 1);                                     // side: Sell
    for (std::size_t i = 49; i < ORDER_ADDED_SIZE; ++i) {
        EXPECT_EQ(buf[i], 0) << "reserved byte " << i;
    }
}

TEST(EventFormat, EveryMessageRoundTrips) {
    std::vector<char> buf(ORDER_EXECUTED_SIZE);
    DecodedEvent event;

    encodeOrderExecuted(buf.data(), 1, 2, OrderExecutedEvent{10, 11, 3, -5, 6, 4, Side::Sell});
    ASSERT_EQ(decodeEvent(buf.data(), buf.size(), event), ORDER_EXECUTED_SIZE);
    const auto &executed = bodyAs<OrderExecutedEvent>(event);
    EXPECT_EQ(event.header.seq, 1U);
    EXPECT_EQ(event.header.timestamp, 2U);
    EXPECT_EQ(executed.maker_order_id, 10U);
    EXPECT_EQ(executed.taker_order_id, 11U);
    EXPECT_EQ(executed.match_id, 3U);
    EXPECT_EQ(executed.price, -5);
    EXPECT_EQ(executed.quantity, 6U);
    EXPECT_EQ(executed.maker_remaining, 4U);
    EXPECT_EQ(executed.aggressor_side, Side::Sell);

    encodeOrderCancelled(buf.data(), 1, 2, OrderCancelledEvent{10, 120, 30, Side::Buy});
    ASSERT_EQ(decodeEvent(buf.data(), buf.size(), event), ORDER_CANCELLED_SIZE);
    const auto &cancelled = bodyAs<OrderCancelledEvent>(event);
    EXPECT_EQ(cancelled.order_id, 10U);
    EXPECT_EQ(cancelled.price, 120);
    EXPECT_EQ(cancelled.quantity, 30U);
    EXPECT_EQ(cancelled.side, Side::Buy);

    encodeOrderReplaced(buf.data(), 1, 2, OrderReplacedEvent{10, 12, 120, 118, 30, 25, Side::Sell});
    ASSERT_EQ(decodeEvent(buf.data(), buf.size(), event), ORDER_REPLACED_SIZE);
    const auto &replaced = bodyAs<OrderReplacedEvent>(event);
    EXPECT_EQ(replaced.old_order_id, 10U);
    EXPECT_EQ(replaced.new_order_id, 12U);
    EXPECT_EQ(replaced.old_price, 120);
    EXPECT_EQ(replaced.new_price, 118);
    EXPECT_EQ(replaced.old_quantity, 30U);
    EXPECT_EQ(replaced.new_quantity, 25U);
    EXPECT_EQ(replaced.side, Side::Sell);

    encodeTradeExecuted(buf.data(), 1, 2, TradeExecutedEvent{3, 10, 11, 150, 6, Side::Buy});
    ASSERT_EQ(decodeEvent(buf.data(), buf.size(), event), TRADE_EXECUTED_SIZE);
    const auto &trade = bodyAs<TradeExecutedEvent>(event);
    EXPECT_EQ(trade.match_id, 3U);
    EXPECT_EQ(trade.maker_order_id, 10U);
    EXPECT_EQ(trade.taker_order_id, 11U);
    EXPECT_EQ(trade.price, 150);
    EXPECT_EQ(trade.quantity, 6U);
    EXPECT_EQ(trade.aggressor_side, Side::Buy);
}

TEST(EventFormat, DecodeRejectsTruncatedAndUndersizedMessages) {
    std::vector<char> buf(ORDER_ADDED_SIZE);
    encodeOrderAdded(buf.data(), 1, 0, OrderAddedEvent{1, 150, 10, Side::Buy});
    DecodedEvent event;
    EXPECT_EQ(decodeEvent(buf.data(), EVENT_HEADER_SIZE - 1, event), 0U);
    EXPECT_EQ(decodeEvent(buf.data(), ORDER_ADDED_SIZE - 1, event), 0U);

    buf[0] = static_cast<char>(ORDER_ADDED_SIZE - 8); // claims fewer bytes than an OrderAdded needs
    EXPECT_EQ(decodeEvent(buf.data(), buf.size(), event), 0U);
}

TEST(EventFormat, UnknownTypeIsSkippedByLength) {
    // A future additive message type: header plus 8 body bytes.
    std::vector<char> buf(EVENT_HEADER_SIZE + 8 + ORDER_ADDED_SIZE, 0);
    buf[0] = static_cast<char>(EVENT_HEADER_SIZE + 8);
    buf[4] = 'Z';
    encodeOrderAdded(buf.data() + EVENT_HEADER_SIZE + 8, 2, 0, OrderAddedEvent{1, 150, 10, Side::Buy});

    DecodedEvent event;
    const std::size_t consumed = decodeEvent(buf.data(), buf.size(), event);
    ASSERT_EQ(consumed, EVENT_HEADER_SIZE + 8);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(event.body));
    ASSERT_EQ(decodeEvent(buf.data() + consumed, buf.size() - consumed, event), ORDER_ADDED_SIZE);
    EXPECT_EQ(bodyAs<OrderAddedEvent>(event).order_id, 1U);
}

TEST(EventWriter, StartsWithStreamHeaderAndRejectsTinyBuffer) {
    const EventWriter writer(STREAM_HEADER, BUFFER_BYTES);
    ASSERT_EQ(writer.size(), EVENT_STREAM_HEADER_SIZE);
    EventStreamHeader decoded;
    ASSERT_TRUE(decodeStreamHeader(writer.data(), writer.size(), decoded));
    EXPECT_EQ(decoded.min_tick, BAND_MIN);
    EXPECT_EQ(decoded.max_tick, BAND_MAX);
    EXPECT_EQ(writer.lastSeq(), 0U);

    EXPECT_THROW(EventWriter(STREAM_HEADER, EVENT_STREAM_HEADER_SIZE - 1), std::invalid_argument);
}

TEST(EventWriter, SequenceIsMonotonicAcrossClearAndTimestampIsStamped) {
    EventWriter writer(STREAM_HEADER, BUFFER_BYTES);
    writer.setTimestamp(100);
    writer.orderAdded(OrderAddedEvent{1, 150, 10, Side::Buy});
    writer.orderExecuted(OrderExecutedEvent{1, 2, 0, 150, 4, 6, Side::Sell});
    std::vector<DecodedEvent> first = decodeAll(writer);
    ASSERT_EQ(first.size(), 3U);
    EXPECT_EQ(first[0].header.seq, 1U);
    EXPECT_EQ(first[1].header.seq, 2U);
    EXPECT_EQ(first[2].header.seq, 3U);
    EXPECT_EQ(first[2].header.timestamp, 100U);

    // Drained: the header is not repeated, numbering carries on.
    writer.clear();
    writer.setTimestamp(200);
    writer.orderCancelled(OrderCancelledEvent{1, 150, 6, Side::Buy});
    DecodedEvent event;
    ASSERT_EQ(decodeEvent(writer.data(), writer.size(), event), ORDER_CANCELLED_SIZE);
    EXPECT_EQ(event.header.seq, 4U);
    EXPECT_EQ(event.header.timestamp, 200U);
}

TEST(EventWriter, ExecutionIsFollowedByLinkedTradePrint) {
    EventWriter writer(STREAM_HEADER, BUFFER_BYTES);
    writer.orderExecuted(OrderExecutedEvent{1, 2, 999, 150, 4, 6, Side::Sell}); // match_id is assigned, not taken
    writer.orderExecuted(OrderExecutedEvent{3, 2, 999, 149, 1, 0, Side::Sell});
    const std::vector<DecodedEvent> events = decodeAll(writer);
    ASSERT_EQ(events.size(), 4U);

    const auto &first_exec = bodyAs<OrderExecutedEvent>(events[0]);
    const auto &first_trade = bodyAs<TradeExecutedEvent>(events[1]);
    EXPECT_EQ(first_exec.match_id, 1U);
    EXPECT_EQ(first_trade.match_id, 1U);
    EXPECT_EQ(first_trade.maker_order_id, 1U);
    EXPECT_EQ(first_trade.taker_order_id, 2U);
    EXPECT_EQ(first_trade.price, 150);
    EXPECT_EQ(first_trade.quantity, 4U);
    EXPECT_EQ(first_trade.aggressor_side, Side::Sell);
    EXPECT_EQ(bodyAs<OrderExecutedEvent>(events[2]).match_id, 2U);
    EXPECT_EQ(bodyAs<TradeExecutedEvent>(events[3]).match_id, 2U);
}

TEST(EventWriter, OverflowLatchesAndKeepsOnlyWholeMessages) {
    EventWriter writer(STREAM_HEADER, EVENT_STREAM_HEADER_SIZE + ORDER_ADDED_SIZE + 10);
    writer.orderAdded(OrderAddedEvent{1, 150, 10, Side::Buy});
    EXPECT_FALSE(writer.overflowed());
    writer.orderAdded(OrderAddedEvent{2, 150, 10, Side::Buy});
    EXPECT_TRUE(writer.overflowed());
    EXPECT_EQ(writer.size(), EVENT_STREAM_HEADER_SIZE + ORDER_ADDED_SIZE);
    EXPECT_EQ(writer.lastSeq(), 1U);

    // Latched: even after draining, nothing more is written.
    writer.clear();
    writer.orderAdded(OrderAddedEvent{3, 150, 10, Side::Buy});
    EXPECT_TRUE(writer.overflowed());
    EXPECT_EQ(writer.size(), 0U);
}

TEST(EventWriter, SnapshotThatDoesNotFitIsRolledBackWhole) {
    OrderBook book = makeBook();
    book.insert(makeOrder(1, 150, 10, Side::Buy));
    book.insert(makeOrder(2, 149, 10, Side::Buy));
    EventWriter writer(STREAM_HEADER, EVENT_STREAM_HEADER_SIZE + BOOK_SNAPSHOT_FIXED_SIZE + SNAPSHOT_LEVEL_SIZE);
    writer.bookSnapshot(book);
    EXPECT_TRUE(writer.overflowed());
    EXPECT_EQ(writer.size(), EVENT_STREAM_HEADER_SIZE);
}

TEST(EventWriter, SnapshotIsLevelAggregatedBestFirstAndCarriesLastSeq) {
    OrderBook book = makeBook();
    EventWriter writer(STREAM_HEADER, BUFFER_BYTES);
    MatchingEngine engine(book, &writer);
    engine.submitLimit(makeOrder(1, 150, 10, Side::Buy));
    engine.submitLimit(makeOrder(2, 150, 5, Side::Buy));
    engine.submitLimit(makeOrder(3, 140, 7, Side::Buy));
    engine.submitLimit(makeOrder(4, 160, 3, Side::Sell));
    engine.submitLimit(makeOrder(5, 170, 8, Side::Sell));
    engine.submitLimit(makeOrder(6, BAND_MAX, 1, Side::Sell)); // band edge is included

    writer.clear();
    writer.setTimestamp(77);
    writer.bookSnapshot(book);
    EXPECT_EQ(writer.lastSeq(), 6U); // a snapshot does not consume a sequence number

    DecodedEvent event;
    ASSERT_EQ(decodeEvent(writer.data(), writer.size(), event), writer.size());
    EXPECT_EQ(event.header.type, EventType::BookSnapshot);
    EXPECT_EQ(event.header.seq, 6U);
    EXPECT_EQ(event.header.timestamp, 77U);
    const auto &snapshot = bodyAs<BookSnapshotEvent>(event);
    ASSERT_EQ(snapshot.bids.size(), 2U);
    EXPECT_EQ(snapshot.bids[0].price, 150);
    EXPECT_EQ(snapshot.bids[0].quantity, 15U);
    EXPECT_EQ(snapshot.bids[0].order_count, 2U);
    EXPECT_EQ(snapshot.bids[1].price, 140);
    ASSERT_EQ(snapshot.asks.size(), 3U);
    EXPECT_EQ(snapshot.asks[0].price, 160);
    EXPECT_EQ(snapshot.asks[1].price, 170);
    EXPECT_EQ(snapshot.asks[2].price, BAND_MAX);
}

TEST(EventWriter, EmptyBookSnapshotHasNoLevels) {
    const OrderBook book = makeBook();
    EventWriter writer(STREAM_HEADER, BUFFER_BYTES);
    writer.clear();
    writer.bookSnapshot(book);
    ASSERT_EQ(writer.size(), BOOK_SNAPSHOT_FIXED_SIZE);
    DecodedEvent event;
    ASSERT_EQ(decodeEvent(writer.data(), writer.size(), event), BOOK_SNAPSHOT_FIXED_SIZE);
    EXPECT_EQ(event.header.seq, 0U);
    EXPECT_TRUE(bodyAs<BookSnapshotEvent>(event).bids.empty());
    EXPECT_TRUE(bodyAs<BookSnapshotEvent>(event).asks.empty());
}

TEST(MatchingEngineEvents, RestingOrderEmitsOrderAdded) {
    OrderBook book = makeBook();
    EventWriter writer(STREAM_HEADER, BUFFER_BYTES);
    MatchingEngine engine(book, &writer);
    engine.submitLimit(makeOrder(1, 150, 10, Side::Buy));

    const std::vector<DecodedEvent> events = decodeAll(writer);
    ASSERT_EQ(events.size(), 1U);
    const auto &added = bodyAs<OrderAddedEvent>(events[0]);
    EXPECT_EQ(added.order_id, 1U);
    EXPECT_EQ(added.price, 150);
    EXPECT_EQ(added.quantity, 10U);
    EXPECT_EQ(added.side, Side::Buy);
}

TEST(MatchingEngineEvents, SweepEmitsExecutionPerFillThenRemainderAdded) {
    OrderBook book = makeBook();
    EventWriter writer(STREAM_HEADER, BUFFER_BYTES);
    MatchingEngine engine(book, &writer);
    engine.submitLimit(makeOrder(1, 150, 10, Side::Sell));
    engine.submitLimit(makeOrder(2, 151, 10, Side::Sell));
    writer.clear();

    engine.submitLimit(makeOrder(3, 151, 25, Side::Buy));
    const std::vector<DecodedEvent> events = decodeAll(writer, 0);
    ASSERT_EQ(events.size(), 5U); // E P E P A

    const auto &first = bodyAs<OrderExecutedEvent>(events[0]);
    EXPECT_EQ(first.maker_order_id, 1U);
    EXPECT_EQ(first.taker_order_id, 3U);
    EXPECT_EQ(first.price, 150);
    EXPECT_EQ(first.quantity, 10U);
    EXPECT_EQ(first.maker_remaining, 0U);
    EXPECT_EQ(first.aggressor_side, Side::Buy);
    EXPECT_EQ(bodyAs<TradeExecutedEvent>(events[1]).match_id, first.match_id);
    EXPECT_EQ(bodyAs<OrderExecutedEvent>(events[2]).maker_order_id, 2U);
    EXPECT_EQ(bodyAs<OrderExecutedEvent>(events[2]).price, 151);
    const auto &remainder = bodyAs<OrderAddedEvent>(events[4]);
    EXPECT_EQ(remainder.order_id, 3U);
    EXPECT_EQ(remainder.price, 151);
    EXPECT_EQ(remainder.quantity, 5U);
}

TEST(MatchingEngineEvents, PartialMakerFillReportsMakerRemaining) {
    OrderBook book = makeBook();
    EventWriter writer(STREAM_HEADER, BUFFER_BYTES);
    MatchingEngine engine(book, &writer);
    engine.submitLimit(makeOrder(1, 150, 10, Side::Buy));
    writer.clear();

    engine.submitLimit(makeOrder(2, 150, 4, Side::Sell));
    const std::vector<DecodedEvent> events = decodeAll(writer, 0);
    ASSERT_EQ(events.size(), 2U); // fully filled taker: no OrderAdded
    const auto &executed = bodyAs<OrderExecutedEvent>(events[0]);
    EXPECT_EQ(executed.quantity, 4U);
    EXPECT_EQ(executed.maker_remaining, 6U);
    EXPECT_EQ(executed.aggressor_side, Side::Sell);
}

TEST(MatchingEngineEvents, CancelEmitsRestingQuantityAndUnknownIdEmitsNothing) {
    OrderBook book = makeBook();
    EventWriter writer(STREAM_HEADER, BUFFER_BYTES);
    MatchingEngine engine(book, &writer);
    engine.submitLimit(makeOrder(1, 150, 10, Side::Buy));
    engine.submitLimit(makeOrder(2, 150, 3, Side::Sell)); // leaves 7 resting
    writer.clear();

    EXPECT_TRUE(engine.cancel(OrderId(1)));
    EXPECT_FALSE(engine.cancel(OrderId(1)));
    EXPECT_FALSE(engine.cancel(OrderId(99)));
    EXPECT_EQ(book.size(), 0U);

    const std::vector<DecodedEvent> events = decodeAll(writer, 0);
    ASSERT_EQ(events.size(), 1U);
    const auto &cancelled = bodyAs<OrderCancelledEvent>(events[0]);
    EXPECT_EQ(cancelled.order_id, 1U);
    EXPECT_EQ(cancelled.price, 150);
    EXPECT_EQ(cancelled.quantity, 7U);
    EXPECT_EQ(cancelled.side, Side::Buy);
}

TEST(MatchingEngineEvents, NonCrossingReplaceEmitsOrderReplacedAndLosesPriority) {
    OrderBook book = makeBook();
    EventWriter writer(STREAM_HEADER, BUFFER_BYTES);
    MatchingEngine engine(book, &writer);
    engine.submitLimit(makeOrder(1, 150, 10, Side::Buy));
    engine.submitLimit(makeOrder(2, 150, 5, Side::Buy));
    writer.clear();

    const std::optional<SubmitResult> result = engine.replace(OrderId(1), makeOrder(3, 150, 8, Side::Buy));
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->trades.empty());
    ASSERT_NE(result->resting, nullptr);
    EXPECT_EQ(result->status, SubmitStatus::Rested);
    EXPECT_EQ(book.find(OrderId(1)), nullptr);
    EXPECT_EQ(book.levelFront(Side::Buy, Ticks(150))->order_id, 2U); // replacement went to the back
    EXPECT_EQ(book.levelQuantity(Side::Buy, Ticks(150)), Quantity(13));

    const std::vector<DecodedEvent> events = decodeAll(writer, 0);
    ASSERT_EQ(events.size(), 1U);
    const auto &replaced = bodyAs<OrderReplacedEvent>(events[0]);
    EXPECT_EQ(replaced.old_order_id, 1U);
    EXPECT_EQ(replaced.new_order_id, 3U);
    EXPECT_EQ(replaced.old_price, 150);
    EXPECT_EQ(replaced.new_price, 150);
    EXPECT_EQ(replaced.old_quantity, 10U);
    EXPECT_EQ(replaced.new_quantity, 8U);
    EXPECT_EQ(replaced.side, Side::Buy);
}

TEST(MatchingEngineEvents, ReplaceKeepingSameIdIsAllowed) {
    OrderBook book = makeBook();
    EventWriter writer(STREAM_HEADER, BUFFER_BYTES);
    MatchingEngine engine(book, &writer);
    engine.submitLimit(makeOrder(1, 150, 10, Side::Buy));

    ASSERT_TRUE(engine.replace(OrderId(1), makeOrder(1, 148, 4, Side::Buy)).has_value());
    ASSERT_NE(book.find(OrderId(1)), nullptr);
    EXPECT_EQ(book.find(OrderId(1))->price_ticks, 148);
    EXPECT_EQ(book.size(), 1U);
}

TEST(MatchingEngineEvents, CrossingReplaceIsCancelThenSubmit) {
    OrderBook book = makeBook();
    EventWriter writer(STREAM_HEADER, BUFFER_BYTES);
    MatchingEngine engine(book, &writer);
    engine.submitLimit(makeOrder(1, 150, 4, Side::Sell));
    engine.submitLimit(makeOrder(2, 145, 10, Side::Buy));
    writer.clear();

    const std::optional<SubmitResult> result = engine.replace(OrderId(2), makeOrder(3, 150, 10, Side::Buy));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->trades.size(), 1U);
    EXPECT_EQ(result->remaining, Quantity(6));

    const std::vector<DecodedEvent> events = decodeAll(writer, 0);
    ASSERT_EQ(events.size(), 4U); // X E P A
    EXPECT_EQ(bodyAs<OrderCancelledEvent>(events[0]).order_id, 2U);
    EXPECT_EQ(bodyAs<OrderCancelledEvent>(events[0]).price, 145);
    EXPECT_EQ(bodyAs<OrderExecutedEvent>(events[1]).maker_order_id, 1U);
    EXPECT_EQ(bodyAs<OrderExecutedEvent>(events[1]).taker_order_id, 3U);
    EXPECT_EQ(bodyAs<TradeExecutedEvent>(events[2]).quantity, 4U);
    EXPECT_EQ(bodyAs<OrderAddedEvent>(events[3]).order_id, 3U);
    EXPECT_EQ(bodyAs<OrderAddedEvent>(events[3]).quantity, 6U);
}

TEST(MatchingEngineEvents, RejectedReplaceLeavesBookAndStreamUntouched) {
    OrderBook book = makeBook();
    EventWriter writer(STREAM_HEADER, BUFFER_BYTES);
    MatchingEngine engine(book, &writer);
    engine.submitLimit(makeOrder(1, 150, 10, Side::Buy));
    engine.submitLimit(makeOrder(2, 140, 10, Side::Buy));
    writer.clear();

    EXPECT_FALSE(engine.replace(OrderId(99), makeOrder(3, 150, 10, Side::Buy)).has_value()); // unknown
    EXPECT_FALSE(engine.replace(OrderId(1), makeOrder(3, 150, 10, Side::Sell)).has_value()); // side flip
    EXPECT_FALSE(engine.replace(OrderId(1), makeOrder(3, 300, 10, Side::Buy)).has_value());  // out of band
    EXPECT_FALSE(engine.replace(OrderId(1), makeOrder(2, 150, 10, Side::Buy)).has_value());  // id taken
    EXPECT_FALSE(engine.replace(OrderId(1), makeOrder(3, 150, 0, Side::Buy)).has_value());   // zero qty

    EXPECT_EQ(writer.size(), 0U);
    EXPECT_EQ(book.size(), 2U);
    ASSERT_NE(book.find(OrderId(1)), nullptr);
    EXPECT_EQ(book.find(OrderId(1))->quantity, 10U);
}

TEST(MatchingEngineEvents, RemainderRejectedFromBookEmitsNoOrderAdded) {
    OrderBook book = makeBook();
    EventWriter writer(STREAM_HEADER, BUFFER_BYTES);
    MatchingEngine engine(book, &writer);
    engine.submitLimit(makeOrder(1, 300, 10, Side::Buy)); // out of band: never rests
    EXPECT_EQ(writer.size(), EVENT_STREAM_HEADER_SIZE);
}

// Golden file: a fixed op sequence covering every message type must produce
// exactly these bytes. Any diff here is a wire-format change, which after the
// v1 freeze means a version bump -- not a golden-file refresh.
std::vector<char> goldenStream() {
    OrderBook book(Ticks(BAND_MIN), Ticks(BAND_MAX), CAPACITY);
    EventWriter writer(EventStreamHeader{EVENT_STREAM_VERSION, 1, BAND_MIN, BAND_MAX}, BUFFER_BYTES);
    MatchingEngine engine(book, &writer);

    writer.setTimestamp(1000);
    engine.submitLimit(makeOrder(1, 150, 10, Side::Buy));  // A
    engine.submitLimit(makeOrder(2, 150, 5, Side::Buy));   // A
    engine.submitLimit(makeOrder(3, 155, 20, Side::Sell)); // A
    writer.setTimestamp(2000);
    engine.submitLimit(makeOrder(4, 150, 12, Side::Sell)); // E P (1 filled) E P (2 partial)
    writer.setTimestamp(3000);
    engine.cancel(OrderId(3));                                    // X
    engine.replace(OrderId(2), makeOrder(5, 148, 8, Side::Buy));  // U
    engine.submitLimit(makeOrder(6, 152, 4, Side::Sell));         // A
    engine.replace(OrderId(6), makeOrder(6, 148, 6, Side::Sell)); // X E P (crossing replace)
    writer.setTimestamp(4000);
    engine.submitLimit(makeOrder(7, 160, 9, Side::Sell)); // A
    engine.submitLimit(makeOrder(8, 140, 3, Side::Buy));  // A
    writer.bookSnapshot(book);                            // S: bids 148x2, 140x3; asks 160x9

    EXPECT_FALSE(writer.overflowed());
    return {writer.data(), writer.data() + writer.size()};
}

TEST(EventStreamGolden, KnownOpSequenceIsByteIdentical) {
    const std::string golden_path = std::string(OME_GOLDEN_DIR) + "/event_stream_v1.bin";
    const std::vector<char> actual = goldenStream();

    std::ifstream in(golden_path, std::ios::binary);
    const std::vector<char> expected((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (actual != expected) {
        // Leave the actual bytes in the test's working directory for diffing.
        std::ofstream("event_stream_v1.bin.actual", std::ios::binary)
            .write(actual.data(), static_cast<std::streamsize>(actual.size()));
    }
    ASSERT_FALSE(expected.empty()) << "missing golden file " << golden_path;
    EXPECT_EQ(actual, expected) << "event stream differs from " << golden_path
                                << "; actual bytes written to event_stream_v1.bin.actual";
}

TEST(EventStreamGolden, GoldenStreamDecodesToExpectedMessageSequence) {
    const std::vector<char> bytes = goldenStream();
    EventStreamHeader header;
    ASSERT_TRUE(decodeStreamHeader(bytes.data(), bytes.size(), header));

    const std::string expected_types = "AAAEPEPXUAXEPAAS";
    std::string types;
    uint64_t expected_seq = 1;
    std::size_t offset = EVENT_STREAM_HEADER_SIZE;
    while (offset < bytes.size()) {
        DecodedEvent event;
        const std::size_t consumed = decodeEvent(bytes.data() + offset, bytes.size() - offset, event);
        ASSERT_NE(consumed, 0U);
        types.push_back(static_cast<char>(event.header.type));
        if (event.header.type == EventType::BookSnapshot) {
            EXPECT_EQ(event.header.seq, expected_seq - 1);
        } else {
            EXPECT_EQ(event.header.seq, expected_seq++);
        }
        offset += consumed;
    }
    EXPECT_EQ(types, expected_types);
}

} // namespace
