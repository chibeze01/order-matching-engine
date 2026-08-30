#include "book_invariants.hpp"

#include <sstream>
#include <vector>

namespace {

std::string sideName(const Side side) { return side == Side::Buy ? "Buy" : "Sell"; }

std::string opKindName(const FuzzOpKind kind) { return kind == FuzzOpKind::Add ? "Add" : "Cancel"; }

std::string formatOptionalTicks(const std::optional<Ticks> &value) {
    return value.has_value() ? std::to_string(value->getValue()) : "none";
}

std::vector<uint64_t> realLevelIds(const OrderBook &book, const Side side, const Ticks price) {
    std::vector<uint64_t> ids;
    for (const OrderNode *node = book.levelFront(side, price); node != nullptr; node = node->next) {
        ids.push_back(node->order_id);
    }
    return ids;
}

InvariantFailure makeFailure(const std::ostringstream &message, const uint64_t op_index, const uint64_t seed) {
    return InvariantFailure{message.str(), op_index, seed};
}

// Compares one level between the real and naive books. Returns a failure
// describing the first mismatch found, or nullopt if the level agrees.
std::optional<InvariantFailure> checkLevel(const OrderBook &book, const NaiveOrderBook &naive, const Side side,
                                           const Ticks price, const uint64_t op_index, const uint64_t seed) {
    const Quantity real_quantity = book.levelQuantity(side, price);
    const Quantity naive_quantity = naive.levelQuantity(side, price);
    if (real_quantity != naive_quantity) {
        std::ostringstream message;
        message << "level quantity mismatch on " << sideName(side) << "@" << price.getValue()
                << ": real=" << real_quantity.getValue() << " naive=" << naive_quantity.getValue();
        return makeFailure(message, op_index, seed);
    }

    const std::size_t real_count = book.levelOrderCount(side, price);
    const std::size_t naive_count = naive.levelOrderCount(side, price);
    if (real_count != naive_count) {
        std::ostringstream message;
        message << "level order count mismatch on " << sideName(side) << "@" << price.getValue()
                << ": real=" << real_count << " naive=" << naive_count;
        return makeFailure(message, op_index, seed);
    }

    const std::vector<uint64_t> real_ids = realLevelIds(book, side, price);
    const std::vector<uint64_t> naive_ids = naive.levelOrderIds(side, price);
    if (real_ids != naive_ids) {
        std::ostringstream message;
        message << "FIFO order mismatch on " << sideName(side) << "@" << price.getValue() << ": real=[";
        for (const uint64_t id : real_ids) {
            message << id << ",";
        }
        message << "] naive=[";
        for (const uint64_t id : naive_ids) {
            message << id << ",";
        }
        message << "]";
        return makeFailure(message, op_index, seed);
    }

    return std::nullopt;
}

} // namespace

std::string describe(const InvariantFailure &failure) {
    std::ostringstream message;
    message << "op_index=" << failure.op_index << " seed=" << failure.seed << ": " << failure.what;
    return message.str();
}

std::optional<InvariantFailure> checkAfterOp(const OrderBook &book, const NaiveOrderBook &naive, const FuzzOp &op,
                                             const bool real_result, const bool naive_result,
                                             const std::optional<std::pair<Side, Ticks>> touched_level,
                                             const uint64_t op_index, const uint64_t seed) {
    if (real_result != naive_result) {
        std::ostringstream message;
        message << opKindName(op.kind) << " id=" << op.order_id << " result mismatch: real=" << real_result
                << " naive=" << naive_result;
        return makeFailure(message, op_index, seed);
    }

    if (book.size() != naive.size()) {
        std::ostringstream message;
        message << "resting count mismatch: real=" << book.size() << " naive=" << naive.size();
        return makeFailure(message, op_index, seed);
    }

    const std::optional<Ticks> real_bid = book.bestBid();
    const std::optional<Ticks> naive_bid = naive.bestBid();
    if (real_bid != naive_bid) {
        std::ostringstream message;
        message << "best bid mismatch: real=" << formatOptionalTicks(real_bid)
                << " naive=" << formatOptionalTicks(naive_bid);
        return makeFailure(message, op_index, seed);
    }

    const std::optional<Ticks> real_ask = book.bestAsk();
    const std::optional<Ticks> naive_ask = naive.bestAsk();
    if (real_ask != naive_ask) {
        std::ostringstream message;
        message << "best ask mismatch: real=" << formatOptionalTicks(real_ask)
                << " naive=" << formatOptionalTicks(naive_ask);
        return makeFailure(message, op_index, seed);
    }

    if (real_bid.has_value() && real_ask.has_value() && !(*real_bid < *real_ask)) {
        std::ostringstream message;
        message << "crossed book: best_bid=" << real_bid->getValue() << " best_ask=" << real_ask->getValue();
        return makeFailure(message, op_index, seed);
    }

    if (touched_level.has_value()) {
        return checkLevel(book, naive, touched_level->first, touched_level->second, op_index, seed);
    }

    return std::nullopt;
}

std::optional<InvariantFailure> checkFullSweep(const OrderBook &book, const NaiveOrderBook &naive,
                                               const uint64_t op_index, const uint64_t seed) {
    for (const Side side : {Side::Buy, Side::Sell}) {
        for (const Ticks price : naive.occupiedPrices(side)) {
            if (std::optional<InvariantFailure> failure = checkLevel(book, naive, side, price, op_index, seed)) {
                return failure;
            }
        }
    }
    return std::nullopt;
}
