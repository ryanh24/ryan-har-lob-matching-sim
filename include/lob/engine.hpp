#pragma once
#include "lob/book.hpp"
#include "lob/message.hpp"

// Behavior: owns a Book, implements apply(Message) — dispatch on MsgType and run the
// matching loop (take liquidity across levels that cross the incoming limit, rest the
// remainder). Matching policy lives here, decoupled from storage.
// TODO(tracer-bullet): class Engine with apply(const Message&).

namespace lob {
// class Engine { ... };
} // namespace lob
