#include "lob/order_index.hpp"
#include <cstdio>
#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

using namespace lob;

static int failures = 0;
static void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL %s\n", what); ++failures; }
}

// Fake non-null Order* values tied to a key (we only compare pointer identity).
static Order* val(OrderId k) { return reinterpret_cast<Order*>(static_cast<uintptr_t>(k) + 1); }

int main() {
    OrderIndex idx;

    // 1) Basic insert / find / update / erase.
    idx.insert(42, val(42));
    check(idx.find(42) == val(42), "find after insert");
    idx.insert(42, val(999));                       // update
    check(idx.find(42) == val(999), "find returns updated value");
    check(idx.find(43) == nullptr, "miss returns nullptr");
    idx.erase(42);
    check(idx.find(42) == nullptr, "find after erase is a miss");
    check(idx.size() == 0, "size back to 0");

    // 2) Sequential ids (the LOBSTER clustering case) — 100k in a row, all findable.
    for (OrderId k = 1'000'000; k < 1'100'000; ++k) idx.insert(k, val(k));
    bool all_found = true;
    for (OrderId k = 1'000'000; k < 1'100'000; ++k) if (idx.find(k) != val(k)) all_found = false;
    check(all_found, "100k sequential ids all findable (Fibonacci hash scatters them)");

    // Erase every other one; survivors still findable, erased ones gone — this exercises
    // backward-shift deletion hard (holes opened all through the clusters).
    for (OrderId k = 1'000'000; k < 1'100'000; k += 2) idx.erase(k);
    bool backshift_ok = true;
    for (OrderId k = 1'000'000; k < 1'100'000; ++k) {
        Order* got = idx.find(k);
        Order* want = (k % 2 == 0) ? nullptr : val(k);   // evens erased, odds kept
        if (got != want) backshift_ok = false;
    }
    check(backshift_ok, "backward-shift delete keeps survivors findable");

    // 3) Randomized cross-check against std::unordered_map — the real backshift torture test.
    std::mt19937_64 rng(12345);
    std::unordered_map<OrderId, Order*> ref;
    OrderIndex mine;
    std::vector<OrderId> live;
    bool agree = true;
    for (int op = 0; op < 500'000 && agree; ++op) {
        bool do_insert = live.empty() || (rng() & 1);
        if (do_insert) {
            OrderId k = rng() % 200'000;             // small space → forces collisions
            mine.insert(k, val(k));
            if (ref.find(k) == ref.end()) live.push_back(k);
            ref[k] = val(k);
        } else {
            size_t idx_ = rng() % live.size();       // erase a random live key
            OrderId k = live[idx_];
            live[idx_] = live.back(); live.pop_back();
            mine.erase(k);
            ref.erase(k);
        }
        // spot-check a few keys agree between the two maps
        for (int s = 0; s < 4; ++s) {
            OrderId k = rng() % 200'000;
            Order* want = (ref.count(k) ? ref[k] : nullptr);
            if (mine.find(k) != want) agree = false;
        }
    }
    check(agree, "randomized ops agree with std::unordered_map");
    check(mine.size() == ref.size(), "size matches reference after random ops");

    if (failures == 0) { std::puts("index_test: all assertions passed"); return 0; }
    std::printf("index_test: %d FAILURE(S)\n", failures);
    return 1;
}
