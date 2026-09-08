# Design decisions

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

### 2026-06-08 — Codebase architecture: separated modules, `Book`/`Engine` split, one lib + two binaries

- **What:** The code is organized as one static library `liblob` plus two thin binaries, with a clean module separation:
  - **Public headers** under `include/lob/` (library convention): `types` (Side, `int32_t` Price, Qty, OrderId, `OrderRef`/`LimitRef` typedefs), `message` (parsed input record), `pool` (generic slab pool template), `order`, `limit`, `order_index` (the hash map), `book`, `engine`. Cold implementation in `src/*.cpp`.
  - **Book / Engine are split:** `Book` owns storage only — the two per-side Limit trees, cached best-bid/ask pointers, and the primitives (`insert_order`, `remove_order`, `best_bid/ask`, `decrement`, `delete_level`). `Engine` owns behavior — `apply(Message)` dispatch and the matching loop, calling Book primitives.
  - **Message is the seam** between parser and engine: `parser.cpp` turns LOBSTER CSV into `Message` and knows nothing about `Book`; `Engine` consumes `Message` and knows nothing about CSV. Swapping CSV for raw ITCH later touches only the parser.
  - **`snapshot.cpp`** reads `Book` (never writes) to produce top-N levels and diff against the LOBSTER oracle.
  - **Two binaries** over the shared lib: `verify` (binary B — replay + snapshot-diff → PASS/FAIL) and `bench` (binary A — time each `apply` → p50/p99/p99.9 + throughput). No TUI (Tier C / future work).
  - **Default is compiled split** (`.hpp` declares, `.cpp` defines) for clean builds and separation; the **hot `Book` primitives touched inside the matching loop are `inline` in `book.hpp`** so the storage/matching boundary carries no per-call cost on the hot path. Cold Book ops stay in `book.cpp`.
