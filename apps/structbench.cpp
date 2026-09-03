#include "lob/order_index.hpp"
#include "lob/price_tree.hpp"
#include "lob/order.hpp"
#include "lob/limit.hpp"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <random>
#include <unordered_map>
#include <vector>

// Head-to-head: the hand-rolled structures vs their std:: equivalents, on identical, realistic
// operation sequences. The RATIO (ours / std::) is the result and is meaningful on any platform,
// since the same timer overhead applies to both sides and cancels. (Absolute sub-100ns tails come
// from the Linux host with RDTSC; this reports amortized ns/op + the speedup.)
//
// Two comparisons:
//   OrderIndex  vs  std::unordered_map<OrderId, Order*>   — order-id lookup under churn
//   PriceTree   vs  std::map<Price, Limit>                — price levels + best-of-side reads

using namespace lob;
using Clock = std::chrono::steady_clock;

enum class Op : uint8_t { Insert, Find, Erase, Best };
struct KeyOp { Op op; uint64_t key; };

static double ns_per_op(Clock::time_point a, Clock::time_point b, size_t n) {
    return std::chrono::duration<double, std::nano>(b - a).count() / (double)n;
}

// Build a churny insert/find/erase sequence over a live set of ~`live_target` keys.
static std::vector<KeyOp> make_ops(uint64_t seed, size_t n, size_t live_target, bool sequential_keys) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> u(0, 1);
    std::vector<KeyOp> ops; ops.reserve(n);
    std::vector<uint64_t> live;
    uint64_t next_key = 1;
    std::normal_distribution<double> price_walk(0, 1);
    double mid = 100000;

    for (size_t i = 0; i < n; ++i) {
        double r = u(rng);
        if (r < 0.15) { ops.push_back({Op::Best, 0}); continue; }   // best-of-side read
        if (live.size() < live_target || r < 0.45) {                // INSERT
            uint64_t k;
            if (sequential_keys) k = next_key++;                    // LOBSTER-like incrementing ids
            else { mid += price_walk(rng) * 2; k = (uint64_t)((long long)(mid) / 100 * 100); } // price near a walking mid
            ops.push_back({Op::Insert, k});
            live.push_back(k);
        } else if (r < 0.70 && !live.empty()) {                     // FIND a live key
            ops.push_back({Op::Find, live[rng() % live.size()]});
        } else if (!live.empty()) {                                 // ERASE a live key
            size_t idx = rng() % live.size();
            ops.push_back({Op::Erase, live[idx]});
            live[idx] = live.back(); live.pop_back();
        }
    }
    return ops;
}

// ---- OrderIndex vs std::unordered_map ----
template <class Index>
static double run_index(const std::vector<KeyOp>& ops, Index& idx) {
    Order* dummy = reinterpret_cast<Order*>(0x1);   // non-null sentinel value
    volatile uint64_t sink = 0;
    auto t0 = Clock::now();
    for (const KeyOp& o : ops) {
        switch (o.op) {
            case Op::Insert: idx.insert(o.key, dummy); break;
            case Op::Find:   sink += (uint64_t)idx.find(o.key); break;
            case Op::Erase:  idx.erase(o.key); break;
            case Op::Best:   break;   // no best-of-side concept for the index
        }
    }
    auto t1 = Clock::now();
    (void)sink;
    return ns_per_op(t0, t1, ops.size());
}

// std::unordered_map adapter with the same insert/find/erase surface.
struct StdIndex {
    std::unordered_map<OrderId, Order*> m;
    void   insert(OrderId k, Order* v) { m[k] = v; }
    Order* find(OrderId k) const { auto it = m.find(k); return it == m.end() ? nullptr : it->second; }
    void   erase(OrderId k) { m.erase(k); }
};

// ---- PriceTree vs std::map ----
template <class Tree>
static double run_tree(const std::vector<KeyOp>& ops, Tree& tree) {
    volatile uint64_t sink = 0;
    auto t0 = Clock::now();
    for (const KeyOp& o : ops) {
        switch (o.op) {
            case Op::Insert: tree.find_or_create((Price)o.key); break;
            case Op::Find:   sink += (uint64_t)tree.find((Price)o.key); break;
            case Op::Erase:  tree.erase((Price)o.key); break;
            case Op::Best:   sink += (uint64_t)tree.min() + (uint64_t)tree.max(); break;
        }
    }
    auto t1 = Clock::now();
    (void)sink;
    return ns_per_op(t0, t1, ops.size());
}

// std::map adapter with the PriceTree surface used by the bench.
struct StdTree {
    std::map<Price, Limit> m;
    Limit* find(Price p) { auto it = m.find(p); return it == m.end() ? nullptr : &it->second; }
    Limit* find_or_create(Price p) { Limit& l = m[p]; l.price = p; return &l; }
    void   erase(Price p) { m.erase(p); }
    Limit* min() { return m.empty() ? nullptr : &m.begin()->second; }
    Limit* max() { return m.empty() ? nullptr : &m.rbegin()->second; }
};

int main(int argc, char** argv) {
    const size_t n = (argc >= 2) ? (size_t)std::atoll(argv[1]) : 5'000'000;

    std::printf("structbench: %zu ops each (ratio = std:: / ours; >1 means ours is faster)\n\n", n);

    // Index: sequential (LOBSTER-like) keys, ~50k live.
    {
        auto ops = make_ops(1, n, 50'000, /*sequential=*/true);
        OrderIndex ours; StdIndex theirs;
        double a = run_index(ops, ours);
        double b = run_index(ops, theirs);
        std::printf("OrderIndex  vs unordered_map:  ours=%6.1f ns/op   std::=%6.1f ns/op   ratio=%.2fx\n", a, b, b / a);
    }
    // Tree: price keys near a walking mid, ~1000 live levels.
    {
        auto ops = make_ops(2, n, 1'000, /*sequential=*/false);
        PriceTree ours; StdTree theirs;
        double a = run_tree(ops, ours);
        double b = run_tree(ops, theirs);
        std::printf("PriceTree   vs std::map:       ours=%6.1f ns/op   std::=%6.1f ns/op   ratio=%.2fx\n", a, b, b / a);
    }
    return 0;
}
