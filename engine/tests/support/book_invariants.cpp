#include "book_invariants.hpp"

#include <sstream>
#include <vector>

namespace {

std::string sideName(const Side side) { return side == Side::Buy ? "Buy" : "Sell"; }

std::string opKindName(const FuzzOpKind kind) { return kind == FuzzOpKind::Add ? "Add" : "Cancel"; }

std::string formatOptionalTicks(const std::optional<Ticks> &value) {
    return value.has_value() ? std::to_string(value->getValue()) : "none";
}

std::string levelLabel(const std::string &field, const Side side, const Ticks price) {
    std::ostringstream label;
    label << field << " on " << sideName(side) << "@" << price.getValue();
    return label.str();
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

// Shared shape behind every simple real-vs-naive equality check below: same
// message format, only the label and the two values change.
template <typename T>
std::optional<InvariantFailure> compareEqual(const std::string &label, const T &real_value, const T &naive_value,
                                             const uint64_t op_index, const uint64_t seed) {
    if (real_value == naive_value) {
        return std::nullopt;
    }
    std::ostringstream message;
    message << label << " mismatch: real=" << real_value << " naive=" << naive_value;
    return makeFailure(message, op_index, seed);
}

// Compares one level between the real and naive books. Returns a failure
// describing the first mismatch found, or nullopt if the level agrees.
std::optional<InvariantFailure> checkLevel(const OrderBook &book, const NaiveOrderBook &naive, const Side side,
                                           const Ticks price, const uint64_t op_index, const uint64_t seed) {
    if (std::optional<InvariantFailure> failure =
            compareEqual(levelLabel("level quantity", side, price), book.levelQuantity(side, price).getValue(),
                         naive.levelQuantity(side, price).getValue(), op_index, seed)) {
        return failure;
    }

    if (std::optional<InvariantFailure> failure =
            compareEqual(levelLabel("level order count", side, price), book.levelOrderCount(side, price),
                         naive.levelOrderCount(side, price), op_index, seed)) {
        return failure;
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
    const std::string op_label = opKindName(op.kind) + " id=" + std::to_string(op.order_id) + " result";
    if (std::optional<InvariantFailure> failure = compareEqual(op_label, real_result, naive_result, op_index, seed)) {
        return failure;
    }

    if (std::optional<InvariantFailure> failure =
            compareEqual(std::string("resting count"), book.size(), naive.size(), op_index, seed)) {
        return failure;
    }

    const std::optional<Ticks> real_bid = book.bestBid();
    const std::optional<Ticks> naive_bid = naive.bestBid();
    if (std::optional<InvariantFailure> failure = compareEqual(std::string("best bid"), formatOptionalTicks(real_bid),
                                                               formatOptionalTicks(naive_bid), op_index, seed)) {
        return failure;
    }

    const std::optional<Ticks> real_ask = book.bestAsk();
    const std::optional<Ticks> naive_ask = naive.bestAsk();
    if (std::optional<InvariantFailure> failure = compareEqual(std::string("best ask"), formatOptionalTicks(real_ask),
                                                               formatOptionalTicks(naive_ask), op_index, seed)) {
        return failure;
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
                                               const int64_t band_min, const int64_t band_max, const uint64_t op_index,
                                               const uint64_t seed) {
    for (const Side side : {Side::Buy, Side::Sell}) {
        for (int64_t ticks = band_min; ticks <= band_max; ++ticks) {
            if (std::optional<InvariantFailure> failure = checkLevel(book, naive, side, Ticks(ticks), op_index, seed)) {
                return failure;
            }
        }
    }
    return std::nullopt;
}
