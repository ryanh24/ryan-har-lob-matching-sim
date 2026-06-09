# Memory management — optimization ideas to revisit

Catalogue of memory-density and allocator optimizations that go *beyond* the baseline decision (slab pool + intrusive free list + pointer-based references) recorded in the README.

**Most should NOT be done preemptively.** The v1 MVP commits to the baseline only. Most ideas here are profile-driven — wait until benchmarks show where the bottleneck actually lives.

Two exceptions are baked into the design from day one (already in the README decision entry):
- Power-of-2 struct sizes with `alignas(64)`.
- Grouping hot fields at the top of `Order` / `Limit` so a future hot/cold split is cheap.

---

## 1. Smaller reference types

References within `Order` and `Limit` (currently `Order*` / `Limit*`) can shrink past `uint32_t`.

**`uint16_t` per-pool sizing.** The Limit pool has < 1000 active price levels even on the busiest tickers — `uint16_t` (max 65k) is plenty, and halves all five Limit-internal references (`parent`, `leftChild`, `rightChild`, `headOrder`, `tailOrder`) again. Order pool stays at `uint32_t` to be safe; tens of thousands live at peak.

**Bitfields.** `uint32_t prev : 24;` gives a 24-bit index (16M slots), saving a byte per reference. Compiler emits masking — ~1–2 extra cycles per access. Only worth it to squeeze a struct into a specific cache-line budget.

**Tagged handles.** Reserve top bits of a 32-bit index for metadata: generation counter (catches use-after-free), side bit, pool ID for multi-pool designs. Costs masking on every deref; buys bug safety or side-channel info.

---

## 2. Struct layout (the biggest wins)

Most LOB engines spend their cache-optimization effort here, not on reference encoding.

**Hot/cold field splitting.** Split `Order` (and `Limit`) into two parallel arrays indexed by the same slot:

- Hot (touched every message): `prev`, `next`, `parent`, `price`, `shares`
- Cold (touched on insert / cancel / analytics only): `id`, `timestamp`, `entry_time`

```cpp
struct OrderHot  { OrderRef prev, next; LimitRef parent; int32_t price; uint32_t shares; };
struct OrderCold { uint64_t id; int64_t timestamp; /* ... */ };

OrderHot  hot_pool[N];
OrderCold cold_pool[N];   // parallel indexing — same slot number
```

Matching loop touches only `hot_pool` — effectively doubles orders-per-cache-line on the hot path. Single highest-impact optimization in the toolkit. **Design for it from day one** (group hot fields at top of struct), even if you don't implement the split yet.

**Struct-of-Arrays (SoA).** Take it further: `prev[]`, `next[]`, `price[]` as fully separate parallel arrays. Sequential scans (e.g., volume analytics) hit dense cache lines. Cost: random-access-by-slot becomes scattered across N arrays. For matching engines, usually too far — matching is constant random-access. Hot/cold splitting is the sweet spot.

**Power-of-2 struct sizes.** Pad `Order` to 32 or 64 bytes. Then `pool[i]` compiles to `pool_base + (i << shift)` — shift instead of multiply, one cycle saved per deref. Compiler does this when size is a power of 2; deliberate padding ensures it. **Baked into v1.**

**Cache-line alignment.** `alignas(64) Order pool[N];` — pool starts on a cache line boundary, no `Order` straddles two lines. **Baked into v1.**

---

## 3. Backing memory

How the pool's underlying memory is allocated — separate from its internal layout.

**Hugepages.** A 1M-slot pool at 64 B/slot = 64 MB. At 4 KB pages that's 16,384 entries — the TLB caches a few hundred at a time, so cross-page accesses are TLB misses (~30 ns each). Switch to 2 MB hugepages: 32 entries cover the whole pool, basically always TLB-resident. Linux only: `madvise(MADV_HUGEPAGE)` or `mmap(MAP_HUGETLB)`. Free upgrade on the bench host.

**NUMA pinning.** Multi-socket servers attach memory to specific CPU sockets; cross-socket access is ~3× slower. `numa_alloc_onnode()` pins the pool to the matcher thread's node. Matters on real servers, not laptops.

**Prefetching.** When walking a price-level dlist, `__builtin_prefetch(o->next, 0, 3)` two iterations ahead hides load latency. Real but profile-driven — speculative prefetching can pollute cache and make things worse. Wait for data showing queue-walk is cache-miss-bound.

**Base address in a hot register.** `static thread_local` for the pool base lets the compiler hoist it. Microscopic but real on the hot path.

---

## When to do which

| Optimization | Pre-MVP? | Why |
|---|---|---|
| `uint16_t` for Limit pool refs | When migrating pointers → indices | Free, no complexity |
| Power-of-2 struct sizes + `alignas(64)` | **Yes — baked into v1** | One line, structural |
| Group hot fields at top of struct | **Yes — baked into v1** | Free; makes future split cheap |
| Hot/cold parallel-array split | Maybe — design for it | Adds machinery; revisit after first profile |
| Hugepages on Linux bench host | When setting up the bench box | Config flag, zero code |
| Bitfield-packed refs | No | Needs profile justification |
| Prefetching | No | Speculative — needs miss data |
| NUMA pinning | No | Only multi-socket servers |
| Full SoA | No | Hurts random-access patterns |

The two `Yes` rows are recorded in the README decision entry. The rest stay here until benchmarks justify them.
