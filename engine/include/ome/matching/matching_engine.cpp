#include "matching_engine.hpp"

#include <utility>

bool MatchingEngine::crosses(const Side taker_side, const Ticks taker_price, const Ticks resting_price) {
    return taker_side == Side::Buy ? taker_price.getValue() >= resting_price.getValue()
                                   : taker_price.getValue() <= resting_price.getValue();
}

bool MatchingEngine::crossesBook(const Side taker_side, const Ticks taker_price) const {
    const std::optional<Ticks> best = taker_side == Side::Buy ? book.bestAsk() : book.bestBid();
    return best.has_value() && crosses(taker_side, taker_price, *best);
}

SubmitResult MatchingEngine::submitLimit(const Order &order) {
    const Side taker_side = order.side;
    const Side resting_side = taker_side == Side::Buy ? Side::Sell : Side::Buy;
    const Ticks taker_price = order.price.getTicks();

    std::vector<Trade> trades;
    uint64_t remaining = order.quantity.getValue();

    while (remaining > 0 && crossesBook(taker_side, taker_price)) {
        const ConsumeResult fill = book.consumeBestFront(resting_side, Quantity(remaining));
        trades.push_back(Trade{fill.order_id, order.id, fill.price, fill.filled, taker_side});
        remaining -= fill.filled.getValue();
        if (events != nullptr) {
            events->orderExecuted(OrderExecutedEvent{fill.order_id.getValue(), order.id.getValue(), 0,
                                                     fill.price.getValue(), fill.filled.getValue(),
                                                     fill.remaining.getValue(), taker_side});
        }
    }

    // Quantity's const member has no assignment operator (see quantity.hpp),
    // so the result is built once here rather than mutated field-by-field.
    OrderNode *resting = nullptr;
    if (remaining > 0) {
        resting = book.insert(Order{order.id, order.price, Quantity(remaining), order.side, order.type});
        if (resting != nullptr && events != nullptr) {
            events->orderAdded(
                OrderAddedEvent{resting->order_id, resting->price_ticks, resting->quantity, resting->side});
        }
    }
    return SubmitResult{std::move(trades), resting, Quantity(remaining)};
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
    return SubmitResult{{}, resting, order.quantity};
}
