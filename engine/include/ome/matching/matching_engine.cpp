#include "matching_engine.hpp"

#include <utility>

bool MatchingEngine::crosses(const Side taker_side, const Ticks taker_price, const Ticks resting_price) {
    return taker_side == Side::Buy ? taker_price.getValue() >= resting_price.getValue()
                                   : taker_price.getValue() <= resting_price.getValue();
}

SubmitResult MatchingEngine::submitLimit(const Order &order) {
    const Side taker_side = order.side;
    const Side resting_side = taker_side == Side::Buy ? Side::Sell : Side::Buy;
    const Ticks taker_price = order.price.getTicks();

    std::vector<Trade> trades;
    uint64_t remaining = order.quantity.getValue();

    while (remaining > 0) {
        const std::optional<Ticks> best = resting_side == Side::Buy ? book.bestBid() : book.bestAsk();
        if (!best.has_value() || !crosses(taker_side, taker_price, *best)) {
            break;
        }

        const ConsumeResult fill = book.consumeBestFront(resting_side, Quantity(remaining));
        trades.push_back(Trade{fill.order_id, order.id, fill.price, fill.filled, taker_side});
        remaining -= fill.filled.getValue();
    }

    // Quantity's const member has no assignment operator (see quantity.hpp),
    // so the result is built once here rather than mutated field-by-field.
    OrderNode *resting = nullptr;
    if (remaining > 0) {
        resting = book.insert(Order{order.id, order.price, Quantity(remaining), order.side, order.type});
    }
    return SubmitResult{std::move(trades), resting, Quantity(remaining)};
}
