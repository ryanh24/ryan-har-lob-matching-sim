#pragma once
#include "lob/types.hpp"
#include "lob/order.hpp"
#include "lob/limit.hpp"
#include "lob/pool.hpp"
#include "lob/price_tree.hpp"
#include "lob/order_index.hpp"

// Storage only: two per-side AVL trees (PriceTree) of Limit levels, the Order pool, and the
// hand-rolled order-ID index. Exposes the primitives the Engine's matching loop calls. Hot
// primitives are inline here so the storage/matching boundary is free on the hot path
// (see README architecture decision).

namespace lob {
    class Book {
        private:
            PriceTree  asks_;
            PriceTree  bids_;
            OrderIndex index_;                    // hand-rolled open-addressing map
            Pool<Order, (1u << 20)> order_pool_;  // ~1M order slots
        public:
            // Hot primitives — inline (best-of-side is the tree's min/max; nullptr if empty).
            Limit* best_ask() { return asks_.min(); }   // lowest ask price
            Limit* best_bid() { return bids_.max(); }   // highest bid price
            Order* head_order(Limit* l) { return l->head; }
            void   reduce(Order* o, Qty q) { o->shares -= q; o->parent->volume -= q; }
            // Cold primitives — defined in book.cpp.
            void   remove_order(Order* o);
            Order* insert_order(Side side, Price price, Qty shares, OrderId id);
            // Warm-start support (LOBSTER verify): reduce a level by price when the specific
            // order isn't tracked (a pre-existing order). Reduces the level's head (oldest) order.
            void   phantom_reduce(Side side, Price price, Qty qty);
            // OrderIndex::find returns the Order* directly (nullptr on miss).
            Order* lookup(OrderId id) { return index_.find(id); }

            // Read-only top-N views for the snapshot (asks ascending, bids descending).
            void top_asks(int n, std::vector<std::pair<Price, Qty>>& out) const { asks_.top_ascending(n, out); }
            void top_bids(int n, std::vector<std::pair<Price, Qty>>& out) const { bids_.top_descending(n, out); }
    };
} // namespace lob
