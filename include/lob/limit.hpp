#pragma once
#include "lob/types.hpp"

// A price level: FIFO dlist of orders (head/tail) plus AVL-tree links (parent, children,
// height) and an aggregated volume counter. One tree per side.
// TODO(tracer-bullet): fields + AVL node layout.

namespace lob {
// struct Limit { ... };
} // namespace lob
