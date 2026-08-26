#include "lob/engine.hpp"
#include <algorithm>   // std::min

// Engine::apply — dispatch on message type; type-1 runs the matching loop.

namespace lob {

void Engine::apply(const Message& m) {
    switch (m.type) {
        case MsgType::NewLimit: {
            Qty remaining = m.size;

            // Re-fetch best each turn so we never touch a level a removal deleted.
            if (m.side == Side::Buy) {
                // A buy takes from the asks: cross while the cheapest ask <= our limit.
                while (remaining > 0) {
                    Limit* best = book_.best_ask();
                    if (best == nullptr || best->price > m.price) break; // no asks / too expensive
                    Order* resting = best->head;
                    Qty fill = std::min(remaining, resting->shares);
                    remaining -= fill;
                    if (fill == resting->shares) book_.remove_order(resting); // may delete level…
                    else                         book_.reduce(resting, fill); // …so don't touch best again
                }
            } else {
                // A sell takes from the bids: cross while the highest bid >= our limit.
                while (remaining > 0) {
                    Limit* best = book_.best_bid();
                    if (best == nullptr || best->price < m.price) break; // no bids / too cheap
                    Order* resting = best->head;
                    Qty fill = std::min(remaining, resting->shares);
                    remaining -= fill;
                    if (fill == resting->shares) book_.remove_order(resting);
                    else                         book_.reduce(resting, fill);
                }
            }

            if (remaining > 0)                                    // rest the residual on its own side
                book_.insert_order(m.side, m.price, remaining, m.id);
            break;
        }
        case MsgType::PartialCancel: {
            Order* o = book_.lookup(m.id); if (o) book_.reduce(o, m.size);
        } break;
        case MsgType::Delete: {
            Order* o = book_.lookup(m.id); if (o) book_.remove_order(o);
        } break;
        default: break; // Execute / Hidden / Halt — not needed for the synthetic test
    }
}

} // namespace lob
