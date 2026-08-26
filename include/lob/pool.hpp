#pragma once
#include "lob/types.hpp"
#include <cstdlib>
#include <memory>

// Slab pool + intrusive free list. Generic over T (used for Order and Limit).
// See README decision: slab pool over new/delete; pointers first.

namespace lob {
    template <typename T, size_t Capacity> 
    class Pool {
        private:
            // The slab lives on the HEAP (a 64 MB array can't sit on the stack).
            // unique_ptr gives us one contiguous, owned block; slots_[i] indexes it.
            std::unique_ptr<T[]> slots_ = std::make_unique<T[]>(Capacity);
            T* free_head_; // the head of intrusive free list
        public:
            Pool() {
            // Put the slots together
            for (size_t i = 0; i + 1 < Capacity; ++i) {
                *reinterpret_cast<T**>(&slots_[i]) = &slots_[i+1];
            }
            *reinterpret_cast<T**>(&slots_[Capacity - 1]) = nullptr;
            free_head_ = &slots_[0];
            }

            T* allocate() {
                if (!free_head_) std::abort(); // abort if empty
                T* saved_slot = free_head_; // save the free head to return
                free_head_ = *reinterpret_cast<T**>(saved_slot);
                return saved_slot;
            }

            void deallocate(T* p) {
                *reinterpret_cast<T**>(p) = free_head_; // deref p to the free head
                free_head_ = p; //free head now becomes new head
            }




    };
} // namespace lob
