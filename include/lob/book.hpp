#pragma once
#include "lob/types.hpp"

// Storage only: two per-side AVL trees of Limit levels, cached best-bid/ask pointers,
// the Order/Limit pools, and the orderID index. Exposes primitives the Engine calls.
// Hot primitives (best_bid/ask, pop-front, decrement) are inline HERE so the
// storage/matching boundary is free on the hot path (see README architecture decision).
// TODO(tracer-bullet): class Book with insert_order / remove_order / best_bid / best_ask
//                      / decrement / delete_level.

namespace lob {
// class Book { ... };
} // namespace lob
