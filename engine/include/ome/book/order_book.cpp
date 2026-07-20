#include "order_book.hpp"

#include <bit>
#include <stdexcept>

OrderBook::OrderBook(const Ticks min_tick_, const Ticks max_tick_, const std::size_t capacity_)
    : min_tick(min_tick_.getValue()), max_tick(max_tick_.getValue()) {
    if (min_tick < 0) {
        throw std::invalid_argument("Band minimum must be non-negative");
    }
    if (min_tick > max_tick) {
        throw std::invalid_argument("Band minimum must not exceed band maximum");
    }
    if (capacity_ == 0) {
        throw std::invalid_argument("Capacity must be positive");
    }

    const std::size_t width = static_cast<std::size_t>(max_tick - min_tick) + 1;
    bids.resize(width);
    asks.resize(width);

    node_storage.resize(capacity_);
    for (std::size_t i = 0; i + 1 < capacity_; ++i) {
        node_storage[i].next = &node_storage[i + 1];
    }
    free_head = &node_storage[0];

    const std::size_t table_size = std::bit_ceil(capacity_ * 2);
    handle_map.resize(table_size);
    handle_mask = table_size - 1;
    handle_shift = 64U - static_cast<unsigned>(std::countr_zero(table_size));
}

OrderNode *OrderBook::insert(const Order &order) {
    const int64_t ticks = order.price.getTicks().getValue();
    if (!inBand(ticks)) { // reject before allocating so out-of-band inserts never consume a node
        return nullptr;
    }
    if (handleFind(order.id.getValue()) != NOT_FOUND) { // duplicate id would corrupt the cancel index
        return nullptr;
    }
    OrderNode *node = allocateNode();
    if (node == nullptr) {
        return nullptr;
    }

    node->order_id = order.id.getValue();
    node->price_ticks = ticks;
    node->quantity = order.quantity.getValue();
    node->side = order.side;
    node->prev = nullptr;
    node->next = nullptr;

    const std::size_t index = indexOf(ticks);
    Level &level = ladder(order.side)[index];
    node->prev = level.tail;
    if (level.tail != nullptr) {
        level.tail->next = node;
    } else {
        level.head = node;
    }
    level.tail = node;
    level.total_quantity += node->quantity;
    ++level.order_count;
    ++resting_count;

    if (order.side == Side::Buy) {
        if (best_bid_index == NO_LEVEL || index > best_bid_index) {
            best_bid_index = index;
        }
    } else {
        if (best_ask_index == NO_LEVEL || index < best_ask_index) {
            best_ask_index = index;
        }
    }
    handleInsert(node->order_id, node);
    return node;
}

void OrderBook::remove(OrderNode *handle) {
    handleErase(handleFind(handle->order_id));
    unlinkAndRelease(handle);
}

bool OrderBook::cancel(const OrderId id) {
    const std::size_t slot = handleFind(id.getValue());
    if (slot == NOT_FOUND) {
        return false;
    }
    OrderNode *node = handle_map[slot].node;
    handleErase(slot);
    unlinkAndRelease(node);
    return true;
}

void OrderBook::unlinkAndRelease(OrderNode *handle) {
    const std::size_t index = indexOf(handle->price_ticks);
    Level &level = ladder(handle->side)[index];

    if (handle->prev != nullptr) {
        handle->prev->next = handle->next;
    } else {
        level.head = handle->next;
    }
    if (handle->next != nullptr) {
        handle->next->prev = handle->prev;
    } else {
        level.tail = handle->prev;
    }
    level.total_quantity -= handle->quantity;
    --level.order_count;
    --resting_count;

    if (level.head == nullptr) {
        if (handle->side == Side::Buy && index == best_bid_index) {
            refreshBestAfterEmpty(Side::Buy, index);
        } else if (handle->side == Side::Sell && index == best_ask_index) {
            refreshBestAfterEmpty(Side::Sell, index);
        }
    }
    releaseNode(handle);
}

