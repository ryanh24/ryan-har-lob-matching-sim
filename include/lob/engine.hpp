#pragma once
#include "lob/book.hpp"
#include "lob/message.hpp"

// Behavior: owns the matching policy. apply(Message) dispatches on type and runs the
// matching loop (take liquidity across crossing levels, rest the remainder). Storage lives
// in Book; the Engine only speaks the Book primitives. See README architecture decision.

namespace lob {

class Engine {
    Book& book_;   // the Engine matches against a Book the caller owns (reference injection)

public:
    explicit Engine(Book& book) : book_(book) {}

    // Dispatch on message type; type-1 runs the matching loop, 2/3 are cancels.
    void apply(const Message& m);
};

} // namespace lob
