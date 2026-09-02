#pragma once
#include "lob/message.hpp"
#include <vector>

// LOBSTER message-file parser. Turns the 6-column CSV into a vector of Message.
// Knows nothing about Book — the parser→Message→Engine seam (see architecture decision).
// Not on the hot path, so simplicity over speed (plain istream + sscanf).

namespace lob {

// Parse a LOBSTER "_message_" CSV. Columns: time, type, order-id, size, price, direction
// (direction 1 = buy, -1 = sell). Returns the messages in file order. Empty vector on open
// failure.
std::vector<Message> parse_messages(const char* path);

} // namespace lob
