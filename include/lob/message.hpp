#pragma once
#include "lob/types.hpp"

// The seam between parser and engine. parser.cpp produces these from LOBSTER CSV and
// knows nothing about Book; Engine consumes these and knows nothing about CSV. Swapping
// CSV for raw ITCH later touches only the parser.

namespace lob {

// LOBSTER event-type codes (message-file column 2).
enum class MsgType : uint8_t {
  NewLimit      = 1,  // new limit order
  PartialCancel = 2,  // partial cancellation (size reduced)
  Delete        = 3,  // total deletion of an order
  Execute       = 4,  // execution of a visible limit order
  ExecuteHidden = 5,  // execution of a hidden order
  // 6 unused in samples
  Halt          = 7,  // trading halt indicator
};

struct Message {
  int64_t  time_ns;  // seconds-after-midnight, scaled to integer ns (no floats)
  MsgType  type;
  OrderId  id;
  Qty      size;
  Price    price;
  Side     side;
};

} // namespace lob
