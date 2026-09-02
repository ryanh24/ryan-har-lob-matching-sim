#pragma once
#include "lob/types.hpp"
#include "lob/book.hpp"
#include <vector>
#include <string>

// Book snapshots + LOBSTER orderbook-file oracle, and the exact diff between them.
// A Snapshot is the top-N occupied levels per side (asks ascending, bids descending) — the
// same shape our Book produces and LOBSTER's orderbook file records, so they diff directly.

namespace lob {

struct Level { Price price; Qty size; };
struct Snapshot { std::vector<Level> asks; std::vector<Level> bids; };

// Top-`levels` snapshot of the live book.
Snapshot snapshot_book(const Book& book, int levels);

// Parse a LOBSTER "_orderbook_" CSV (4*levels columns per row: askP,askV,bidP,bidV, …).
// Sentinel/empty positions (price ±9999999999, size 0) are dropped, so each row holds only
// occupied levels. Returns one Snapshot per row (aligned 1:1 with the message file).
std::vector<Snapshot> parse_orderbook(const char* path, int levels);

// Exact compare, capped at `levels` per side. Returns true if equal; on mismatch fills `msg`
// with a human-readable description of the first divergence.
bool snapshots_equal(const Snapshot& ours, const Snapshot& oracle, int levels, std::string& msg);

} // namespace lob
