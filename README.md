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

### 2026-06-08 — Order-ID lookup: hand-rolled open-addressing hash map (orderID → OrderRef)

- **What:** Cancel and execution messages reference an order by its LOBSTER order ID (a sparse, non-contiguous `uint64_t`). We resolve `orderID → OrderRef` through a hand-rolled flat hash map with this configuration:
  - **Open addressing** (entries live in one flat array — no per-node allocation, no pointer chasing).
  - **Linear probe sequence** (walk slot+1, slot+2 … on collision — sequential memory access, cache-friendly).
  - **Power-of-two slot count** with bit-mask indexing (`h & (N-1)`, one AND instead of a modulo), held at a **low load factor (α ≤ 0.5)**.
  - **Backward-shift deletion** — on erase, shift the trailing cluster back to fill the hole so the table stays tombstone-free.
  - **Pluggable hash function**, swappable behind a single typedef/template param. Baseline = **modulo-prime** (intentionally the slow reference point); then **Fibonacci multiply-shift**; then explore **CRC32** (hardware `_mm_crc32_u64` / `__crc32cd`).
  - Accessed only through `insert(id, ref)` / `find(id) → ref` / `erase(id)` so the backend can be replaced without touching engine code.
- **Alternatives:** `std::unordered_map` (chaining + node-per-entry + allocator-heavy — scatters cache, slow tail); `absl::flat_hash_map` (Swiss/SIMD — fast but a heavy external dependency, and "I used Google's map" is a weaker interview story than "I wrote my own"); **prime** slot count (better memory granularity but reintroduces division on every lookup); **robin-hood** insertion discipline (bounds probe variance, but its payoff is at high α — unneeded at α ≤ 0.5); **tombstone** deletion (simple, but rots under the LOB's heavy cancel rate and forces periodic rebuilds that spike p99.9).
- **Why:**
  - **Open addressing + linear over chaining:** chaining adds a pointer per element and a random-memory walk per probe; open addressing keeps everything in one dense array with sequential probes. At a sub-µs target the dominant cost is cache misses, so density wins.
  - **Power-of-two over prime (for now):** mask is ~1 cycle vs ~20–40 for a division on every lookup. The cost is coarser sizing (slot count jumps by 2×, wasting some slots) — but that waste is a few MB of cold RAM, cheap next to a per-op division. Prime's other virtue (bit-mixing that forgives a weak hash) is redundant once we mix bits in the hash function itself. **If we later raise α, revisit prime** (with Skarupke's constant-divisor switch trick to keep the modulo affordable).
  - **Backward-shift deletion over tombstones:** the engine cancels constantly, so deletion frequency is high. Tombstones accumulate, lengthen probe chains, and break the α math even when few entries are live — then need a full rebuild (a latency spike that kills p99.9). Backward-shift keeps the table self-healing with no rebuild and no spike. It is the natural deletion partner to linear probing (Knuth's in-place Algorithm 6.4R).
  - **Modulo-prime as the baseline hash:** chosen deliberately as the slow, simple reference implementation to measure improvement *from*. The Fibonacci → CRC progression then has a concrete before/after number behind it — methodology for the writeup, not premature optimization.
  - **Pointer-stability note:** the map value (`OrderRef`) stays valid for an order's whole life because the slab pool never relocates a live slot (fixed-capacity array, no compaction). We never hold references *into* the hash map across mutations — we look up, take the value, use it immediately — so the map's own rehash/relocation behavior is irrelevant, which keeps every backend (including ones that move entries) safe to drop in.
- **Open:**
  - Final hash function after the baseline → Fibonacci → CRC benchmark.
  - The α threshold at which we'd switch to prime sizing + robin-hood (the documented upgrade path).
  - Map capacity: sized from the same message-file pre-scan that sizes the pools — track running live-order count (`+1` on add, `−1` on full delete/execution), take the max, then `next_pow2(max / α)`.

---

*As this list grows, the conventional next step is to split it into one file per decision under `docs/decisions/0001-*.md` (the formal ADR pattern). Easy migration when it's needed.*