- **Alternatives:** a single combined `Book` class holding storage *and* matching (fewer files, but storage can't be tested without driving the matching path, and a second matching policy means forking the class); headers flat next to sources in `src/` instead of an `include/` tree (fine for a solo project, but the `include/lob/` convention keeps the public surface obvious and mirrors how the lib would be consumed); one monolithic binary with `--verify`/`--bench` flags instead of two.
- **Why:**
  - **Module separation** for maintenance and testability: the parser→`Message`→engine seam means each side is unit-testable in isolation and the input format is swappable without touching matching.
  - **Book/Engine split** decouples the *data structure* from the *matching policy*. It shrinks each test surface (storage primitives tested without matching; matching tested against a stub book) and makes a future pro-rata policy or multi-symbol setup a change of `Engine` / a set of `Book`s rather than surgery on one fused class. The cost — an API boundary to draw correctly, and remembering to `inline` the hot primitives so the boundary is free on the hot path — is small and understood.
  - **One lib + two thin binaries** keeps all engine logic in `liblob` (tested once) and the `main()`s as trivial wrappers, so the demos share exactly the same engine the tests exercise.
- **Open:**
  - `-fno-exceptions -fno-rtti` on `liblob` (hot-path hygiene) — intended, but deferred as a build-flag decision, not v1-blocking.
  - Exact primitive set on the `Book` API — will firm up while writing the tracer-bullet slice; risk to watch is a boundary drawn so tight that matching pokes `Book` internals (leaky abstraction).

### 2026-08-18 — Tracer-bullet internals (grill session): validation, primitives, allocation, book structure, struct fields

A single design-tree grill resolved the internals of the limit-only slice. Recorded as one entry (interrelated sub-decisions); each may split into its own ADR later.

- **1. How the engine is exercised / validated.** The **matching loop is the deliverable**, and it is driven + proven by a **synthetic order generator + hand-written crossing scenarios** (asserted fills). **LOBSTER replay runs in apply-mode** — types 1/2/3/4 applied as book deltas to validate the *data structures* against the orderbook oracle — because LOBSTER is a resting-book event log: every type-1 already rested (did not cross), and aggressors appear only implicitly as type-4 executions, so feeding LOBSTER to the matcher never fires the crossing path. Making real data drive the matcher requires **reconstructing aggressor orders** from type-4 runs; that is **parked for the final runtime benchmark**, not the tracer bullet.
  - *Alternatives:* LOBSTER-drives-matcher from day one (needs the fiddly reconstruction up front — rejected as a spine-blocker); pure synthetic only (no real-data validation — kept as a milestone instead).
  - *Why:* unblocks the spine immediately while keeping the "engine reproduces NASDAQ executions" flex as a promoted near-term goal.

- **2. Matching-loop semantics.** Crossing rule (one line, both sides): **buy crosses while `limit ≥ best_ask`; sell crosses while `limit ≤ best_bid`; a market order is that limit set to ±∞**. Fills take the **head of the level (FIFO / time priority)**; each fill is O(1). **The only AVL rebalance in the whole operation happens when a level empties (or a new price is born)** — fills never touch the tree. A residual that survives the crossing **flips to the resting side and rests at its limit** (a marketable order can be *taker and maker in one message*).

- **3. Book/Engine primitive boundary.** The matching loop (in `Engine`) speaks exactly six verbs to storage (`Book`): `best_bid()` / `best_ask()`, `head_order(level)`, `reduce(order, qty)`, `remove_order(order)`, `insert_order(...)`, and `lookup(id)` for the cancel path. The matcher re-derived this set without reaching for anything else — evidence the boundary isn't leaky. Hot verbs (`best_*`, `head_order`, `reduce`) are `inline` in `book.hpp`.

- **4. Allocation policy: stack value until it rests (Policy A).** The incoming aggressor is a **plain stack struct `{ side, price, remaining }`** while it matches; a **pool slot is allocated only if a residual survives to rest** (`insert_order` copies the three fields into a fresh pooled `Order`). A fully-filling marketable order — the hottest path — then costs **zero pool ops**; Policy B (pool every order on arrival) would waste an alloc+free (~10–20 ns) on exactly that path.
  - *Cost accepted:* two order "shapes" (in-flight stack struct vs pooled resting `Order`) and one copy at rest-time.

- **5. The book is three distinct structures, one job each.** (a) **AVL tree of `Limit`s** — finds where a *new* price splices in and stays balanced to keep that O(log M); (b) **level DLL** (`prev_level`/`next_level` on each `Limit`) — steps best → next-best in **O(1)** and gives snapshot its top-N-in-price-order walk for free; (c) **order DLL** (`prev`/`next` on each `Order`) — FIFO time priority within a level. Price lookup is the tree's job; order-by-ID lookup is the hash map's job; the two never overlap. **Priority is stored as *position*, not data** — the engine reads it off the layout, never compares it.
  - *Alternatives:* in-order successor walk for best-advancement (O(log M), leaner `Limit`, no level DLL) — rejected because the level DLL is cheap (2 refs on <1000 nodes) and snapshot needs the ordered walk anyway.

- **6. No timestamp on `Order`.** Time priority is structural (append at tail, match from head), LOBSTER messages arrive in sequence, and benchmarking times *processing* (RDTSC), never the message timestamp — so nothing on the hot path reads a time value. A timestamp is a cold field added only if reconstruction/analytics later needs it.

- **7. `Order` fields = `{ next, prev, parent, shares, id }`.** Keep **`id`** (needed to erase the order from the ID-keyed hash index on fill/cancel — the fill-sweep walks by pointer, so the key must live on the struct). **Drop price** (derive via `parent->price`; the cancel path already holds `parent`). **Drop side** (implied by which book the order lives in; matching is side-specialized). As raw pointers this is 36 B → **padded to 64 B (one cache line) for v1**; the later uint32-index migration shrinks it to ~24 B → 32 B (two orders per line, ~half the misses on deep-queue walks). Staying on pointers keeps gdb ergonomics; migrate when the profiler shows queue-walks are cache-miss-bound.

- **8. `Limit` fields = `{ parent, left, right, height, prev_level, next_level, head_order, tail_order, price, volume }`.** Maintain the **aggregated `volume` counter** (running total resting shares at the level), updated O(1) at every add/reduce/remove. Primary payoff: **snapshot reads level size in O(1)** instead of re-walking every order every message — and this helps *any* book-state read (synthetic asserts, analytics), not just LOBSTER; LOBSTER just exercises it hardest (once per message). Secondary: a cheap upfront matching branch-hint. `Limit`s are few (<~1000), so their size barely matters — the deferred `uint16` index shrink lands here later.

- **Open (next grill):** synthetic generator design (distribution, guaranteeing crossing flow); snapshot/diff mechanics (top-N depth, tolerance, cadence); pool capacity sizing from the message-file pre-scan; cancel / partial-cancel (type 2 vs 3) semantics (`reduce` vs `remove_order`).

### 2026-08-18 — Tracer-bullet internals, part 2 (grill session): cancel semantics, verify/bench split, pool sizing, generator

Second grill closing the four branches left open above. Interrelated; may split into ADRs later.

- **1. Cancel semantics — type 3 vs type 2.** **Type 3 (total delete)** → `remove_order`: unlink from the order DLL, subtract remaining shares from `level.volume`, erase from the hash index by `id`, free to pool; delete the level + rebalance + advance best pointer if it emptied. **Type 2 (partial cancel)** → in-place `reduce(order, msg.size)`: `order.shares -= msg.size`, `level.volume -= msg.size`, **keep queue position (time priority preserved)**. LOBSTER's type-2 `size` field is the shares *canceled* (a delta), not the new size. Defensive: a `lookup(id)` miss is skipped + counted (not fatal); a type-2 that would hit 0 shares is treated as a removal.
  - *Why priority is preserved:* reducing size keeps you in the same queue; only a reprice (different level, tail of a different queue) or a size *increase* resets the clock — the same principle as the logged cancel-replace decision. **Only size reduction is priority-preserving.**

- **2. Correctness checking is `verify`-only, off the measured path.** The snapshot + oracle-diff is a **development/test** apparatus, not something the engine does in production (real-time has no oracle to check against, and any checking left in would measure *engine + checker*). So: **`verify`** replays the message file and, after every message, snapshots the top-N and exact-diffs against the LOBSTER orderbook row — **per-message, exact integer equality, flag-and-stop on the first divergence** (everything downstream of a divergence is just its echo). **`bench`** replays the same file and does **nothing but time `apply()`** — no snapshot, no diff. Crucially, **`Engine::apply()` contains no verification code**; the checking lives in the `verify` harness that *wraps* the engine, so it contributes exactly zero to the benchmark. Message-type filter for the visible-book reconstruction: apply **1/2/3/4**, **skip 5** (hidden orders aren't in the visible book) and **7** (halt).

- **3. Pool sizing — fixed generous capacities, panic on exhaustion.** `Order` pool = 2²⁰ (~1M slots, ~64 MB), `Limit` pool = 2¹⁶ (~64k), both powers of two. **No dynamic growth** — a mid-run realloc would move the arena and invalidate every live pointer (the thing the slab pool exists to avoid); instead size once at startup and **panic loudly** if exceeded. This mirrors production: engines pre-allocate a fixed arena from historical worst-case and reject/halt rather than grow. (A message-file pre-scan to size from actual peak-live orders is a later refinement, deferred; fixed caps get us to running code now.)

- **4. Synthetic order generator — a seeded, knob-driven experimental instrument.** Two distinct things: **(a) hand-written correctness scenarios** — specific deterministic cases with asserted fills (partial fill, multi-level sweep, exhaust-limit-then-rest, empty-level rebalance, cancel-from-middle) — this is what actually proves price-time priority, since LOBSTER can't fire the matcher. **(b) A load generator** for benchmarking: a **seeded Monte-Carlo random-walk mid-price** with resting adds clustered around the drifting touch and marketable orders that cross *by construction* (the generator knows its own best bid/ask). **Knobs:** `%add / %cancel / %marketable`, price spread, size distribution, arrival frequencies. Each emitted order is **tagged with its op type** so the bench harness reports **per-operation** latency distributions. Two profiles from the same generator: a **realistic** ~95% add+cancel profile (headline throughput) and a **matching-heavy** profile (labeled stress test, for probing p99/p99.9 and measuring the cost of the matching loop vs a plain add). Seed is fixed so knob-vs-knob comparisons differ only by the knob. Realism guardrail: keep steps/dispersion in plausible tick ranges; never quote the stress profile's numbers as real-market throughput. Sweeping the knobs yields the Tier-B writeup curves (latency vs depth, latency vs mix).

### 2026-09-02 — LOBSTER verify reframed as a fidelity characterizer (message-file completeness gap)

- **What:** `verify` seeds the book from the first orderbook snapshot and replays the message file, but it does **not** pass/fail. It reports how deep exact top-N reconstruction holds and for how long — a fidelity curve. Correctness of the engine itself rests on the synthetic `matching_test`; the benchmark workload is synthetic-and-calibrated (see next steps).
- **The finding (empirical):** a level-N LOBSTER **message file is filtered to top-N-affecting events**. An order placed while its price sits *deeper* than level N generates **no message**; when the book later shifts and that price surfaces into the top-N, the orderbook file shows liquidity with **no message trail**. Proven at price `5875000` in the AAPL sample: the message file adds **10** shares there, yet the oracle shows **394**. So exact top-N reconstruction *from the message file alone is impossible* — the **input is incomplete**, not the engine (an infinite-level engine couldn't do it either).
- **Alternatives:** (a) full-depth raw ITCH — a complete feed that would make reconstruction exact, but out of scope (paid / not LOBSTER's level-limited output); (b) read the orderbook file directly as book state (no reconstruction) — fine for research, but doesn't exercise the engine; (c) **seed + best-effort replay + characterize fidelity** — chosen.
- **Field validation:** the two "lobster"-named repos surveyed ([rubik/lobster](https://github.com/rubik/lobster), [DylanBT928/lobster](https://github.com/DylanBT928/lobster)) are matching engines that don't replay LOBSTER at all; a matching-engine benchmark paper ([arXiv 2606.01183](https://arxiv.org/html/2606.01183v6)) stresses the matcher with **synthetic bursts calibrated to real statistics** (power-law depth, geometric-Brownian-motion prices), *explicitly excluding real-market-data complexity*. The field tests matchers on calibrated synthetics, not LOBSTER replay — so this is the standard call, not a compromise.
- **Result:** the top-of-book reconstructs exactly for ~8.8% of the AAPL file (~7,600 messages) before the completeness gap surfaces; fidelity falls off with depth. The best-effort phantom-reduce handles *seeded* pre-existing cancels but is unsound for *deep-surfaced* orders — an accepted limitation, since the two can't be told apart from the filtered feed.

### 2026-09-02 — Templated `Book`/`Engine` on the container (whole-engine head-to-head)

- **What:** `Book` and `Engine` are now templates parameterized on the price-level tree and the order-id index. `Book = BookT<PriceTree, OrderIndex>` is the real engine; `BookStd = BookT<StdTree, StdIndex>` is a `std::map`/`std::unordered_map` baseline; `Engine = EngineT<Book>`. `bench` runs the *same* workload through the *same* matching loop on both backends — a whole-engine comparison, not just isolated structures.
- **Supersedes** the hot/cold `.hpp`/`.cpp` split from the 2026-08-18 architecture entry: a template must be header-only, so all `Book`/`Engine` methods are now inline in the headers (`book.cpp`/`engine.cpp` removed). Header-only is fine here — it only helps inlining, and the build-time cost is negligible at this size.
- **Why:** to measure the whole engine hand-rolled-vs-`std::` for real p99.9 tails, which the isolated `structbench` can't show. Result (macOS, indicative): **~20.5M ops/s vs ~11.1M ops/s (~1.8×)**, and — the headline — `std::` shows a **~2 ms max** latency outlier (an allocation/rehash spike) against the hand-rolled engine's **~61 µs** max. That is the slab-pool + open-addressing "no malloc tail" decision validated empirically in the tail.
- **Open:** structbench keeps its own local `std::` adapters; could be de-duplicated against `std_containers.hpp` later (cosmetic).

---

*As this list grows, the conventional next step is to split it into one file per decision under `docs/decisions/0001-*.md` (the formal ADR pattern). Easy migration when it's needed.*
