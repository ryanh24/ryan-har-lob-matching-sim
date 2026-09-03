#pragma once
#include "lob/book.hpp"
#include "lob/message.hpp"
#include <algorithm>   // std::min

// Matching policy, parameterized on the Book type. apply(Message) dispatches on type and runs the
// matching loop; storage lives in the Book. Header-only (template).
//
//   Engine    = EngineT<Book>      (hand-rolled backend)
//   EngineStd = EngineT<BookStd>   (std:: backend — for the head-to-head)

namespace lob {

template <class BookType>
class EngineT {
    BookType& book_;

public:
    explicit EngineT(BookType& book) : book_(book) {}

    void apply(const Message& m) {
        switch (m.type) {
            case MsgType::NewLimit: {
                Qty remaining = m.size;
                if (m.side == Side::Buy) {
                    // A buy takes from the asks: cross while the cheapest ask <= our limit.
                    while (remaining > 0) {
                        Limit* best = book_.best_ask();
                        if (best == nullptr || best->price > m.price) break;
                        Order* resting = best->head;
                        Qty fill = std::min(remaining, resting->shares);
                        remaining -= fill;
                        if (fill == resting->shares) book_.remove_order(resting);
                        else                         book_.reduce(resting, fill);
                    }
                } else {
                    // A sell takes from the bids: cross while the highest bid >= our limit.
                    while (remaining > 0) {
                        Limit* best = book_.best_bid();
                        if (best == nullptr || best->price < m.price) break;
                        Order* resting = best->head;
                        Qty fill = std::min(remaining, resting->shares);
                        remaining -= fill;
                        if (fill == resting->shares) book_.remove_order(resting);
                        else                         book_.reduce(resting, fill);
                    }
                }
                if (remaining > 0)                                   // rest the residual on its own side
                    book_.insert_order(m.side, m.price, remaining, m.id);
                break;
            }
            case MsgType::PartialCancel: { Order* o = book_.lookup(m.id); if (o) book_.reduce(o, m.size); } break;
            case MsgType::Delete:        { Order* o = book_.lookup(m.id); if (o) book_.remove_order(o); }    break;
            default: break;   // Execute / Hidden / Halt — not part of the synthetic matching path
        }
    }
};

using Engine    = EngineT<Book>;
using EngineStd = EngineT<BookStd>;

} // namespace lob
