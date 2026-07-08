#ifndef ORDER_MATCHING_ENGINE_ORDER_HPP
#define ORDER_MATCHING_ENGINE_ORDER_HPP

#include "order_id.hpp"
#include "order_type.hpp"
#include "price.hpp"
#include "quantity.hpp"
#include "side.hpp"

struct Order {
    OrderId id;
    Price price;
    Quantity quantity;
    Side side;
    OrderType type;
};

#endif // ORDER_MATCHING_ENGINE_ORDER_HPP
