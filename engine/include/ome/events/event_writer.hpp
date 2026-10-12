#ifndef ORDER_MATCHING_ENGINE_EVENT_WRITER_HPP
#define ORDER_MATCHING_ENGINE_EVENT_WRITER_HPP

#include "ome/events/event_format.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

class OrderBook;

// Encodes events into a byte buffer preallocated at construction, stamping
// each delta with the next sequence number and the current timestamp. The
// buffer starts with the stream header. Emission never allocates, so it is
// safe on the engine's hot path. Single-writer, like the engine.
//
// The owner drains the buffer between engine calls -- copy data()/size() out
// to a log file or socket, then clear(). Sequence numbers carry on across
// clear(). Running out of room is a sizing bug, not back-pressure: the
// message that did not fit and everything after it is dropped, overflowed()
// latches true and stays true, and the stream must be treated as broken.
class EventWriter {
  public:
    // Throws std::invalid_argument if capacity_bytes cannot hold the stream
    // header.
    EventWriter(const EventStreamHeader &header, std::size_t capacity_bytes);

    // Stamped on every message written from now on. Caller-supplied so the
    // engine never reads a clock and replays stay byte-identical.
    void setTimestamp(uint64_t timestamp_) { timestamp = timestamp_; }

    void orderAdded(const OrderAddedEvent &event);
    // Writes the OrderExecuted followed by its TradeExecuted print, both
    // under a freshly assigned match id (match_id in event is ignored).
    void orderExecuted(const OrderExecutedEvent &event);
    void orderCancelled(const OrderCancelledEvent &event);
    void orderReplaced(const OrderReplacedEvent &event);

    // Price-level snapshot of book, bids best-first then asks best-first.
    // Does not consume a sequence number: carries lastSeq(), the last delta
    // it reflects. Not a hot-path call (walks every occupied tick from the
    // best quotes outward), but still writes into the preallocated buffer
    // without allocating. book must match the band in the stream header.
    void bookSnapshot(const OrderBook &book);

    const char *data() const { return buffer.data(); }
    std::size_t size() const { return used; }
    void clear() { used = 0; }
    bool overflowed() const { return overflow; }
    uint64_t lastSeq() const { return last_seq; }

  private:
    // Returns where to encode a size-byte message, or nullptr (and latches
    // overflow) if it does not fit.
    char *reserve(std::size_t size);

    EventStreamHeader header;
    std::vector<char> buffer; // sized once in ctor, never resized
    std::size_t used = 0;
    uint64_t last_seq = 0;
    uint64_t last_match_id = 0;
    uint64_t timestamp = 0;
    bool overflow = false;
};

#endif // ORDER_MATCHING_ENGINE_EVENT_WRITER_HPP
