# ryan-har-lob-matching-sim

A price-time-priority limit order book matching engine in C++.

## Status

**Research and design phase.** No engine code yet. Current work:

1. Read foundational material on order book mechanics and existing implementations.
2. Pick a scope tier (see [`docs/plan.md`](docs/plan.md)).
3. Design the data structures via a grilling session before any code is written.

## Goal

Build a credible HFT-style matching engine:

- Limit, market, and cancel orders (minimum).
- Sub-microsecond per-message latency target.
- Correctness validated against [LOBSTER](https://data.lobsterdata.com/) NASDAQ data via snapshot-diff replay.

## Plan

See [`docs/plan.md`](docs/plan.md) — the research and scoping plan, including reading list, design space, scope tiers, and pre-design checklist.

## Build

Not yet — placeholder. Likely CMake + C++20. Benchmarks will require Linux (RDTSC, CPU pinning, hugepages); correctness work can be done on macOS.

---

## Design decisions

A running log of the design choices made and the reasoning. New entries are appended; **superseded entries are kept and marked, not deleted**, so the *history of thinking* stays visible — not just the current state.

Per entry: **what** was decided, **alternatives** considered, **why** this one won, optionally **open** sub-questions deferred to later.

### 2026-05-14 — Doubly-linked intrusive list inside each price level

- **What:** Orders at the same price level are held in a doubly-linked list whose `prev`/`next` pointers live inside the `Order` struct itself (intrusive).
- **Alternatives:** `std::list` (non-intrusive), `std::deque`, `std::vector` with manual index management.
- **Why:** Intrusive halves the allocations per order, puts list pointers on the same cache line as order data, and gives O(1) cancel-by-pointer with no search. `std::list` allocates a separate list-node per element, doubling memory traffic and scattering cache locations. Universal answer in credible LOB implementations (WK Selph, brprojects, quantcup-orderbook).

### 2026-05-14 — Integer prices in LOBSTER's 1/10000-dollar unit (`int32_t`)

- **What:** Prices stored as `int32_t` in units of 1/10000 of a dollar — matching LOBSTER's NASDAQ encoding exactly.
- **Alternatives:** `double`, `int64_t` with a different scale, a fixed-point wrapper class.
- **Why:** Floats break equality comparisons (the foundational primitive in a matching engine) and are slower than integer compare. Matching LOBSTER's unit means message replay needs no conversion. `int32_t` covers prices up to $214,748.36 per share, fine for all NASDAQ-listed equities except Berkshire-A. Swap in `int64_t` if extending to that or to commodities.

### 2026-05-14 — Scope: Tier B, no stop or stop-limit orders

- **What:** Support limit, market, cancel, modify/cancel-replace, IOC, FOK, GTC. Skip stop and stop-limit.
- **Alternatives:** Tier A (limit + market + cancel only — MVP); Tier C (full feature set including stops, self-trade prevention, pro-rata, multi-symbol).
- **Why:** Stop orders need a separate trigger book and an event mechanism (last-trade prints fire stops into the active book) — a multi-week addition on top of the core matching engine. A CLOB without stops is already a complete artifact for a credentialing project. Tier C is "ambitious-becomes-abandoned" territory for a single-person project.
- **Note:** Cancel-replace is implemented internally as cancel + add (losing time priority, which is the correct semantics), not as a third top-level operation.

### 2026-05-14 — First implementation slice: limit orders only, end-to-end

- **What:** Build the full pipeline (parser → engine → snapshot → LOBSTER diff → latency histogram) with only limit-order add/cancel/match. Add other order types only after the spine works.
- **Alternatives:** Implement all order types up front; or implement the matching engine in isolation before any LOBSTER plumbing.
- **Why:** Tracer-bullet development. The hardest engineering is wiring the full pipeline; doing that wiring with the simplest order type means none of it is blocked on matching-loop edge cases. Once the spine works, market / IOC / FOK / cancel-replace are localized changes to the matching loop and the order struct — not architectural surgery.

### 2026-05-14 — Price-level container: hand-rolled AVL tree of Limit nodes (direction committed)

- **What:** Price levels are held in a hand-rolled balanced BST (AVL), one tree per side. Best-bid and best-ask are tracked via cached pointers maintained on insert/delete.
- **Alternatives:** `std::map`, sorted skiplist, sorted `std::vector` of Limits, direct-mapped flat array indexed by price-in-ticks.
- **Why:** AVL gives O(log M) insert/delete with O(1) best-of-side via cached pointers. `std::map` works but heap-allocates each node, killing cache locality. Skiplist has similar complexity but is harder to make cache-friendly in a single-threaded engine. Sorted vector and direct-mapped array are interesting enough to **benchmark against** AVL — that head-to-head is part of the project's interview-talking-point value.
- **Open:** which alternative container to build for the head-to-head; how to size the node pool. Deferred to the design session.

### 2026-05-14 — Memory management: slab pool with intrusive free list; pointers first, indices later

- **What:** `Order` and `Limit` objects are drawn from fixed-size, pre-allocated slab pools. Each pool maintains an intrusive free list — free slots reuse their own bytes to chain into the next free slot, so no separate "free-slot tracker" storage is needed. References between objects (`prev`, `next`, `parent`, tree-child pointers, etc.) are raw `Order*` / `Limit*` pointers in v1, hidden behind `using OrderRef = Order*;` / `using LimitRef = Limit*;` typedefs in a single header. Migrating later to `uint32_t` pool indices is then a one-line typedef swap + helpers, not a sweeping refactor.
- **Alternatives:** `new`/`delete` (deferred-optimization route); `std::pmr::pool_resource` (stdlib middle ground); pure bump arena (can't reclaim out-of-order — non-starter once orders cancel or fill).
- **Why:**
  - **Slab pool over `new`/`delete`:** at the sub-µs target, allocation must be O(1) deterministic at ~5–10 ns. malloc's *tail* variance (occasional thousands-of-ns spikes when its thread-local cache misses) is what kills p99.9 latency, regardless of how good the average is. Pool removes that whole tail class.
  - **Slab pool over `std::pmr`:** pmr is fine ergonomically and ~2–3× slower in the hot path. The pool is ~30 lines per type — writing it is far cheaper than revisiting allocation later when it shows up at the top of the profile.
  - **Pointers first over indices:** debuggability. gdb shows a real address; `p *prev` works directly. The cache-density gain from `uint32_t` indices is real but modest (~10–20% on tree-heavy workloads), and only worth taking once benchmarks point at it. The typedef wrapper means deferring is one line of code, not technical debt.
- **Open:**
  - Pool capacity: for LOBSTER replay, pre-count orders in the message file and size accordingly; v1 panics on exhaustion.
  - Whether to use `uint16_t` indices for the `Limit` pool when migrating (active price levels are typically < 1000, so 16 bits is plenty and halves Limit-internal reference size again).
  - Hot/cold field splitting, hugepages, NUMA pinning, cache-line alignment, prefetching — all deferred until first benchmark shows where the bottleneck actually lives.

---

*As this list grows, the conventional next step is to split it into one file per decision under `docs/decisions/0001-*.md` (the formal ADR pattern). Easy migration when it's needed.*
