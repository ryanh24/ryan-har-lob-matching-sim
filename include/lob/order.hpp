#pragma once
#include "lob/types.hpp"

// A resting order. Hot fields grouped at top (see memory-management-ideas.md) so a
// future hot/cold split is cheap. Intrusive prev/next make it a dlist node in its level.

namespace lob {
    struct alignas(64) Order {
        Order* next; // next order in queue
        Order* prev; // previous order in queue
        Limit* parent; // orderbook price level
        uint32_t shares; // number of shares
        uint64_t id; // unique order id
    };
} // namespace lob
