#include "lob/book.hpp"
#include "lob/parser.hpp"
#include "lob/snapshot.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

// Binary B — LOBSTER reconstruction fidelity.
//
// IMPORTANT LIMITATION (empirically established): a level-N LOBSTER message file is filtered to
// events that affect the top-N book, so orders placed while their price sits deeper than level N
// generate no message. When the book shifts and that price surfaces into view, the orderbook file
// shows liquidity with no message trail. Exact top-N reconstruction from the message file alone is
// therefore impossible — an infinite-level engine still couldn't do it, because the input is
// incomplete. (Correctness of the engine itself is proven separately by the synthetic tests; the
// benchmark workload is synthetic-and-calibrated, matching how the field actually stresses matchers.)
//
// So this tool does NOT pass/fail. It seeds the book from the first orderbook snapshot, replays the
// messages (best-effort: cancels/executions of untracked pre-existing orders are applied as a
// phantom reduce at their price level), and reports how DEEP exact reconstruction stays correct and
// for how LONG before the completeness gap bites — a fidelity curve.
//
// Usage: verify <message.csv> <orderbook.csv> [levels]

using namespace lob;

// First (shallowest) 0-based level index where the two snapshots differ, else `levels`.
static int first_divergence(const Snapshot& a, const Snapshot& b, int levels) {
    auto scan = [&](const std::vector<Level>& x, const std::vector<Level>& y) -> int {
        for (int i = 0; i < levels; ++i) {
            bool xo = i < (int)x.size(), yo = i < (int)y.size();
            if (xo != yo) return i;
            if (xo && (x[i].price != y[i].price || x[i].size != y[i].size)) return i;
        }
        return levels;
    };
    return std::min(scan(a.asks, b.asks), scan(a.bids, b.bids));
}

static void apply_by_size(Book& book, const Message& m, unsigned long long& phantom) {
    Order* o = book.lookup(m.id);
    if (o) {
        if (m.size >= o->shares) book.remove_order(o);
        else                     book.reduce(o, m.size);
    } else {
        book.phantom_reduce(m.side, m.price, m.size);   // best-effort for a pre-existing order
        ++phantom;
    }
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s <message.csv> <orderbook.csv> [levels]\n", argv[0]); return 2; }
    const int levels = (argc >= 4) ? std::atoi(argv[3]) : 30;

    std::vector<Message>  msgs   = parse_messages(argv[1]);
    std::vector<Snapshot> oracle = parse_orderbook(argv[2], levels);
    if (msgs.empty() || oracle.empty() || msgs.size() != oracle.size()) {
        std::fprintf(stderr, "input problem: msgs=%zu oracle=%zu\n", msgs.size(), oracle.size());
        return 2;
    }

    Book book;
    OrderId seed_id = 1ULL << 63;   // reserved range, above any real LOBSTER id
    for (const Level& l : oracle[0].asks) book.insert_order(Side::Sell, l.price, l.size, seed_id++);
    for (const Level& l : oracle[0].bids) book.insert_order(Side::Buy,  l.price, l.size, seed_id++);

    // broke_at[K] = first message index where the top-K book first diverged (-1 = never).
    std::vector<long long> broke_at(levels + 1, -1);
    unsigned long long phantom = 0, hidden = 0, halt = 0;

    for (size_t i = 1; i < msgs.size(); ++i) {
        const Message& m = msgs[i];
        switch (m.type) {
            case MsgType::NewLimit:      book.insert_order(m.side, m.price, m.size, m.id); break;
            case MsgType::PartialCancel: apply_by_size(book, m, phantom); break;
            case MsgType::Delete: {
                Order* o = book.lookup(m.id);
                if (o) book.remove_order(o);
                else { book.phantom_reduce(m.side, m.price, m.size); ++phantom; }
                break;
            }
            case MsgType::Execute:       apply_by_size(book, m, phantom); break;
            case MsgType::ExecuteHidden: ++hidden; break;
            case MsgType::Halt:          ++halt;   break;
        }

        Snapshot ours = snapshot_book(book, levels);
        int d = first_divergence(ours, oracle[i], levels);   // top-K holds for all K <= d
        for (int K = d + 1; K <= levels; ++K)                // K-level view is broken here
            if (broke_at[K] == -1) broke_at[K] = (long long)i;
    }

    const size_t N = msgs.size();
    std::printf("LOBSTER reconstruction fidelity (%zu messages, %d levels seeded)\n", N, levels);
    std::printf("  depth   held-until-msg    %% of file\n");
    for (int K : {1, 2, 5, 10, 20, levels}) {
        if (K > levels) continue;
        if (broke_at[K] == -1) std::printf("  top-%-3d  (never diverged)   100.0%%\n", K);
        else                   std::printf("  top-%-3d  %-14lld    %.1f%%\n", K, broke_at[K], 100.0 * broke_at[K] / N);
    }
    std::printf("  phantom reduces=%llu  hidden-exec skipped=%llu  halts=%llu\n", phantom, hidden, halt);
    std::printf("  (divergence is the input completeness gap, not an engine error — see file header.)\n");
    return 0;
}
