#pragma once
#include "lob/types.hpp"
#include <cstdint>
#include <cstdlib>
#include <memory>

// orderID -> Order* lookup. Hand-rolled open-addressing hash map realizing the design decision:
//   - open addressing, one flat array (no chaining, no per-node allocation)
//   - LINEAR probing (walk slot+1, slot+2 … on collision — sequential, cache-friendly)
//   - power-of-two capacity + bit-mask indexing (h & MASK — one AND, no modulo)
//   - Fibonacci (multiply-shift) hash so sequential LOBSTER ids don't cluster
//   - BACKWARD-SHIFT deletion (no tombstones — the cluster self-heals on erase)
//   - low load factor (<= 0.5); fixed capacity, panic on exhaustion (mirrors the pools)
// Accessed only via insert / find / erase, so the backend stays swappable.

namespace lob {

class OrderIndex {
    static constexpr size_t CAPACITY = 1u << 21;   // 2,097,152 slots (>= 2x the 1M order pool)
    static constexpr size_t MASK     = CAPACITY - 1;
    static constexpr int    SHIFT    = 64 - 21;    // keep the top 21 bits of the Fibonacci hash

    // A slot is EMPTY iff value == nullptr; key is meaningful only when occupied.
    struct Slot { OrderId key; Order* value; };
    std::unique_ptr<Slot[]> slots_ = std::make_unique<Slot[]>(CAPACITY); // value-init: {0, nullptr}
    size_t count_ = 0;

    // Fibonacci hash: multiply by the 64-bit golden-ratio constant, take the top bits.
    // Smears sequential ids across the whole table so linear probing stays short.
    static size_t home(OrderId key) {
        return static_cast<size_t>((key * 0x9E3779B97F4A7C15ull) >> SHIFT);
    }

    // Is k cyclically within the interval (i, j]? Used by backward-shift deletion to decide
    // whether the entry at j is "correctly placed" relative to the hole at i (leave it) or
    // can be pulled back to fill the hole (move it).
    static bool in_cyclic_range(size_t i, size_t k, size_t j) {
        return (i <= j) ? (i < k && k <= j) : (i < k || k <= j);
    }

public:
    // Insert or update. On a fresh key, place it at the first empty slot in its probe run.
    void insert(OrderId key, Order* value) {
        size_t i = home(key);
        while (slots_[i].value != nullptr) {          // walk the probe run
            if (slots_[i].key == key) { slots_[i].value = value; return; } // key exists → update
            i = (i + 1) & MASK;                       // linear step, wrap with the mask
        }
        slots_[i] = Slot{ key, value };               // first empty slot → place it
        if (++count_ * 2 > CAPACITY) std::abort();    // load factor > 0.5 → fail loud (fixed size)
    }

    // Find: walk the probe run until we match the key (hit) or reach an empty slot (miss).
    Order* find(OrderId key) const {
        size_t i = home(key);
        while (slots_[i].value != nullptr) {
            if (slots_[i].key == key) return slots_[i].value;
            i = (i + 1) & MASK;
        }
        return nullptr;
    }

    // Erase with BACKWARD-SHIFT deletion: pull later entries back into the hole so the probe
    // run stays contiguous — no tombstones, so the table never rots under heavy cancel traffic.
    void erase(OrderId key) {
        size_t i = home(key);                          // locate the key
        while (slots_[i].value != nullptr && slots_[i].key != key)
            i = (i + 1) & MASK;
        if (slots_[i].value == nullptr) return;        // not present
        --count_;

        // i is now the hole. Scan forward; move back any entry whose home is at/before the hole.
        size_t j = i;
        while (true) {
            j = (j + 1) & MASK;
            if (slots_[j].value == nullptr) break;     // cluster ended → done
            size_t k = home(slots_[j].key);            // ideal home of the entry at j
            if (!in_cyclic_range(i, k, j)) {           // its home is at/before the hole → pull it back
                slots_[i] = slots_[j];
                i = j;                                 // the hole moves to j
            }
        }
        slots_[i].value = nullptr;                     // empty the final hole
    }

    size_t size() const { return count_; }
};

} // namespace lob
