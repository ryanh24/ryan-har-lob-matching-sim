#include "lob/book.hpp"

// Cold Book ops (build/tear-down of levels). Hot primitives stay inline in book.hpp.

namespace lob {

void Book::remove_order(Order* o) {
    Limit* level = o->parent; // SAVE the level before we free o (o dies at deallocate)

    // Unlink o from the level's FIFO queue. Guard the ends and fix head/tail:
    if (o->prev) o->prev->next = o->next; // middle/tail: stitch predecessor to successor
    else         level->head   = o->next; // o was the head → new head is o->next
    if (o->next) o->next->prev = o->prev; // middle/head: stitch successor to predecessor
    else         level->tail   = o->prev; // o was the tail → new tail is o->prev

    level->volume -= o->shares;  // adjust level volume
    index_.erase(o->id);         // remove from the id index
    order_pool_.deallocate(o);   // give the slot back — o is INVALID from here on

    if (level->head == nullptr) { // level emptied → remove that Limit from its side
        std::map<Price, Limit>& book_side =
            (bids_.find(level->price) != bids_.end()) ? bids_ : asks_;
        book_side.erase(level->price);
    }
}

Order* Book::insert_order(Side side, Price price, Qty shares, OrderId id) {
    std::map<Price, Limit>& book_side = (side == Side::Buy) ? bids_ : asks_;

    Limit& level = book_side[price]; // creates a clean Limit if the price is new
    level.price  = price;            // set the price (harmless if already set)

    Order* o  = order_pool_.allocate(); // grab a slot from the pool, fill it
    o->next   = nullptr;                // newest order → it's the tail → nothing after it
    o->parent = &level;
    o->shares = shares;
    o->id     = id;

    if (level.tail == nullptr) { // empty level: o is the only order
        o->prev    = nullptr;
        level.head = o;
        level.tail = o;
    } else {                     // non-empty: link o after the current tail
        o->prev          = level.tail;
        level.tail->next = o;    // old tail points forward to o BEFORE we move the tail
        level.tail       = o;
    }

    level.volume += shares; // keep volume + index in sync
    index_.insert(id, o);
    return o;
}

} // namespace lob
