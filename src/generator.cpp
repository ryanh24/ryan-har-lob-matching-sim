#include "lob/generator.hpp"
#include <algorithm>
#include <cmath>

namespace lob {

Generator::Generator(const GenConfig& cfg)
    : cfg_(cfg), rng_(cfg.seed), mid_((double)cfg.mid0) {}

void Generator::step_mid() {
    std::normal_distribution<double> z(0.0, 1.0);
    // GBM step: mid *= exp(-0.5 vol^2 + vol * Z)
    mid_ *= std::exp(-0.5 * cfg_.gbm_vol * cfg_.gbm_vol + cfg_.gbm_vol * z(rng_));
    if (mid_ < (double)cfg_.tick * 2) mid_ = (double)cfg_.tick * 2;  // keep it positive
}

int Generator::depth_offset_ticks() {
    // Pareto (power-law) draw with exponent beta: mostly near the touch, heavy tail into depth.
    std::uniform_real_distribution<double> u(0.0, 1.0);
    double d = std::pow(1.0 - u(rng_), -1.0 / (cfg_.depth_beta - 1.0));  // >= 1
    int ticks = (int)d;
    return std::min(std::max(ticks, 1), 500);   // clamp the tail so prices stay sane
}

Qty Generator::draw_size() {
    std::uniform_int_distribution<uint32_t> lots(1, cfg_.max_lots);
    return cfg_.lot * lots(rng_);
}

Price Generator::round_to_tick(double p) const {
    long long t = (long long)llround(p / cfg_.tick);
    return (Price)(t * cfg_.tick);
}

GenOp Generator::next() {
    step_mid();
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::bernoulli_distribution buy(0.5);

    const double r = u(rng_);
    Message m{};
    m.time_ns = 0;
    OpClass cls;

    if (r < cfg_.p_cancel && !live_.empty()) {
        // CANCEL a random resting order (swap-remove from the live set).
        std::uniform_int_distribution<size_t> pick(0, live_.size() - 1);
        size_t idx = pick(rng_);
        m.type = MsgType::Delete;
        m.id   = live_[idx];
        live_[idx] = live_.back();
        live_.pop_back();
        cls = OpClass::Cancel;
    } else if (r < cfg_.p_cancel + cfg_.p_marketable) {
        // MARKETABLE order priced through the touch so it actually crosses.
        bool b = buy(rng_);
        m.type = MsgType::NewLimit;
        m.id   = next_id_++;
        m.side = b ? Side::Buy : Side::Sell;
        m.size = draw_size();
        double reach = (double)cfg_.marketable_reach * cfg_.tick;
        m.price = round_to_tick(b ? mid_ + reach : mid_ - reach);
        cls = OpClass::Marketable;
    } else {
        // ADD a resting limit near the touch (buys below mid, sells above), tracked for cancel.
        bool b = buy(rng_);
        double off = (double)depth_offset_ticks() * cfg_.tick;
        m.type = MsgType::NewLimit;
        m.id   = next_id_++;
        m.side = b ? Side::Buy : Side::Sell;
        m.size = draw_size();
        m.price = round_to_tick(b ? mid_ - off : mid_ + off);
        live_.push_back(m.id);
        cls = OpClass::Add;
    }
    return { m, cls };
}

} // namespace lob
