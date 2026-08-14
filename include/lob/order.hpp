#pragma once
#include "lob/types.hpp"

// A resting order. Hot fields grouped at top (see memory-management-ideas.md) so a
// future hot/cold split is cheap. Intrusive prev/next make it a dlist node in its level.
// TODO(tracer-bullet): fields (prev, next, parent, price, shares; id, timestamp cold).

namespace lob {
// struct Order { ... };
} // namespace lob
