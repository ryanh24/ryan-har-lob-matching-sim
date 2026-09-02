#include "lob/snapshot.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace lob {

Snapshot snapshot_book(const Book& book, int levels) {
    Snapshot s;
    std::vector<std::pair<Price, Qty>> a, b;
    book.top_asks(levels, a);
    book.top_bids(levels, b);
    s.asks.reserve(a.size());
    s.bids.reserve(b.size());
    for (auto& [p, v] : a) s.asks.push_back({p, v});
    for (auto& [p, v] : b) s.bids.push_back({p, v});
    return s;
}

std::vector<Snapshot> parse_orderbook(const char* path, int levels) {
    // LOBSTER dummy prices for empty positions (these overflow int32, so read as int64 here).
    static constexpr long long ASK_SENTINEL =  9999999999LL;
    static constexpr long long BID_SENTINEL = -9999999999LL;

    std::vector<Snapshot> rows;
    std::ifstream in(path);
    if (!in) return rows;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        Snapshot s;

        std::stringstream ss(line);
        std::string cell;
        long long vals[4];
        // read 4 numbers per level: askP, askV, bidP, bidV
        for (int lvl = 0; lvl < levels; ++lvl) {
            bool ok = true;
            for (int k = 0; k < 4; ++k) {
                if (!std::getline(ss, cell, ',')) { ok = false; break; }
                vals[k] = std::stoll(cell);
            }
            if (!ok) break;
            long long askP = vals[0], askV = vals[1], bidP = vals[2], bidV = vals[3];
            if (askV > 0 && askP != ASK_SENTINEL) s.asks.push_back({(Price)askP, (Qty)askV});
            if (bidV > 0 && bidP != BID_SENTINEL) s.bids.push_back({(Price)bidP, (Qty)bidV});
        }
        rows.push_back(std::move(s));
    }
    return rows;
}

static bool side_equal(const std::vector<Level>& a, const std::vector<Level>& b,
                       int levels, const char* which, std::string& msg) {
    size_t na = std::min<size_t>(a.size(), (size_t)levels);
    size_t nb = std::min<size_t>(b.size(), (size_t)levels);
    if (na != nb) {
        char buf[128];
        std::snprintf(buf, sizeof buf, "%s level count differs: ours=%zu oracle=%zu", which, na, nb);
        msg = buf;
        return false;
    }
    for (size_t i = 0; i < na; ++i) {
        if (a[i].price != b[i].price || a[i].size != b[i].size) {
            char buf[192];
            std::snprintf(buf, sizeof buf,
                "%s level %zu differs: ours=(%d, %u) oracle=(%d, %u)",
                which, i, a[i].price, a[i].size, b[i].price, b[i].size);
            msg = buf;
            return false;
        }
    }
    return true;
}

bool snapshots_equal(const Snapshot& ours, const Snapshot& oracle, int levels, std::string& msg) {
    if (!side_equal(ours.asks, oracle.asks, levels, "ask", msg)) return false;
    if (!side_equal(ours.bids, oracle.bids, levels, "bid", msg)) return false;
    return true;
}

} // namespace lob
