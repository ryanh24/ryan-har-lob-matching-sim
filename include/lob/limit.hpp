#pragma once
#include "lob/types.hpp"

// A price level: FIFO dlist of orders (head/tail), an aggregated volume counter, and the
// AVL-tree links so Limits can live in a hand-rolled balanced tree (one per side).
// Recursive AVL → no parent pointer needed.

namespace lob {
    struct Limit {
        Order* head = nullptr; // oldest order at this price
        Order* tail = nullptr; // newest order at this price
        int32_t  price  = 0;   // price in the respective orderbook
        uint32_t volume = 0;   // total resting shares at this level
        // --- AVL links ---
        Limit* left   = nullptr;
        Limit* right  = nullptr;
        int    height = 1;     // leaf = 1; empty subtree treated as height 0
    };
} // namespace lob
