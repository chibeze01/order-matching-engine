#include "naive_order_book.hpp"

NaiveOrderBook::NaiveOrderBook(const Ticks min_tick_, const Ticks max_tick_, const std::size_t capacity_)
    : min_tick(min_tick_.getValue()), max_tick(max_tick_.getValue()), book_capacity(capacity_) {}

bool NaiveOrderBook::insert(const Order &order) {
    const int64_t ticks = order.price.getTicks().getValue();
    const uint64_t id = order.id.getValue();
    if (!inBand(ticks)) {
        return false;
    }
    if (id_location.contains(id)) {
        return false;
    }
    if (resting_count >= book_capacity) {
        return false;
    }

    ladder(order.side)[ticks].push_back(Resting{id, order.quantity.getValue()});
    id_location.emplace(id, std::make_pair(order.side, ticks));
    ++resting_count;
    return true;
}

bool NaiveOrderBook::cancel(const OrderId id) {
    const auto location_it = id_location.find(id.getValue());
    if (location_it == id_location.end()) {
        return false;
    }
    const auto [side, ticks] = location_it->second;
    Ladder &levels = ladder(side);
    const auto level_it = levels.find(ticks);
    std::deque<Resting> &orders = level_it->second;
    for (auto order_it = orders.begin(); order_it != orders.end(); ++order_it) {
        if (order_it->order_id == id.getValue()) {
            orders.erase(order_it);
            break;
        }
    }
    if (orders.empty()) {
        levels.erase(level_it);
    }
    id_location.erase(location_it);
    --resting_count;
    return true;
}

std::optional<Ticks> NaiveOrderBook::bestBid() const {
    if (bids.empty()) {
        return std::nullopt;
    }
    return Ticks(bids.rbegin()->first);
}

std::optional<Ticks> NaiveOrderBook::bestAsk() const {
    if (asks.empty()) {
        return std::nullopt;
    }
    return Ticks(asks.begin()->first);
}

Quantity NaiveOrderBook::levelQuantity(const Side side, const Ticks price) const {
    const Ladder &levels = ladder(side);
    const auto it = levels.find(price.getValue());
    if (it == levels.end()) {
        return Quantity(0);
    }
    uint64_t total = 0;
    for (const Resting &resting : it->second) {
        total += resting.quantity;
    }
    return Quantity(total);
}

std::size_t NaiveOrderBook::levelOrderCount(const Side side, const Ticks price) const {
    const Ladder &levels = ladder(side);
    const auto it = levels.find(price.getValue());
    return it == levels.end() ? 0 : it->second.size();
}

std::vector<uint64_t> NaiveOrderBook::levelOrderIds(const Side side, const Ticks price) const {
    std::vector<uint64_t> ids;
    const Ladder &levels = ladder(side);
    const auto it = levels.find(price.getValue());
    if (it == levels.end()) {
        return ids;
    }
    ids.reserve(it->second.size());
    for (const Resting &resting : it->second) {
        ids.push_back(resting.order_id);
    }
    return ids;
}

std::vector<Ticks> NaiveOrderBook::occupiedPrices(const Side side) const {
    std::vector<Ticks> prices;
    const Ladder &levels = ladder(side);
    prices.reserve(levels.size());
    for (const auto &[price, orders] : levels) {
        prices.push_back(Ticks(price));
    }
    return prices;
}

std::optional<std::pair<Side, Ticks>> NaiveOrderBook::locate(const OrderId id) const {
    const auto it = id_location.find(id.getValue());
    if (it == id_location.end()) {
        return std::nullopt;
    }
    return std::make_pair(it->second.first, Ticks(it->second.second));
}
