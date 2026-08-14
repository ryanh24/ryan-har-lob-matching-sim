#pragma once
#include "lob/types.hpp"

// Slab pool + intrusive free list. Generic over T (used for Order and Limit).
// TODO(tracer-bullet): fixed-capacity slab, free-list head, allocate()/deallocate().
// See README decision: slab pool over new/delete; pointers first.

namespace lob {
// template <typename T> class Pool { ... };
} // namespace lob
