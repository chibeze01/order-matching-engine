#include "ome/replay/replay_engine.hpp"

#include "ome/book/order_book.hpp"
#include "ome/replay/log_reader.hpp"
#include "ome/types/order.hpp"

#include <optional>
#include <stdexcept>

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

uint64_t fnv1aOptionalTicks(uint64_t hash, const std::optional<Ticks> &ticks) {
    hash = fnv1aByte(hash, ticks.has_value() ? 1 : 0);
    return fnv1aU64(hash, ticks.has_value() ? static_cast<uint64_t>(ticks->getValue()) : 0);
}

// Folds one applied command's outcome into the running hash: the command,
// whether it succeeded, the touched price level, and the book-wide state
// (best quotes and resting count). The book-wide part matters -- without it
// a bug that corrupts a level nobody touched this step, or that breaks the
// incrementally maintained best-bid/best-ask indices, would stay invisible
// until some later command happened to land on the damaged level.
uint64_t foldStep(uint64_t running, const Command &cmd, const OrderBook &book, const bool success,
                  const uint64_t level_qty, const uint64_t level_count) {
    running = fnv1aU64(running, cmd.seq);
    running = fnv1aByte(running, static_cast<uint8_t>(cmd.type));
    running = fnv1aU64(running, cmd.order_id);
    running = fnv1aByte(running, static_cast<uint8_t>(cmd.side));
    running = fnv1aU64(running, static_cast<uint64_t>(cmd.price_ticks));
    running = fnv1aU64(running, cmd.quantity);
    running = fnv1aByte(running, success ? 1 : 0);
    running = fnv1aU64(running, level_qty);
    running = fnv1aU64(running, level_count);
    running = fnv1aOptionalTicks(running, book.bestBid());
    running = fnv1aOptionalTicks(running, book.bestAsk());
    running = fnv1aU64(running, static_cast<uint64_t>(book.size()));
    return running;
}

uint64_t applyAndFold(OrderBook &book, const LogHeader &header, const Command &cmd, const uint64_t running_hash) {
    const bool in_band = cmd.price_ticks >= header.min_tick && cmd.price_ticks <= header.max_tick;
    bool success = false;

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
        // Check the target price before cancelling. Once the order is out of
        // the book the re-insert can only fail on an out-of-band price (the
        // id has just been freed from the cancel index, and cancelling
        // returned a node to the pool), so validating the band up front is
        // enough to guarantee the re-insert succeeds. Cancelling first and
        // discovering the failure afterwards would drop the order entirely --
        // resting at neither the old price nor the new one.
        if (in_band && book.cancel(OrderId(cmd.order_id))) {
            const Order order{OrderId(cmd.order_id), Price(Ticks(cmd.price_ticks), TickSize(0)), Quantity(cmd.quantity),
                              cmd.side, OrderType::Limit};
            success = book.insert(order) != nullptr;
        }
        break;
    }
    }

    uint64_t level_qty = 0;
    uint64_t level_count = 0;
    if (success) {
        level_qty = book.levelQuantity(cmd.side, Ticks(cmd.price_ticks)).getValue();
        level_count = book.levelOrderCount(cmd.side, Ticks(cmd.price_ticks));
    }
    return foldStep(running_hash, cmd, book, success, level_qty, level_count);
}

OrderBook makeBookFromHeader(const LogHeader &header) {
    return OrderBook(Ticks(header.min_tick), Ticks(header.max_tick), static_cast<std::size_t>(header.capacity));
}

bool sameRunConfig(const LogHeader &a, const LogHeader &b) {
    return a.version == b.version && a.tick_size == b.tick_size && a.min_tick == b.min_tick &&
           a.max_tick == b.max_tick && a.capacity == b.capacity;
}

} // namespace

ReplayResult replayLog(const std::string &base_path) {
    LogReader reader(base_path);
    OrderBook book = makeBookFromHeader(reader.header());

    uint64_t running_hash = FNV_OFFSET_BASIS;
    uint64_t count = 0;
    Command cmd;
    while (reader.next(cmd)) {
        running_hash = applyAndFold(book, reader.header(), cmd, running_hash);
        ++count;
    }

    ReplayResult result;
    result.command_count = count;
    result.final_hash = running_hash;
    result.resting_count = book.size();
    if (const std::optional<Ticks> bid = book.bestBid(); bid.has_value()) {
        result.best_bid_ticks = bid->getValue();
    }
    if (const std::optional<Ticks> ask = book.bestAsk(); ask.has_value()) {
        result.best_ask_ticks = ask->getValue();
    }
    return result;
}

CompareResult compareLogs(const std::string &path_a, const std::string &path_b) {
    LogReader reader_a(path_a);
    LogReader reader_b(path_b);
    // Comparing runs built on different bands or capacities would produce a
    // divergence report that blames a sequence number when the real answer is
    // that the two logs were never comparable. The seed is deliberately not
    // checked: comparing two different seeds is a legitimate use.
    if (!sameRunConfig(reader_a.header(), reader_b.header())) {
        throw std::runtime_error("compareLogs: logs were recorded with different book configurations");
    }
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
        hash_a = applyAndFold(book_a, reader_a.header(), cmd_a, hash_a);
        hash_b = applyAndFold(book_b, reader_b.header(), cmd_b, hash_b);
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
