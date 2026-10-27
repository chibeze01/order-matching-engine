#include "matching_engine.hpp"

#include <utility>

bool MatchingEngine::crosses(const Side taker_side, const Ticks taker_price, const Ticks resting_price) {
    return taker_side == Side::Buy ? taker_price.getValue() >= resting_price.getValue()
                                   : taker_price.getValue() <= resting_price.getValue();
}

Side MatchingEngine::restingSide(const Side taker_side) { return taker_side == Side::Buy ? Side::Sell : Side::Buy; }

bool MatchingEngine::crossesBook(const Side taker_side, const Ticks taker_price) const {
    const std::optional<Ticks> best = taker_side == Side::Buy ? book.bestAsk() : book.bestBid();
    return best.has_value() && crosses(taker_side, taker_price, *best);
}

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
        if (events != nullptr) {
            events->orderExecuted(OrderExecutedEvent{fill.order_id.getValue(), order.id.getValue(), 0,
                                                     fill.price.getValue(), fill.filled.getValue(),
                                                     fill.remaining.getValue(), taker_side});
        }
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
        if (resting != nullptr && events != nullptr) {
            events->orderAdded(
                OrderAddedEvent{resting->order_id, resting->price_ticks, resting->quantity, resting->side});
        }
    }
    return SubmitResult{std::move(trades), resting, Quantity(remaining), status};
}

bool MatchingEngine::cancel(const OrderId id) {
    OrderNode *node = book.find(id);
    if (node == nullptr) {
        return false;
    }
    const OrderCancelledEvent cancelled{node->order_id, node->price_ticks, node->quantity, node->side};
    book.remove(node);
    if (events != nullptr) {
        events->orderCancelled(cancelled);
    }
    return true;
}

std::optional<SubmitResult> MatchingEngine::replace(const OrderId old_id, const Order &order) {
    OrderNode *old_node = book.find(old_id);
    if (old_node == nullptr || old_node->side != order.side || !book.inBand(order.price.getTicks()) ||
        order.quantity.getValue() == 0) {
        return std::nullopt;
    }
    if (order.id != old_id && book.find(order.id) != nullptr) {
        return std::nullopt;
    }

    if (crossesBook(order.side, order.price.getTicks())) {
        cancel(old_id);
        return submitLimit(order);
    }

    // Validated above: once the old order is out, the insert cannot fail --
    // the price is in band, the id is free, and removing the old order just
    // returned a node to the pool.
    const OrderReplacedEvent replaced{
        old_node->order_id, order.id.getValue(),       old_node->price_ticks, order.price.getTicks().getValue(),
        old_node->quantity, order.quantity.getValue(), old_node->side};
    book.remove(old_node);
    OrderNode *resting = book.insert(order);
    if (events != nullptr) {
        events->orderReplaced(replaced);
    }
    // Non-crossing replace rests the whole order directly; nothing traded.
    return SubmitResult{{}, resting, order.quantity, SubmitStatus::Rested};
}
