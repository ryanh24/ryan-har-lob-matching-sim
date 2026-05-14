# LOB Matching Engine — Research & Scoping Plan

## Context

Goal: build a price-time-priority limit order book (LOB) matching engine in C++ as an HFT-credentialing project — the kind of thing that converts a math major with a C++ certificate into a credible quant dev candidate.

But before writing any C++, you need a working mental model of:
1. **How an order book actually behaves** at the message level (you can't implement what you can't simulate in your head).
2. **The design space** — the interesting decisions the project is famous for: data structures, memory layout, cache discipline.
3. **What the real benchmarks mean** — "sub-microsecond per message" is meaningless without methodology.
4. **What scope is honest** — a serious project, not a toy; not a multi-quarter rewrite of CME either.

This file is the **study guide and scope-thinking document**. The actual implementation plan gets built in a `/grill-me` session after you've absorbed this.

---

## Part 1 — LOB mechanics: what you must internalize

### The object

An order book is two sorted queues, one per side:
- **Bid side** — buy orders, sorted descending (best bid = highest)
- **Ask side** — sell orders, sorted ascending (best ask = lowest)
- **Spread** = ask − bid; **mid** = (ask + bid) / 2; the book is "crossed" if bid ≥ ask (only momentarily, during matching)

Each side is a **stack of price levels**. Each price level is a **FIFO queue of orders** sitting at that price. So the book is conceptually: `side → price → time-ordered queue of orders`.

### Order lifecycle

Three primitives drive everything:
- **Add** — new resting order arrives, goes to back of its price-level queue.
- **Cancel** — order is removed by ID (somewhere in the middle of its queue).
- **Execute / match** — incoming aggressive order consumes resting orders at the front of opposing queues until filled or its limit price is exhausted.

Per the canonical reference (WK Selph, "How to Build a Fast LOB"), good implementations hit these complexities:

| Operation | Target | Notes |
|---|---|---|
| Add (new price level) | O(log M) | M = active price levels (M ≪ N orders) |
| Add (existing price level) | O(1) | append to tail of dlist |
| Cancel by orderID | O(1) | hashmap orderID → Order, dlist unlink |
| Execute / match-step | O(1) | always head of best-price queue |
| Best bid / best ask | O(1) | cached pointer maintained on insert/delete |
| Volume at price | O(1) | aggregated counter on each Limit |

The vast majority of real-market traffic is **add and cancel** (market makers re-quoting), with sparse matches. Optimize for that.

### Order types (decide which ones you'll support)

- **Limit order** — rests on book until matched or canceled. The default.
- **Market order** — equivalent to a limit order with price = ±∞; sweeps the book. Many implementations model it as exactly that.
- **Cancel** — remove a resting order by ID.
- **Modify / Cancel-Replace** — usually decomposed into cancel + add, but losing time priority on the new order.
- **IOC (Immediate-or-Cancel)** — match what you can right now, kill the rest.
- **FOK (Fill-or-Kill)** — match everything immediately or reject the whole order.
- **GTC (Good-Till-Cancel)** — the default for resting limits.
- **Stop / Stop-Limit** — triggered orders; require a separate "stop book" and a trigger event mechanism. **Likely out of scope** for v1.

### Edge cases that trip up first-time implementers

- **Self-matching / crossed book** — should two orders from the same participant fill each other? Real venues offer self-trade-prevention modes. For a sim, you can ignore unless you want to model it.
- **Partial fills** — incoming order partially eats a resting order. Resting order's `shares` decrements; it stays in queue. Incoming continues to next resting order or next price level. Eventually either incoming is exhausted, OR its limit price is hit (rest goes on book), OR (for IOC) the rest is canceled.
- **Price-level deletion** — when the last order at a level is removed (filled or canceled), the Limit object goes away; if it was the best bid/ask, update the cached pointer.
- **Tick size** — minimum price increment. Picking a tick size and an integer price representation (e.g., price in 1/100 of a cent, à la LOBSTER) avoids all floating-point comparison nightmares.

---

## Part 2 — The design space (the interesting decisions)

This is where the project earns its résumé credibility. There's a small number of well-known design axes; getting opinionated about each is the whole point.

### Decision 1 — Price-level container

The book needs: ordered iteration of price levels, O(1) best-of-side, O(log M) or better insert/delete.

Options:
- **Balanced BST (AVL / red-black) of Limit nodes** — classic Selph design. O(log M) insert/delete, O(1) best via cached pointer. `std::map` is the lazy version (slow, allocator-heavy); a hand-rolled AVL is the serious version.
- **Sorted skiplist** — similar complexity, simpler concurrency story.
- **Sorted array / vector of price levels** — O(log M) search, O(M) insert in worst case, but tiny M (often a few hundred active levels) means memory locality wins. Worth measuring.
- **Direct-mapped array indexed by price-in-ticks** — if price range is bounded (e.g., a stock at $100 with ±$50 range and $0.01 ticks = 10000 slots), use a flat array. O(1) for everything. Memory cost is the tradeoff; this is what many production systems actually do because cache > algorithms.

