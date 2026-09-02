#include "lob/parser.hpp"
#include <cstdio>
#include <fstream>
#include <string>

// LOBSTER CSV -> Message. Knows nothing about Book.

namespace lob {

std::vector<Message> parse_messages(const char* path) {
    std::vector<Message> out;
    std::ifstream in(path);
    if (!in) return out;

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;

        double             t;      // seconds after midnight (parsed faithfully; unused on the hot path)
        int                type;
        unsigned long long id;
        unsigned           size;
        long long          price;  // signed & wide: real prices fit int32, but be lenient on input
        int                dir;

        if (std::sscanf(line.c_str(), "%lf,%d,%llu,%u,%lld,%d",
                        &t, &type, &id, &size, &price, &dir) != 6)
            continue;              // skip malformed line

        Message m;
        m.time_ns = static_cast<int64_t>(t * 1e9);
        m.type    = static_cast<MsgType>(type);
        m.id      = static_cast<OrderId>(id);
        m.size    = static_cast<Qty>(size);
        m.price   = static_cast<Price>(price);
        m.side    = (dir == 1) ? Side::Buy : Side::Sell;   // -1 (and halt's -1) → Sell
        out.push_back(m);
    }
    return out;
}

} // namespace lob
