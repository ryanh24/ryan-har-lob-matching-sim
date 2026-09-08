# Limit Order Book Matching Engine (C++20)

A price-time-priority limit order book matching engine, built hand-rolled from the data structures
up — **no `std::` containers on the hot path** — then benchmarked head-to-head against `std::` and
validated against real NASDAQ ([LOBSTER](https://data.lobsterdata.com/)) data.

## Results (Ryzen 7600X, Linux)

- **37 M messages/s** through the full engine on calibrated synthetic order flow.
- **Faster than `std::`:** the hand-rolled hash index is **1.9×** `std::unordered_map` and the AVL
  price tree **1.2×** `std::map` in isolation; **~28% faster end-to-end** (same matching loop, only
  the containers swapped).
- **Bounded tail latency:** ~34 µs max vs `std::`'s ~352 µs allocation spike — the slab-pool +
  open-addressing "no malloc on the hot path" design showing up exactly where it matters, the tail.
- **A measured optimization:** making best-of-side O(1) (cached extremes + a price-ordered level
  list) lifted whole-engine throughput ~16% and the advantage over `std::` from 8% → 28%.
- **Correctness:** the matching loop is proven by asserted scenarios; the AVL and hash map are
  cross-checked against `std::set` / `std::unordered_map` over 200k–500k randomized ops.

Full numbers, methodology, and honest caveats: **[`docs/benchmarks.md`](docs/benchmarks.md)**.

## Design

Every hot-path structure is hand-written and chosen deliberately (reasoning in the decision log):

| Component | What it is | Key ops |
|---|---|---|
| `Pool<T>` | fixed slab + intrusive free list — no per-order `malloc` | alloc/free **O(1)**, deterministic |
| `OrderIndex` | open-addressing hash (linear probe, Fibonacci hash, backward-shift delete) | lookup / cancel **O(1)** |
| `PriceTree` | recursive AVL of price levels + cached best + price-ordered level list | insert **O(log M)**, best-of-side **O(1)** |
| per-level queue | intrusive doubly-linked list inside each `Order` | append / cancel-by-pointer **O(1)** |
| `Book` / `Engine` | storage vs. matching-policy split, templated on the container | — |

Prices are integers (LOBSTER's 1/10000-dollar unit); the engine is side-specialized with no virtual
calls on the matching path. `Book`/`Engine` are templated on the tree and index so the whole engine
can be benchmarked on the hand-rolled structures *or* `std::` baselines.

## Build & run

CMake + C++20:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build

# correctness
for t in matching avl index parser snapshot generator; do ./build/${t}_test; done

# benchmarks
./build/structbench 5000000       # hand-rolled structures vs std::
./build/bench 2000000 200000      # whole engine, both backends, throughput + latency
```

## Correctness against real data

`verify` replays a [LOBSTER](https://data.lobsterdata.com/info/DataSamples.php) NASDAQ message file
and diffs the reconstructed book against the reference orderbook. Sample data is **not committed**
(large + redistributable separately) — download a free sample and drop the unzipped folder in the
repo root (gitignored). Note: a level-*N* LOBSTER *message* file is top-*N*-filtered, so exact deep
reconstruction is impossible from it — `verify` reports reconstruction *fidelity* and documents why
(see the decision log). Engine correctness rests on the synthetic tests.

## Scope

Committed to **Tier B**: limit, market, and cancel orders; a second price-level container for a
head-to-head benchmark; honest benchmarking. IOC / FOK / GTC / cancel-replace are localized
matching-loop extensions (noted as remaining Tier-B breadth). **Out of scope** (Tier C, future
work): stop / stop-limit, self-trade prevention, pro-rata matching, multi-symbol, lock-free
ingestion, raw ITCH parsing. See [`docs/plan.md`](docs/plan.md) for the full scoping.

---

## Design decisions

The full reasoned log — what was decided, the alternatives weighed, and why — lives in
[`docs/design-decisions.md`](docs/design-decisions.md) (superseded entries are kept and marked, not
deleted; the history of thinking is the point). The headline choices:

- **Integer prices** (LOBSTER 1/10000-dollar unit) — equality is the engine's foundational primitive; floats break it and are slower.
- **Intrusive doubly-linked list** per price level — O(1) cancel-by-pointer, one allocation per order, list pointers on the order's cache line.
- **Hand-rolled AVL `PriceTree`** over `std::map` — pooled nodes for locality; cached extremes + a level-list make best-of-side O(1).
- **Open-addressing `OrderIndex`** over `std::unordered_map` — flat array, linear probing, Fibonacci hash, backward-shift deletion (no tombstones under heavy cancels).
- **Slab pools + intrusive free list** over `new`/`delete` — deterministic O(1) allocation, no malloc tail-latency spikes (the p99.9 killer).
- **`Book`/`Engine` split, templated on the container** — storage vs. matching policy decoupled; enables the hand-rolled-vs-`std::` head-to-head.
- **Correctness** = synthetic asserted scenarios + randomized cross-checks vs `std::`; LOBSTER replay reframed as a fidelity characterizer after finding its message-file completeness gap.
