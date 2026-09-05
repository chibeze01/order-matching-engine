#include "naive_matcher.hpp"

#include <algorithm>

std::vector<Trade> NaiveMatcher::submitLimit(const Order &order) {
    std::vector<Trade> trades;
    uint64_t remaining = order.quantity.getValue();
    const int64_t taker_price = order.price.getTicks().getValue();

    if (order.side == Side::Buy) {
        while (remaining > 0 && !asks_.empty()) {
            auto level_it = asks_.begin();
            if (level_it->first > taker_price) {
                break;
            }
            auto &queue = level_it->second;
            auto &front = queue.front();
            const uint64_t filled = std::min(remaining, front.quantity);
            trades.push_back(
                Trade{OrderId(front.order_id), order.id, Ticks(level_it->first), Quantity(filled), Side::Buy});
            remaining -= filled;
            front.quantity -= filled;
            if (front.quantity == 0) {
                queue.pop_front();
            }
            if (queue.empty()) {
                asks_.erase(level_it);
            }
        }
        if (remaining > 0) {
            bids_[taker_price].push_back(Resting{order.id.getValue(), remaining});
        }
    } else {
        while (remaining > 0 && !bids_.empty()) {
            auto level_it = bids_.begin();
            if (level_it->first < taker_price) {
                break;
            }
            auto &queue = level_it->second;
            auto &front = queue.front();
            const uint64_t filled = std::min(remaining, front.quantity);
            trades.push_back(
                Trade{OrderId(front.order_id), order.id, Ticks(level_it->first), Quantity(filled), Side::Sell});
            remaining -= filled;
            front.quantity -= filled;
            if (front.quantity == 0) {
                queue.pop_front();
            }
            if (queue.empty()) {
                bids_.erase(level_it);
            }
        }
        if (remaining > 0) {
            asks_[taker_price].push_back(Resting{order.id.getValue(), remaining});
        }
    }
    return trades;
}
