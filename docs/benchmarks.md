# Benchmarks & interpretation

Compact results, honest caveats, optimization leads, and what the exercise implies from a
trading/research angle. Full methodology lives in the code (`apps/bench.cpp`, `apps/structbench.cpp`);
design rationale is in the [README decision log](../README.md).

## Setup

- **Workload:** synthetic, calibrated flow (`generator.hpp`) — GBM mid, power-law limit-order depth
  (β≈2.23, per arXiv 2606.01183), ~60/35/5 add/cancel/marketable. Seeded and reproducible.
- **Machine:** Ryzen 7600X, **WSL2** (Ubuntu on Windows). Develop/validate on macOS; measure on Linux.
- **What's reliable here:** the `structbench` ratios and `bench` *throughput* (batch-timed). What's
  **not:** `bench` per-op p50/p99 — WSL2 jitter + `std::chrono`'s ~20 ns call overhead floor and
  quantize sub-100 ns samples (values land on round numbers). Reliable per-op tails need native Linux
  + RDTSC + CPU pinning.

## Results

**Isolated structures vs `std::`** (`structbench`, 5M ops; ratio = std::/ours, >1 = ours faster):

| Structure | ours | std:: | ratio |
|---|---|---|---|
| `OrderIndex` vs `std::unordered_map` | 7.8 ns/op | 14.4 ns/op | **1.84×** |
| `PriceTree` vs `std::map` | 5.7 ns/op | 6.2 ns/op | **1.09×** |

**Whole engine** (`bench`, 2M ops, throughput is the trustworthy number):

| Backend | throughput | amortized | max (per-op) |
|---|---|---|---|
| hand-rolled | **32.1 M ops/s** | 31.2 ns/op | ~124 µs |
| `std::` | 29.8 M ops/s | 33.6 ns/op | ~352 µs |

## Interpretation

- **The hash map is a clear win (1.84×);** the AVL tree is a thin one (1.09×) — libstdc++'s
  red-black tree is well-tuned, and a pooled AVL only edges it. (`std::unordered_map` is far faster on
  libstdc++ than on macOS/libc++, which is why the isolated hash ratio fell from 4.2× to 1.84× — the
  baseline improved, not us.)
- **Micro wins shrink end-to-end:** 1.84× / 1.09× on the containers become **~8%** on the whole
  engine, because the container is only a fraction of per-message work (matching loop, intrusive DLL,
  slab pool run regardless). That is Amdahl's law, and reporting it honestly is more credible than a
  headline multiple.
- **The tail still separates them:** `std::`'s max is ~2.8× worse (~352 µs vs ~124 µs) — an
  allocation/rehash spike. Even understated by WSL2 noise, it's the slab-pool + open-addressing
  "no malloc on the hot path" decision showing up exactly where it was supposed to: the tail.

## Optimization applied — O(1) best-of-side ✓

Cached `min_`/`max_` + a price-ordered level-DLL (`prev_level`/`next_level` on each `Limit`), so
`best_ask`/`best_bid` are O(1) instead of O(log M) tree-spine walks. Best-of-side is read on every
marketable order and level boundary. Rotations don't touch price order, so the list and cached
extremes need no maintenance during rebalancing. Measured **before → after (macOS, indicative)**:

| Metric | before | after |
|---|---|---|
| `PriceTree` vs `std::map` (structbench) | 1.27× | **1.40×** |
| whole-engine hand-rolled throughput | 20.5 M ops/s | **24.0 M ops/s** (~17%) |

**Re-run on Linux pending** for the authoritative before/after (the Linux tree ratio was 1.09×).

## Further optimization (in priority order)

1. **Native Linux + RDTSC + CPU pinning (+ hugepages)** — the only way to get trustworthy p50/p99/
   p99.9. WSL2 + `chrono` can't resolve the sub-100 ns tail.
2. **`OrderIndex` capacity tuning** — the fixed 2²¹-slot (32 MB) table is sparse (~50k live); test a
   smaller capacity that still holds the working set at α≤0.5, in case the large cold footprint costs
   cache/TLB on the hot path.
3. **Pointer → `uint32` index migration** — halves reference size, ~2 orders per cache line on
   deep-queue walks. Profile-gated (the typedef hatch makes it a small change).
4. **Profile first, then optimize** — `perf` / cachegrind to find the real bottleneck rather than
   guessing; several of the above are hypotheses until a profile confirms them.

## Trading / research perspective

Building the mechanism, not just reading about it, makes several market realities concrete:

- **Latency is the product, and the tail is the risk.** The mean is a vanity metric; the p99.9/max is
  where money is lost — a 350 µs allocation spike is a missed quote, a stale price, or a risk check
  firing late at precisely the wrong instant. This is *why* HFT obsesses over deterministic
  allocation and jitter, not just average speed.
- **Queue position is a tradeable edge.** The per-level FIFO (the intrusive DLL) *is* time priority —
  join a price level early and you fill first. Market-making and passive execution live or die on
  this; the engine makes "why does my resting order's age matter" tangible.
- **Maker/taker is structural, and spread is its price.** A resting limit *provides* liquidity; a
  marketable order *takes* it and pays the spread by walking the book to progressively worse prices.
  The matching loop is that trade-off in code — the foundation of any execution/impact model.
- **Liquidity has a shape.** The power-law depth (thick at the touch, thin in the tail) that
  calibrates the workload is the same shape that governs market impact: a large order's average fill
  price degrades as it eats depth. That shape, not a single "price," is what a serious execution model
  must respect.
- **Know what your feed omits.** The LOBSTER completeness gap (a level-N *message* file can't
  reconstruct liquidity that surfaces from below) is a live caution for microstructure research:
  incomplete feeds have blind spots exactly where price discovery happens. Validate provenance before
  trusting a reconstruction.

Net: the engineering axis (data structures, cache, allocation, tail latency) and the microstructure
axis (price-time priority, maker/taker, depth, feed quality) are the same subject seen from two
sides — which is the point of building it by hand.
