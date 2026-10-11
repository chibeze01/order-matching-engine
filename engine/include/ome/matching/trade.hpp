#ifndef ORDER_MATCHING_ENGINE_TRADE_HPP
#define ORDER_MATCHING_ENGINE_TRADE_HPP

#include "ome/types/order_id.hpp"
#include "ome/types/quantity.hpp"
#include "ome/types/side.hpp"
#include "ome/types/ticks.hpp"

// One execution produced by the matching engine. price is always the
// maker's (resting order's) price -- the taker crosses into it, never the
// other way round. aggressor_side is the taker's side.
struct Trade {
    OrderId maker_id;
    OrderId taker_id;
    Ticks price;
    Quantity quantity;
    Side aggressor_side;
};

#endif // ORDER_MATCHING_ENGINE_TRADE_HPP
