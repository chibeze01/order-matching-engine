#include "ome/replay/replay_engine.hpp"

#include "ome/book/order_book.hpp"
#include "ome/replay/log_reader.hpp"
#include "ome/types/order.hpp"

namespace {

constexpr uint64_t FNV_OFFSET_BASIS = 0xCBF29CE484222325ULL;
constexpr uint64_t FNV_PRIME = 0x100000001B3ULL;

uint64_t fnv1aByte(uint64_t hash, const uint8_t byte) {
    hash ^= byte;
    hash *= FNV_PRIME;
    return hash;
}

uint64_t fnv1aU64(uint64_t hash, const uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        hash = fnv1aByte(hash, static_cast<uint8_t>((value >> (8 * i)) & 0xFF));
    }
    return hash;
}

// Folds one applied command's outcome into the running hash: the command
// itself, whether it succeeded, and the resulting state of the price level
// it touched. This makes the hash a fingerprint of actual book state rather
// than just a checksum of the input bytes.
uint64_t foldStep(uint64_t running, const Command &cmd, const bool success, const uint64_t level_qty,
                  const uint64_t level_count) {
    running = fnv1aU64(running, cmd.seq);
    running = fnv1aByte(running, static_cast<uint8_t>(cmd.type));
    running = fnv1aU64(running, cmd.order_id);
    running = fnv1aByte(running, static_cast<uint8_t>(cmd.side));
    running = fnv1aU64(running, static_cast<uint64_t>(cmd.price_ticks));
    running = fnv1aU64(running, cmd.quantity);
    running = fnv1aByte(running, success ? 1 : 0);
    running = fnv1aU64(running, level_qty);
    running = fnv1aU64(running, level_count);
    return running;
}

uint64_t applyAndFold(OrderBook &book, const Command &cmd, const uint64_t running_hash) {
    bool success = false;
    uint64_t level_qty = 0;
    uint64_t level_count = 0;

    switch (cmd.type) {
    case CommandType::Add: {
        const Order order{OrderId(cmd.order_id), Price(Ticks(cmd.price_ticks), TickSize(0)), Quantity(cmd.quantity),
                          cmd.side, cmd.order_type};
        success = book.insert(order) != nullptr;
        break;
    }
    case CommandType::Cancel: {
        success = book.cancel(OrderId(cmd.order_id));
        break;
    }
    case CommandType::Modify: {
        if (book.cancel(OrderId(cmd.order_id))) {
            const Order order{OrderId(cmd.order_id), Price(Ticks(cmd.price_ticks), TickSize(0)), Quantity(cmd.quantity),
                              cmd.side, OrderType::Limit};
            success = book.insert(order) != nullptr;
        }
        break;
    }
    }

    if (success) {
        level_qty = book.levelQuantity(cmd.side, Ticks(cmd.price_ticks)).getValue();
        level_count = book.levelOrderCount(cmd.side, Ticks(cmd.price_ticks));
    }
    return foldStep(running_hash, cmd, success, level_qty, level_count);
}

OrderBook makeBookFromHeader(const LogHeader &header) {
    return OrderBook(Ticks(header.min_tick), Ticks(header.max_tick), static_cast<std::size_t>(header.capacity));
}

} // namespace

ReplayResult replayLog(const std::string &base_path) {
    LogReader reader(base_path);
    OrderBook book = makeBookFromHeader(reader.header());

    uint64_t running_hash = FNV_OFFSET_BASIS;
    uint64_t count = 0;
    Command cmd;
    while (reader.next(cmd)) {
        running_hash = applyAndFold(book, cmd, running_hash);
        ++count;
    }
    return ReplayResult{count, running_hash};
}

CompareResult compareLogs(const std::string &path_a, const std::string &path_b) {
    LogReader reader_a(path_a);
    LogReader reader_b(path_b);
    OrderBook book_a = makeBookFromHeader(reader_a.header());
    OrderBook book_b = makeBookFromHeader(reader_b.header());

    uint64_t hash_a = FNV_OFFSET_BASIS;
    uint64_t hash_b = FNV_OFFSET_BASIS;

    CompareResult result;
    Command cmd_a;
    Command cmd_b;
    while (true) {
        const bool has_a = reader_a.next(cmd_a);
        const bool has_b = reader_b.next(cmd_b);
        if (!has_a && !has_b) {
            break;
        }
        if (has_a != has_b) {
            result.diverged = true;
            result.first_diverging_seq = has_a ? cmd_a.seq : cmd_b.seq;
            break;
        }
        hash_a = applyAndFold(book_a, cmd_a, hash_a);
        hash_b = applyAndFold(book_b, cmd_b, hash_b);
        if (hash_a != hash_b) {
            result.diverged = true;
            result.first_diverging_seq = cmd_a.seq;
            break;
        }
    }
    result.final_hash_a = hash_a;
    result.final_hash_b = hash_b;
    return result;
}
