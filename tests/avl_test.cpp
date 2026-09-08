#include "lob/price_tree.hpp"
#include <cstdio>
#include <random>
#include <set>
#include <vector>

using namespace lob;

static int failures = 0;
static void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL %s\n", what); ++failures; }
}

int main() {
    // 1) Insert in an adversarial ascending order (worst case for an unbalanced BST).
    PriceTree t;
    for (Price p = 1; p <= 1000; ++p) t.find_or_create(p);
    check(!t.empty(), "tree non-empty after inserts");
    check(t.min() && t.min()->price == 1,    "min is 1");
    check(t.max() && t.max()->price == 1000, "max is 1000");
    check(t.balanced(), "balanced after 1000 ascending inserts (AVL kept height low)");

    // in-order traversal must be sorted 1..1000
    std::vector<Price> in;
    t.collect_inorder(in);
    bool sorted = (in.size() == 1000);
    for (size_t i = 0; sorted && i < in.size(); ++i) if (in[i] != (Price)(i + 1)) sorted = false;
    check(sorted, "in-order traversal is sorted 1..1000");

    // find_or_create returns the SAME node for an existing price (no duplicate)
    Limit* a = t.find_or_create(500);
    Limit* b = t.find_or_create(500);
    check(a == b && a->price == 500, "find_or_create is idempotent for an existing price");

    // 2) Erase every 3rd key; survivors findable, erased gone, still balanced.
    for (Price p = 1; p <= 1000; p += 3) t.erase(p);
    bool erase_ok = true;
    for (Price p = 1; p <= 1000; ++p) {
        bool want = (p % 3 != 1);          // erased p where (p-1)%3==0 i.e. p%3==1
        if ((t.find(p) != nullptr) != want) erase_ok = false;
    }
    check(erase_ok, "erase removed exactly the right keys");
    check(t.balanced(), "still balanced after erases");

    // 3) Randomized cross-check against std::set (membership + min + max).
    std::mt19937 rng(777);
    std::set<Price> ref;
    PriceTree mine;
    bool agree = true;
    for (int op = 0; op < 200'000 && agree; ++op) {
        Price p = (Price)(rng() % 5000) + 1;
        if ((rng() & 1) || ref.empty()) { mine.find_or_create(p); ref.insert(p); }
        else                            { mine.erase(p);          ref.erase(p);  }

        if ((mine.find(p) != nullptr) != (ref.count(p) != 0)) agree = false;
        if (!ref.empty()) {
            if (!mine.min() || mine.min()->price != *ref.begin())  agree = false;
            if (!mine.max() || mine.max()->price != *ref.rbegin()) agree = false;
        }
    }
    check(agree, "randomized ops agree with std::set (membership, min, max)");
    check(mine.balanced(), "balanced after randomized ops");

    // The level-DLL (min_ → next_level) must match the tree's sorted order exactly.
    std::vector<Price> via_tree, via_dll;
    mine.collect_inorder(via_tree);
    mine.collect_via_dll(via_dll);
    check(via_tree == via_dll, "level-DLL order matches in-order traversal (O(1) best-of-side intact)");

    if (failures == 0) { std::puts("avl_test: all assertions passed"); return 0; }
    std::printf("avl_test: %d FAILURE(S)\n", failures);
    return 1;
}
