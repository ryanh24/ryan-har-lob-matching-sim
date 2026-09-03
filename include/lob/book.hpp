#pragma once
#include "lob/types.hpp"
#include "lob/order.hpp"
#include "lob/limit.hpp"
#include "lob/pool.hpp"
#include "lob/price_tree.hpp"
#include "lob/order_index.hpp"
#include "lob/std_containers.hpp"
#include <utility>
#include <vector>

// Storage, parameterized on the price-level tree and the order-id index so the whole engine can be
// benchmarked on either the hand-rolled structures or std:: baselines. Header-only because it's a
// template (all methods inline — good for the hot path; the earlier hot/cold .hpp/.cpp split is
// superseded by templating, see the decision log).
//
//   Book    = BookT<PriceTree, OrderIndex>   (hand-rolled — the real engine)
//   BookStd = BookT<StdTree,   StdIndex>     (std:: baseline — for the head-to-head)

namespace lob {

template <class Tree, class Index>
class BookT {
    Tree  asks_;
    Tree  bids_;
    Index index_;
    Pool<Order, (1u << 20)> order_pool_;   // ~1M order slots

public:
    // Hot primitives.
    Limit* best_ask() { return asks_.min(); }   // lowest ask price
    Limit* best_bid() { return bids_.max(); }   // highest bid price
    Order* head_order(Limit* l) { return l->head; }
    void   reduce(Order* o, Qty q) { o->shares -= q; o->parent->volume -= q; }
    Order* lookup(OrderId id) { return index_.find(id); }

    // Read-only top-N views for the snapshot (asks ascending, bids descending).
    void top_asks(int n, std::vector<std::pair<Price, Qty>>& out) const { asks_.top_ascending(n, out); }
    void top_bids(int n, std::vector<std::pair<Price, Qty>>& out) const { bids_.top_descending(n, out); }

    void remove_order(Order* o) {
        Limit* level = o->parent;                       // save before we free o
        if (o->prev) o->prev->next = o->next; else level->head = o->next;
        if (o->next) o->next->prev = o->prev; else level->tail = o->prev;
        level->volume -= o->shares;
        index_.erase(o->id);
        order_pool_.deallocate(o);                       // o is invalid from here
        if (level->head == nullptr) {                    // level emptied → remove it from its side
            Tree& tree = (bids_.find(level->price) != nullptr) ? bids_ : asks_;
            tree.erase(level->price);
        }
    }

    Order* insert_order(Side side, Price price, Qty shares, OrderId id) {
        Tree& book_side = (side == Side::Buy) ? bids_ : asks_;
        Limit* level = book_side.find_or_create(price);
        Order* o = order_pool_.allocate();
        o->next = nullptr; o->parent = level; o->shares = shares; o->id = id;
        if (level->tail == nullptr) { o->prev = nullptr; level->head = o; level->tail = o; }
        else { o->prev = level->tail; level->tail->next = o; level->tail = o; }
        level->volume += shares;
        index_.insert(id, o);
        return o;
    }

    // Warm-start support (LOBSTER verify): reduce a level by price when the order isn't tracked.
    void phantom_reduce(Side side, Price price, Qty qty) {
        Tree& tree = (side == Side::Buy) ? bids_ : asks_;
        Limit* level = tree.find(price);
        if (!level || level->head == nullptr) return;
        Order* o = level->head;                          // the seed (oldest) order at this level
        if (qty >= o->shares) remove_order(o);
        else                  reduce(o, qty);
    }
};

using Book    = BookT<PriceTree, OrderIndex>;   // the real engine
using BookStd = BookT<StdTree,   StdIndex>;     // std:: baseline for benchmarking

} // namespace lob
