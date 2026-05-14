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
