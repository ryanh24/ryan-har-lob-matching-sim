#pragma once
#include "lob/types.hpp"
#include <map>
#include "lob/order.hpp"
#include "lob/limit.hpp"
#include "lob/pool.hpp"
#include "lob/order_index.hpp"

// Storage only: two per-side AVL trees of Limit levels, cached best-bid/ask pointers,
// the Order/Limit pools, and the orderID index. Exposes primitives the Engine calls.
// Hot primitives (best_bid/ask, pop-front, decrement) are inline HERE so the
// storage/matching boundary is free on the hot path (see README architecture decision).
// TODO(tracer-bullet): class Book with insert_order / remove_order / best_bid / best_ask
//                      / decrement / delete_level.

namespace lob {
    class Book {
        private:
            std::map<Price, Limit> asks_;
            std::map<Price, Limit> bids_;
            OrderIndex index_;                       // hand-rolled open-addressing map
            Pool<Order, (1u << 20 )> order_pool_;
        public:
            // Hot primitives — inline here (see architecture decision).
            Limit* best_ask() { return asks_.empty() ? nullptr : &asks_.begin()->second; }
            Limit* best_bid() { return bids_.empty() ? nullptr : &bids_.rbegin()->second; }
            Order* head_order(Limit* l) { return l->head; }
            void   reduce(Order* o, Qty q) { o->shares -= q; o->parent->volume -= q; }
            // Cold primitives — defined in book.cpp.
            void   remove_order(Order* o);
            Order* insert_order(Side side, Price price, Qty shares, OrderId id);
            // OrderIndex::find returns the Order* directly (nullptr on miss).
            Order* lookup(OrderId id) { return index_.find(id); }
    };
} // namespace lob
