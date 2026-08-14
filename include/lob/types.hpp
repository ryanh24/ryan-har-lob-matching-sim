#pragma once
#include <cstdint>

// Shared vocabulary for the whole engine. No logic — just the decided representations.
// See README decision log: integer LOBSTER-unit prices; slab-pool refs behind a typedef.

namespace lob {

using Price   = int32_t;   // LOBSTER unit = 1/10000 dollar. int32 covers up to $214,748.36/share.
using Qty     = uint32_t;  // shares
using OrderId = uint64_t;  // LOBSTER order id (sparse, non-contiguous)

enum class Side : uint8_t { Buy, Sell };

// Reference hatch: raw pointers in v1 (debuggability), one-line swap to uint32_t pool
// indices later. Structs are forward-declared so types.hpp stays logic-free.
struct Order;
struct Limit;
using OrderRef = Order*;
using LimitRef = Limit*;

inline constexpr OrderRef NULL_ORDER = nullptr;
inline constexpr LimitRef NULL_LIMIT = nullptr;

} // namespace lob
