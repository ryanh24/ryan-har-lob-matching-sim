#pragma once
#include "lob/message.hpp"
#include <cstdint>
#include <random>
#include <vector>

// Synthetic order-flow generator for benchmarking — the standard way matchers are stressed
// (see arXiv 2606.01183): a geometric-Brownian-motion mid with limit-order depth drawn from a
// power-law, plus a configurable add/cancel/marketable mix. Seeded → fully reproducible, so
// knob-vs-knob latency comparisons differ only by the knob.
//
// Each op is tagged with its class so the bench harness can report per-operation distributions.

namespace lob {

enum class OpClass : uint8_t { Add, Cancel, Marketable };
struct GenOp { Message msg; OpClass cls; };

struct GenConfig {
    uint64_t seed         = 42;
    Price    mid0         = 100000;  // starting mid ($10.00 in 1/10000)
    Price    tick         = 100;     // $0.01
    double   gbm_vol      = 5e-5;    // per-message log-return stdev of the mid
    double   depth_beta   = 2.23;    // power-law exponent for limit-order depth (fitted, per arXiv)
    Qty      lot          = 100;     // share lot
    uint32_t max_lots     = 10;      // order size = lot * U[1..max_lots]
    uint32_t marketable_reach = 50;  // marketable orders price this many ticks through the touch
    // op mix (should sum to ~1); a cancel with no resting orders falls back to an add
    double   p_add        = 0.60;
    double   p_cancel     = 0.35;
    double   p_marketable = 0.05;
};

class Generator {
public:
    explicit Generator(const GenConfig& cfg);
    GenOp next();                       // produce the next tagged operation

private:
    GenConfig            cfg_;
    std::mt19937_64      rng_;
    double               mid_;          // current mid (float; rounded to tick on use)
    std::vector<OrderId> live_;         // ids of resting adds we can later cancel
    OrderId              next_id_ = 1;

    void  step_mid();                   // advance the GBM mid one step
    int   depth_offset_ticks();         // power-law draw: how far from the touch (in ticks)
    Qty   draw_size();
    Price round_to_tick(double p) const;
};

} // namespace lob
