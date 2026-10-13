#include "matching_engine.hpp"

#include <utility>

bool MatchingEngine::crosses(const Side taker_side, const Ticks taker_price, const Ticks resting_price) {
    return taker_side == Side::Buy ? taker_price.getValue() >= resting_price.getValue()
                                   : taker_price.getValue() <= resting_price.getValue();
}

Side MatchingEngine::restingSide(const Side taker_side) { return taker_side == Side::Buy ? Side::Sell : Side::Buy; }

uint64_t MatchingEngine::match(const Order &order, const std::optional<Ticks> limit, std::vector<Trade> &trades) {
    const Side taker_side = order.side;
    const Side resting_side = restingSide(taker_side);

    uint64_t remaining = order.quantity.getValue();
    while (remaining > 0) {
        const std::optional<Ticks> best = resting_side == Side::Buy ? book.bestBid() : book.bestAsk();
        if (!best.has_value() || (limit.has_value() && !crosses(taker_side, *limit, *best))) {
            break;
        }

        const ConsumeResult fill = book.consumeBestFront(resting_side, Quantity(remaining));
        trades.push_back(Trade{fill.order_id, order.id, fill.price, fill.filled, taker_side});
        remaining -= fill.filled.getValue();
    }
    return remaining;
}

SubmitResult MatchingEngine::submit(const Order &order) {
    const Ticks limit = order.price.getTicks();
    std::vector<Trade> trades;
    uint64_t remaining = 0;

    switch (order.type) {
    case OrderType::Limit:
        return submitLimit(order);
    case OrderType::Market: // no price limit: walks the far side until filled or empty
        remaining = match(order, std::nullopt, trades);
        break;
    case OrderType::IOC:
        remaining = match(order, limit, trades);
        break;
    case OrderType::FOK:
        // Pre-check, not rollback: a kill must leave the book untouched, so
        // nothing is consumed unless the whole quantity is known to cross.
        if (book.crossingQuantity(restingSide(order.side), limit, order.quantity) < order.quantity) {
            return SubmitResult{{}, nullptr, order.quantity, SubmitStatus::Cancelled};
        }
        remaining = match(order, limit, trades);
        break;
    }
    // Market, IOC and FOK never rest: any remainder is cancelled here.
    const SubmitStatus status = remaining == 0 ? SubmitStatus::Filled : SubmitStatus::Cancelled;
    return SubmitResult{std::move(trades), nullptr, Quantity(remaining), status};
}

SubmitResult MatchingEngine::submitLimit(const Order &order) {
    std::vector<Trade> trades;
    const uint64_t remaining = match(order, order.price.getTicks(), trades);

    // Quantity's const member has no assignment operator (see quantity.hpp),
    // so the result is built once here rather than mutated field-by-field.
    OrderNode *resting = nullptr;
    SubmitStatus status = SubmitStatus::Filled;
    if (remaining > 0) {
        resting = book.insert(Order{order.id, order.price, Quantity(remaining), order.side, order.type});
        status = resting != nullptr ? SubmitStatus::Rested : SubmitStatus::RestRejected;
    }
    return SubmitResult{std::move(trades), resting, Quantity(remaining), status};
}