> The serious-credibility version of this project tries at least two and benchmarks them. The interview question is "why did you choose X over Y" — and the answer is the latency histogram.

### Decision 2 — Order container within a price level

Almost universally: **intrusive doubly-linked list**. Each Order struct contains its own `prev`/`next` pointers. Why intrusive:
- One allocation per Order, not two (no separate list-node).
- Cancel-by-pointer is O(1) unlink — no search.
- Same cache line holds order data + list pointers.

### Decision 3 — Order lookup by ID

You need to cancel orders by ID in O(1). That means a **hashmap orderID → Order\***. Either `std::unordered_map` (fine but allocator-heavy) or an open-addressing flat hashmap (e.g., a hand-rolled one or `absl::flat_hash_map` in the C++ world).

### Decision 4 — Memory management

`new`/`delete` per order will dominate your latency budget at sub-µs targets. Standard moves:
- **Object pool / slab allocator** — pre-allocate a big array of Order/Limit slots; allocate from a free-list. Allocation = O(1) pop, deallocate = O(1) push. No fragmentation.
- **Arena per session** — if you process a finite stream, just bump-allocate and free at end.
- Avoid virtual functions, RTTI, exceptions in the hot path (`-fno-exceptions -fno-rtti` is a known flag combo for matching engines).

### Decision 5 — Cache discipline

- Pack Order struct to one or two cache lines (64 B each on x86-64).
- Keep the best-bid / best-ask pointer hot — most messages touch it.
- Side-specialize matching (two functions, no `if (side == BID)` branches).
- Sequential layout of Order slots in the pool helps when the queue is iterated.

### Decision 6 — Integer prices

Always. Floats kill you on both correctness (equality) and speed. Pick a unit (LOBSTER uses 1/10000 of a dollar — i.e., 1/100 of a cent). Store prices as `int32_t` or `int64_t`.

---

## Part 3 — What real implementations look like (reading list)

Sorted from most foundational to most ambitious. **You don't need to read all of these** — pick the canonical reference, plus one or two repos you'll model your scope on.

### Canonical reference

