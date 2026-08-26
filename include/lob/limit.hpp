#pragma once
#include "lob/types.hpp"

// A price level: FIFO dlist of orders (head/tail) plus AVL-tree links (parent, children,
// height) and an aggregated volume counter. One tree per side.

namespace lob {
    struct Limit {
        Order* head = nullptr; // oldest order at this price
        Order* tail = nullptr; // newest order at this price 
        int32_t price = 0; // actual price in the respective orderbook
        uint32_t volume = 0; // total volume at this level
    };
} // namespace lob
