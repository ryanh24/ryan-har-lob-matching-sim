#include "lob/book.hpp"
#include "lob/snapshot.hpp"
#include <cstdio>
#include <fstream>
#include <string>

using namespace lob;

static int failures = 0;
static void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL %s\n", what); ++failures; }
}

int main() {
    // Build a small book directly (no matching): asks at $10.00/$10.01, bids at $9.99/$9.98.
    Book book;
    book.insert_order(Side::Sell, 100000, 100, 1);
    book.insert_order(Side::Sell, 100100, 200, 2);
    book.insert_order(Side::Buy,   99900, 150, 3);
    book.insert_order(Side::Buy,   99800, 300, 4);

    Snapshot s = snapshot_book(book, 30);
    check(s.asks.size() == 2 && s.bids.size() == 2, "snapshot has 2 levels per side");
    check(s.asks[0].price == 100000 && s.asks[0].size == 100, "best ask = $10.00/100 (ascending)");
    check(s.asks[1].price == 100100 && s.asks[1].size == 200, "2nd ask = $10.01/200");
    check(s.bids[0].price ==  99900 && s.bids[0].size == 150, "best bid = $9.99/150 (descending)");
    check(s.bids[1].price ==  99800 && s.bids[1].size == 300, "2nd bid = $9.98/300");

    // Parse a 2-level orderbook fixture; level 2 is a sentinel (empty) and must be dropped.
    const char* path = "orderbook_fixture.csv";
    {
        std::ofstream f(path);
        // askP1,askV1,bidP1,bidV1, askP2,askV2,bidP2,bidV2   (level 2 = dummy/empty)
        f << "100000,100,99900,150,9999999999,0,-9999999999,0\n";
    }
    std::vector<Snapshot> rows = parse_orderbook(path, 2);
    std::remove(path);
    check(rows.size() == 1, "parsed 1 orderbook row");
    check(rows[0].asks.size() == 1 && rows[0].bids.size() == 1, "sentinel level dropped → 1 occupied per side");
    check(rows[0].asks[0].price == 100000 && rows[0].asks[0].size == 100, "oracle best ask parsed");
    check(rows[0].bids[0].price ==  99900 && rows[0].bids[0].size == 150, "oracle best bid parsed");

    // Diff: identical snapshots agree; a perturbed one is flagged with a message.
    std::string msg;
    Snapshot oracle;
    oracle.asks = { {100000, 100}, {100100, 200} };
    oracle.bids = { {99900, 150}, {99800, 300} };
    check(snapshots_equal(s, oracle, 30, msg), "identical snapshots compare equal");

    oracle.asks[1].size = 999;   // perturb
    msg.clear();
    check(!snapshots_equal(s, oracle, 30, msg), "perturbed snapshot compares NOT equal");
    check(!msg.empty(), "mismatch produced a description");

    if (failures == 0) { std::puts("snapshot_test: all assertions passed"); return 0; }
    std::printf("snapshot_test: %d FAILURE(S)\n", failures);
    return 1;
}