- **WK Selph, "How to Build a Fast Limit Order Book"** — the foundational blog post. The original is gone, but mirrored at `quantcup.org/home/howtohft_howtobuildafastlimitorderbook` and recapped in the [Crypto-toolbox/HFT-Orderbook](https://github.com/Crypto-toolbox/HFT-Orderbook) README. Read this first. Everything below is a variation on this design.

### Open-source reference implementations (in rough order of usefulness)

- **[brprojects/Limit-Order-Book](https://github.com/brprojects/Limit-Order-Book)** — Selph design in modern C++, with benchmarks. Reports ~1.4M tx/s, ~713 ns avg latency. Good scope-comparable target.
- **[ajtulloch/quantcup-orderbook](https://github.com/ajtulloch/quantcup-orderbook)** — C++ adaptation of the original QuantCup winner. Uses `boost::intrusive`. Small, readable.
- **[enewhuis/liquibook](https://github.com/enewhuis/liquibook)** — production-grade open-source matching engine (the "blazing fast" one). Bigger than your project needs to be, but worth skimming for what a mature codebase looks like.
- **[jellepelgrims.com/posts/matching_engines](https://jellepelgrims.com/posts/matching_engines)** — clean conceptual walkthrough with pseudocode.

### Background

- **["How Order Matching Engines Process Trades"](https://www.quantvps.com/blog/order-matching-engines)** — matching mechanics, in English.
- **[Coinbase Exchange Matching Engine docs](https://docs.cdp.coinbase.com/exchange/concepts/matching-engine)** — real-venue order type semantics, including STP modes. Useful for understanding the spec side.
- **["Intrusive Design for Low-Latency Trading Systems"](https://www.research.hangukquant.com/p/intrusive-design-for-low-latency)** — why intrusive containers matter.

---

## Part 4 — LOBSTER data: what it is, how to use it

[LOBSTER](https://data.lobsterdata.com/info/DataStructure.php) reconstructs the NASDAQ order book from NASDAQ's TotalView-ITCH feed and gives you per-message CSV files. Free samples for a handful of tickers / days are at [`/info/DataSamples.php`](https://data.lobsterdata.com/info/DataSamples.php).

### File format

Two files per ticker-day, both CSV. Naming: `TICKER_DATE_34200000_57600000_filetype_LEVEL.csv` (34200000–57600000 ms = market hours 09:30–16:00).

**Message file columns** (the input to your engine):
1. Time — seconds after midnight, ms+ precision
2. Type — 1=new limit, 2=partial cancel, 3=total delete, 4=execution (visible), 5=execution (hidden), 7=halt
3. Order ID
4. Size (shares)
5. Price (in 1/10000 of a dollar — integer)
6. Direction — 1=buy, -1=sell

**Orderbook file** — the reference book state after each message, to the top N levels. Use this as your **oracle**: replay messages into your engine, snapshot your book at each step, diff against LOBSTER's snapshot. If they match, you're correct. This is the single best correctness test.

### What this gives you

- A real workload of millions of messages per day.
- Built-in regression test (snapshot diff).
- Realistic distributions (you can't make up a workload this representative).

---

## Part 5 — Benchmarking, honestly

"Sub-microsecond per message" is a claim, not a number. What you actually want to report:

1. **Per-operation latency distributions** (p50 / p90 / p99 / p99.9 / max), separately for add, cancel, match. The p99 is the interesting number — averages lie.
2. **Throughput** — messages per second when fed a continuous stream.
3. **Workload composition** — % adds vs cancels vs matches. Real markets are ~95% add+cancel.

### Tools and methodology

- **Timing**: on x86-64 Linux, use `__rdtsc()` for near-zero-overhead cycle counts (see ["A Systems Engineer's Guide to Benchmarking with RDTSC"](https://blog.codingconfessions.com/p/rdtsc)). On Apple Silicon / ARM, the equivalent is reading `CNTVCT_EL0` via `mrs`. `std::chrono::steady_clock` is the portable fallback but coarser.
- **Histograms**: HdrHistogram (C++ port: `HdrHistogram_c`) for lossless latency recording.
- **CPU isolation**: pin the engine thread, disable frequency scaling, use `taskset` / `isolcpus`. macOS makes this much harder — see "Platform note" below.
- **Warm up** before measuring. Discard the first N thousand messages.
- **Beware**: timing each individual op adds overhead. Better to time batches and divide, or use a sampling timer.

### Platform note

You're on macOS (Darwin). For *serious* low-latency benchmarking, you'll eventually want Linux:
- RDTSC, hugepages, `mlock`, `SCHED_FIFO`, NUMA pinning — all Linux.
- macOS scheduler is non-deterministic for sub-µs work; Apple Silicon has different timing semantics than Intel.

**Practical path**: develop and validate correctness on macOS; run benchmarks on a Linux box (cloud VM, container, or — if available — a bare-metal box). Document this honestly in the writeup; it's how real teams work.

---

## Part 6 — Scope options ("what you should be building")

Three honest tiers. Pick after the grill-me session.

### Tier A — MVP (~2–3 weeks of focused work)

- Limit + market + cancel only
- AVL-tree-of-Limits + intrusive dlist (Selph design)
- Slab pool allocator
- LOBSTER replay with snapshot-diff correctness test
- Latency histograms (HdrHistogram), reported per op
- Throughput on one real ticker-day
- Writeup: data structure choice and what the numbers show

This is already a credible HFT project. You can stop here.

### Tier B — Serious (~5–8 weeks)

Everything in A, plus:
- IOC and FOK
- Cancel-replace
- A second price-level container (sorted array or direct-mapped) and a head-to-head benchmark
- Linux benchmarking with RDTSC, CPU pinning, hugepages
- p50/p99/p99.9/max reported per op, plus throughput at varying mix
- Writeup with charts: latency vs. book depth, latency vs. workload mix

This is the version that gets you interviews.

### Tier C — Ambitious (months)

Everything in B, plus:
- Stop / stop-limit orders (separate trigger book)
- Self-trade-prevention modes
- Pro-rata matching algorithm option (alongside FIFO)
- Multi-symbol support (book-per-symbol)
- Lock-free SPSC queue for ingestion
- ITCH parser straight from binary (skip LOBSTER's CSV layer)
- Memory layout tuning: false-sharing analysis, padding, prefetch hints

Probably overscoped for a single project. Note these as "future work" in the writeup.

---

## Part 7 — Pre-grill-me checklist

Before you start the `/grill-me` session, you should be able to answer cold:

1. **State the matching loop** in pseudocode, including what happens when an incoming limit order partially fills.
2. **Name your top-level data structures** and what each costs for add / cancel / match.
3. **Justify integer prices** over floats in one sentence.
4. **Explain why intrusive dlists** instead of `std::list`.
5. **Pick your price-level container** (or pick two to compare) and say why.
6. **Pick your tier** (A / B / C above).
7. **Pick your benchmarking host** (macOS local, Linux VM, both?).
8. **Pick your correctness test** (LOBSTER snapshot diff is the obvious answer).

Open questions worth bringing into grill-me:
- Single-threaded engine, or producer/matcher split with an SPSC queue?
- Do you write a full ITCH parser, or stick with LOBSTER CSVs?
- One symbol, or N symbols (each with its own book)?
- Build system: CMake, plain Makefile, or Bazel?
- What's the demo? CLI replay tool? TUI book viewer? Just a benchmark binary?

---

## Verification — how you know this plan worked

This is a research plan, not an implementation plan, so "verification" means: you can walk into `/grill-me` and not get stuck on the basics. Concretely:

- You can sketch the LOB data structure on a whiteboard without notes.
- You can explain price-time priority in two sentences.
- You've skimmed at least one open-source implementation end-to-end.
- You've downloaded one LOBSTER sample file and looked at it in a spreadsheet.
- You've picked a scope tier you're willing to defend.

When those are all true, run `/grill-me` and design the implementation plan.