std::optional<Ticks> OrderBook::bestBid() const {
    if (best_bid_index == NO_LEVEL) {
        return std::nullopt;
    }
    return Ticks(min_tick + static_cast<int64_t>(best_bid_index));
}

std::optional<Ticks> OrderBook::bestAsk() const {
    if (best_ask_index == NO_LEVEL) {
        return std::nullopt;
    }
    return Ticks(min_tick + static_cast<int64_t>(best_ask_index));
}

Quantity OrderBook::levelQuantity(const Side side, const Ticks price) const {
    const Level *level = levelAt(side, price);
    return Quantity(level != nullptr ? level->total_quantity : 0);
}

std::size_t OrderBook::levelOrderCount(const Side side, const Ticks price) const {
    const Level *level = levelAt(side, price);
    return level != nullptr ? level->order_count : 0;
}

const OrderNode *OrderBook::levelFront(const Side side, const Ticks price) const {
    const Level *level = levelAt(side, price);
    return level != nullptr ? level->head : nullptr;
}

const OrderBook::Level *OrderBook::levelAt(const Side side, const Ticks price) const {
    const int64_t ticks = price.getValue();
    if (!inBand(ticks)) {
        return nullptr;
    }
    return &ladder(side)[indexOf(ticks)];
}

OrderNode *OrderBook::allocateNode() {
    if (free_head == nullptr) {
        return nullptr;
    }
    OrderNode *node = free_head;
    free_head = node->next;
    return node;
}

void OrderBook::releaseNode(OrderNode *node) {
    node->next = free_head;
    free_head = node;
}

std::size_t OrderBook::handleHome(const uint64_t order_id) const {
    // Fibonacci hashing: multiply spreads ids, top bits index the table.
    return static_cast<std::size_t>((order_id * 0x9E3779B97F4A7C15ULL) >> handle_shift);
}

std::size_t OrderBook::handleFind(const uint64_t order_id) const {
    std::size_t probe = handleHome(order_id);
    while (handle_map[probe].node != nullptr) {
        if (handle_map[probe].order_id == order_id) {
            return probe;
        }
        probe = (probe + 1) & handle_mask;
    }
    return NOT_FOUND;
}

void OrderBook::handleInsert(const uint64_t order_id, OrderNode *node) {
    std::size_t probe = handleHome(order_id);
    while (handle_map[probe].node != nullptr) {
        probe = (probe + 1) & handle_mask;
    }
    handle_map[probe].order_id = order_id;
    handle_map[probe].node = node;
}

void OrderBook::handleErase(const std::size_t index) {
    // Backward-shift deletion: keeps probe chains contiguous without
    // tombstones, so lookups never degrade over long insert/cancel churn.
    handle_map[index].node = nullptr;
    std::size_t hole = index;
    std::size_t probe = (index + 1) & handle_mask;
    while (handle_map[probe].node != nullptr) {
        const std::size_t home = handleHome(handle_map[probe].order_id);
        if (((probe - home) & handle_mask) >= ((probe - hole) & handle_mask)) {
            handle_map[hole] = handle_map[probe];
            handle_map[probe].node = nullptr;
            hole = probe;
        }
        probe = (probe + 1) & handle_mask;
    }
}

void OrderBook::refreshBestAfterEmpty(const Side side, const std::size_t emptied_index) {
    if (side == Side::Buy) {
        for (std::size_t i = emptied_index; i-- > 0;) { // downward scan, underflow-safe
            if (bids[i].order_count > 0) {
                best_bid_index = i;
                return;
            }
        }
        best_bid_index = NO_LEVEL;
    } else {
        for (std::size_t i = emptied_index + 1; i < asks.size(); ++i) {
            if (asks[i].order_count > 0) {
                best_ask_index = i;
                return;
            }
        }
        best_ask_index = NO_LEVEL;
    }
}
