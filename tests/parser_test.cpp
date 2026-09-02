#include "lob/parser.hpp"
#include <cstdio>
#include <fstream>

using namespace lob;

static int failures = 0;
static void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL %s\n", what); ++failures; }
}

int main() {
    // Write a tiny LOBSTER-format fixture (real sample data is gitignored, so we don't depend on it).
    const char* path = "parser_test_fixture.csv";
    {
        std::ofstream f(path);
        f << "34200.004241176,1,16113575,18,5853300,1\n"   // new buy limit
          << "34200.500000000,3,16113575,18,5853300,1\n"   // total delete
          << "34201.000000000,4,222,50,5859400,-1\n"       // execution of a sell
          << "\n"                                          // blank line — should be skipped
          << "34202.000000000,2,333,5,5860000,-1\n";       // partial cancel of a sell
    }

    std::vector<Message> m = parse_messages(path);
    std::remove(path);

    check(m.size() == 4, "parsed 4 messages (blank line skipped)");

    check(m[0].type == MsgType::NewLimit, "row 0 type = NewLimit");
    check(m[0].id == 16113575,            "row 0 id");
    check(m[0].size == 18,                "row 0 size");
    check(m[0].price == 5853300,          "row 0 price ($585.33 in 1/10000)");
    check(m[0].side == Side::Buy,         "row 0 direction 1 -> Buy");

    check(m[1].type == MsgType::Delete,   "row 1 type = Delete");
    check(m[2].type == MsgType::Execute && m[2].side == Side::Sell, "row 2 = Execute, dir -1 -> Sell");
    check(m[3].type == MsgType::PartialCancel && m[3].size == 5,    "row 3 = PartialCancel size 5");

    if (failures == 0) { std::puts("parser_test: all assertions passed"); return 0; }
    std::printf("parser_test: %d FAILURE(S)\n", failures);
    return 1;
}
