#include "lob/generator.hpp"
#include "lob/book.hpp"
#include "lob/engine.hpp"
#include <cstdio>

using namespace lob;

static int failures = 0;
static void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL %s\n", what); ++failures; }
}

int main() {
    // 1) Reproducibility: same seed → identical stream.
    GenConfig cfg;
    Generator g1(cfg), g2(cfg);
    bool identical = true;
    for (int i = 0; i < 1000; ++i) {
        GenOp a = g1.next(), b = g2.next();
        if (a.msg.type != b.msg.type || a.msg.id != b.msg.id ||
            a.msg.price != b.msg.price || a.msg.size != b.msg.size || a.cls != b.cls)
            identical = false;
    }
    check(identical, "same seed produces an identical stream");

    // 2) Mix + sanity: classes appear in roughly the configured proportion; the stream drives the
    //    real engine without crashing and actually exercises the matching path.
    Book book; Engine engine(book);
    Generator g(cfg);
    long add = 0, cancel = 0, mkt = 0;
    for (int i = 0; i < 200000; ++i) {
        GenOp op = g.next();
        switch (op.cls) {
            case OpClass::Add:        ++add;    break;
            case OpClass::Cancel:     ++cancel; break;
            case OpClass::Marketable: ++mkt;    break;
        }
        engine.apply(op.msg);   // must not crash
    }
    long total = add + cancel + mkt;
    check(total == 200000, "generated the requested number of ops");
    check(add > cancel && cancel > mkt, "mix ordering add > cancel > marketable (per config)");
    check(mkt > 0, "some marketable orders were generated (matching path exercised)");
    // With ~5% marketable, expect it in a broad sanity band.
    double mkt_frac = (double)mkt / total;
    check(mkt_frac > 0.02 && mkt_frac < 0.10, "marketable fraction in a sane band around 5%");

    if (failures == 0) { std::puts("generator_test: all assertions passed"); return 0; }
    std::printf("generator_test: %d FAILURE(S)\n", failures);
    return 1;
}
