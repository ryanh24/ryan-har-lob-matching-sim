#pragma once
#include "lob/types.hpp"

// orderID -> OrderRef lookup. Hand-rolled open-addressing map: linear probing,
// power-of-two + mask, backward-shift deletion, pluggable hash (modulo-prime baseline
// -> Fibonacci -> CRC32). Accessed only via insert/find/erase so the backend is swappable.
// TODO(tracer-bullet): the flat table + probe + backshift delete.

namespace lob {
// class OrderIndex { ... };
} // namespace lob
