#include "lob/book.hpp"
#include "lob/engine.hpp"
#include "lob/generator.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

// Binary A — latency + throughput on synthetic, calibrated flow (see generator.hpp).
//
// Methodology: pre-generate the workload (generation is OUT of the timed path); warm up and
// discard the first N ops; then (1) batch-time the rest for throughput, and (2) time each op
// individually for per-operation distributions (p50/p90/p99/p99.9/max), bucketed by op class.
//
// PLATFORM NOTE: on macOS/Apple Silicon the per-op timer overhead (~tens of ns) inflates absolute
// latencies and the coarse timers hide the true sub-100ns tail — so the per-op numbers here are
// INDICATIVE (relative shape across op classes is still meaningful). Authoritative numbers come
// from the Linux bench host with RDTSC + CPU pinning. Batch throughput is meaningful anywhere.
//
// Usage: bench [total_ops] [warmup] [seed]

using namespace lob;
using Clock = std::chrono::steady_clock;

static uint64_t pct(std::vector<uint64_t>& v, double p) {
    if (v.empty()) return 0;
    size_t i = (size_t)(p * (v.size() - 1));
    return v[i];   // v must be sorted
}

static void report(const char* label, std::vector<uint64_t>& v) {
    if (v.empty()) { std::printf("  %-11s (none)\n", label); return; }
    std::sort(v.begin(), v.end());
    std::printf("  %-11s n=%-8zu p50=%-5llu p90=%-5llu p99=%-6llu p99.9=%-6llu max=%llu  (ns)\n",
                label, v.size(),
                (unsigned long long)pct(v, 0.50), (unsigned long long)pct(v, 0.90),
                (unsigned long long)pct(v, 0.99), (unsigned long long)pct(v, 0.999),
                (unsigned long long)v.back());
}

int main(int argc, char** argv) {
    const size_t total  = (argc >= 2) ? (size_t)std::atoll(argv[1]) : 2'000'000;
    const size_t warmup = (argc >= 3) ? (size_t)std::atoll(argv[2]) : total / 10;
    GenConfig cfg;
    if (argc >= 4) cfg.seed = (uint64_t)std::atoll(argv[3]);
    if (warmup >= total) { std::fprintf(stderr, "warmup must be < total\n"); return 2; }

    // Pre-generate the workload once (shared by both passes).
    std::vector<GenOp> ops;
    ops.reserve(total);
    { Generator g(cfg); for (size_t i = 0; i < total; ++i) ops.push_back(g.next()); }

    std::printf("bench: %zu ops (%zu warmup), seed=%llu\n", total, warmup, (unsigned long long)cfg.seed);

    // --- Pass 1: batch throughput (no per-op clocks) ---
    {
        Book book; Engine engine(book);
        for (size_t i = 0; i < warmup; ++i) engine.apply(ops[i].msg);
        auto t0 = Clock::now();
        for (size_t i = warmup; i < total; ++i) engine.apply(ops[i].msg);
        auto t1 = Clock::now();
        double sec = std::chrono::duration<double>(t1 - t0).count();
        size_t n = total - warmup;
        std::printf("throughput: %.2f M ops/s   (%.1f ns/op amortized over %zu ops)\n",
                    n / sec / 1e6, sec * 1e9 / n, n);
    }

    // --- Pass 2: per-op distributions ---
    {
        Book book; Engine engine(book);
        for (size_t i = 0; i < warmup; ++i) engine.apply(ops[i].msg);
        std::vector<uint64_t> lat[3];
        for (auto& v : lat) v.reserve((total - warmup) / 2);
        std::vector<uint64_t> all;
        all.reserve(total - warmup);

        for (size_t i = warmup; i < total; ++i) {
            auto t0 = Clock::now();
            engine.apply(ops[i].msg);
            auto t1 = Clock::now();
            uint64_t ns = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            lat[(int)ops[i].cls].push_back(ns);
            all.push_back(ns);
        }
        std::printf("per-op latency (INDICATIVE on macOS — see note):\n");
        report("add",        lat[(int)OpClass::Add]);
        report("cancel",     lat[(int)OpClass::Cancel]);
        report("marketable", lat[(int)OpClass::Marketable]);
        report("overall",    all);
    }
    return 0;
}
