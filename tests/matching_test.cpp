#include "lob/book.hpp"
#include "lob/engine.hpp"
#include <cstdio>

using namespace lob;

// assert() is compiled out in Release (NDEBUG), so use an explicit check that always runs.
static int failures = 0;
static void check(bool cond, const char* what) {
    if (cond) { std::printf("  ok   %s\n", what); }
    else      { std::printf("  FAIL %s\n", what); ++failures; }
}

// LOBSTER prices are in 1/10000 dollars: $10.00 = 100000, $10.01 = 100100.
static Message limit(OrderId id, Side side, Price price, Qty size) {
    return Message{ /*time_ns*/ 0, MsgType::NewLimit, id, size, price, side };
}

int main() {
    Book   book;
    Engine engine(book);

    // Seed the ask side (sells rest — there are no bids to cross):
    engine.apply(limit(1, Side::Sell, 100000, 100));  // A: 100 @ $10.00 (oldest)
    engine.apply(limit(2, Side::Sell, 100000,  50));  // B:  50 @ $10.00
    engine.apply(limit(3, Side::Sell, 100100, 200));  // C: 200 @ $10.01

    // The Q2 aggressor: BUY 180 @ $10.00.
    engine.apply(limit(4, Side::Buy, 100000, 180));

    // --- assertions encode the Q2 trace ---
    check(book.lookup(1) == nullptr, "A (100) fully filled and gone");
    check(book.lookup(2) == nullptr, "B (50) fully filled and gone");

    Limit* ba = book.best_ask();      // $10.00 level emptied → best ask advanced to $10.01
    check(ba != nullptr && ba->price == 100100, "best ask advanced to $10.01");
    check(ba != nullptr && ba->volume == 200,   "C's 200 untouched");
    check(book.lookup(3) != nullptr && book.lookup(3)->shares == 200, "C still resting with 200");

    Limit* bb = book.best_bid();      // residual rested as a bid
    check(bb != nullptr && bb->price == 100000, "residual rested at $10.00 on the bid side");
    check(bb != nullptr && bb->volume == 30,    "residual volume is 30");
    Order* r = book.lookup(4);
    check(r != nullptr && r->shares == 30 && r->parent->price == 100000, "order 4 rests: 30 @ $10.00");

    // --- mirror scenario: a SELL sweeping the bid side ($9.99 = 99900) ---
    Book   book2;
    Engine engine2(book2);
    engine2.apply(limit(11, Side::Buy, 100000, 100));  // A: 100 @ $10.00 (oldest bid)
    engine2.apply(limit(12, Side::Buy, 100000,  50));  // B:  50 @ $10.00
    engine2.apply(limit(13, Side::Buy,  99900, 200));  // C: 200 @ $9.99 (worse bid)
    engine2.apply(limit(14, Side::Sell, 100000, 180)); // aggressor: SELL 180 @ $10.00

    check(book2.lookup(11) == nullptr, "sell: A (100) fully filled and gone");
    check(book2.lookup(12) == nullptr, "sell: B (50) fully filled and gone");
    Limit* bb2 = book2.best_bid();     // $10.00 bid level emptied → best bid dropped to $9.99
    check(bb2 != nullptr && bb2->price == 99900 && bb2->volume == 200, "sell: best bid dropped to $9.99 (200)");
    Limit* ba2 = book2.best_ask();     // residual rested as an ask
    check(ba2 != nullptr && ba2->price == 100000 && ba2->volume == 30, "sell: residual rested as 30 @ $10.00 ask");
    Order* r2 = book2.lookup(14);
    check(r2 != nullptr && r2->shares == 30 && r2->parent->price == 100000, "sell: order 14 rests: 30 @ $10.00");

    if (failures == 0) { std::puts("\nmatching_test: all assertions passed"); return 0; }
    std::printf("\nmatching_test: %d FAILURE(S)\n", failures);
    return 1;
}
