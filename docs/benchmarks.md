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
| `OrderIndex` vs `std::unordered_map` | 7.8 ns/op | 14.7 ns/op | **1.88×** |
| `PriceTree` vs `std::map` | 5.4 ns/op | 6.3 ns/op | **1.17×** |

**Whole engine** (`bench`, 2M ops, throughput is the trustworthy number):

| Backend | throughput | amortized | max (per-op) |
|---|---|---|---|
| hand-rolled | **37.2 M ops/s** | 26.9 ns/op | ~34 µs |
| `std::` | 29.1 M ops/s | 34.4 ns/op | ~352 µs |

(Numbers above are Linux/WSL2, post-optimization — see below.)

## Interpretation

- **The hash map is a clear win (1.88×);** the AVL tree is a thin one (1.17×) — libstdc++'s
  red-black tree is well-tuned, and a pooled AVL only edges it in isolation. (`std::unordered_map` is
  far faster on libstdc++ than on macOS/libc++, which is why the isolated hash ratio is 1.88× here vs
  4× on macOS — the baseline improved, not us.)
- **Where the whole-engine advantage comes from:** before the O(1) best-of-side optimization, the
  container wins (≈1.8× / 1.1×) shrank to **~8%** end-to-end — Amdahl's law, since the container is
  only a fraction of per-message work. *After* it, the whole-engine advantage is **~28%**
  (37.2 / 29.1) — now *larger* than the isolated tree ratio (1.17×), because O(1) best-of-side pays
  off across the matching hot path (best is read on every marketable order and level boundary), not
  just in isolated tree ops. The most valuable optimization was algorithmic on the hot path, not a
  faster container.
- **The tail still separates them:** `std::`'s max is ~10× worse (~352 µs vs ~34 µs) — an
  allocation/rehash spike. It's the slab-pool + open-addressing "no malloc on the hot path" decision
  showing up exactly where it was supposed to: the tail.

## Optimization applied — O(1) best-of-side ✓

Cached `min_`/`max_` + a price-ordered level-DLL (`prev_level`/`next_level` on each `Limit`), so
`best_ask`/`best_bid` are O(1) instead of O(log M) tree-spine walks. Best-of-side is read on every
marketable order and level boundary. Rotations don't touch price order, so the list and cached
extremes need no maintenance during rebalancing. Measured **before → after**:

| Metric | before | after |
|---|---|---|
| `PriceTree` vs `std::map` (structbench, Linux) | 1.09× | **1.17×** |
| whole-engine hand-rolled throughput (Linux) | 32.1 M ops/s | **37.2 M ops/s** (~16%) |
| whole-engine advantage vs `std::` (Linux) | ~8% | **~28%** |
| (macOS cross-check: `PriceTree` ratio) | 1.27× | 1.40× |

The whole-engine advantage grew *more* than the isolated tree ratio — best-of-side is on the hot
path, so making it O(1) helped the matching loop, not just tree ops.

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
