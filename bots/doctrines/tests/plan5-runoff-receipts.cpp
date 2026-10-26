#include "../runoff/src/receipts.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace r = p5::runoff::receipts;
static int checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "Runoff receipts CHECK%d line%d: %s\n", checks, __LINE__, #x); std::exit(1); } } while (false)

static r::Frame end(int round = 10) {
    r::Frame f;
    f.actor = 0; f.round = round; f.bag = 0; f.gold = 100; f.passive_gold = 0;
    f.current = f.alive = f.supported = true;
    return f;
}
static r::Frame payout(r::Frame p, int gems = 6, int gold = 20, int group = 32) {
    ++p.round; p.bag += gems; p.gold += gold + p.passive_gold;
    ++p.capture; p.capture_round = p.round; p.capture_group = group; p.capture_gems = gems;
    return p;
}
static void exact_and_split() {
    r::State s; const auto p = end(); const auto f = payout(p);
    CHECK(r::finalize(p, s));
    const auto got = r::observe(f, s);
    CHECK(got.exact && got.receipt == 1 && got.round == 11 && got.group == 32);
    CHECK(got.gems == 6 && got.gold == 20); // Actual split share, no full-group reward input.
    for (int i = 0; i < 8; ++i) CHECK(r::observe(f, s) == got);
    CHECK(r::finalize(f, s)); CHECK(r::finalize(f, s));
    const auto second = payout(f, 11, 40, 34);
    const auto next = r::observe(second, s);
    CHECK(next.exact && next.receipt == 2 && next.gems == 11 && next.gold == 40 && next.group == 34);
    CHECK(r::observe(second, s) == next);
    r::State zero; CHECK(r::finalize(p, zero));
    const auto free_gold = r::observe(payout(p, 1, 0), zero);
    CHECK(free_gold.exact && free_gold.gems == 1 && free_gold.gold == 0);
    r::State missing; CHECK(!r::observe(f, missing).exact);
}
static void passive_and_numeric_bounds() {
    auto p = end(); p.passive_gold = 7;
    r::State s; CHECK(r::finalize(p, s));
    CHECK(r::observe(payout(p), s).gold == 20); // Exactly one upkeep is excluded.
    p = end(); p.gold = 0; p.passive_gold = std::numeric_limits<int>::max();
    auto f = p; ++f.round; f.bag = 1; f.gold = std::numeric_limits<int>::max();
    f.capture = 1; f.capture_round = f.round; f.capture_group = 0; f.capture_gems = 1;
    s = {}; CHECK(r::finalize(p, s));
    const auto zero = r::observe(f, s); CHECK(zero.exact && zero.group == 0 && zero.gold == 0);
    p = end(); p.gold = std::numeric_limits<int>::max();
    f = payout(p, 1, 0); f.gold = 0;
    s = {}; CHECK(r::finalize(p, s)); CHECK(!r::observe(f, s).exact);
    p = end(); p.round = std::numeric_limits<int>::max();
    s = {}; CHECK(r::finalize(p, s)); CHECK(!r::observe(p, s).exact);
    p = end(); p.capture = std::numeric_limits<std::uint64_t>::max() - 1;
    s = {}; CHECK(r::finalize(p, s)); f = payout(p, 1, 0);
    CHECK(r::observe(f, s).exact); CHECK(r::finalize(f, s));
    auto wrap = f; ++wrap.round; wrap.capture = 0; ++wrap.bag; wrap.capture_round = wrap.round;
    CHECK(!r::observe(wrap, s).exact);
}
static void reject_ambiguous_entries() {
    for (int bad = 0; bad < 27; ++bad) {
        auto p = end(); r::State s; CHECK(r::finalize(p, s)); auto f = payout(p);
        switch (bad) {
        case 0: ++f.bank; f.bank_round = f.round; break;
        case 1: ++f.purchase; break;
        case 2: ++f.cash_attempt; break;
        case 3: f.submitting = true; break;
        case 4: f.supported = false; break;
        case 5: f.alive = false; break;
        case 6: f.current = false; break;
        case 7: ++f.actor; break;
        case 8: ++f.round; f.capture_round = f.round; break;
        case 9: f.round = 9; f.capture_round = 9; break;
        case 10: ++f.capture; break;
        case 11: f.capture = 0; break;
        case 12: f.capture_round = p.round; break;
        case 13: f.capture_group = -1; break;
        case 14: --f.capture_gems; break;
        case 15: f.bag = p.bag; break;
        case 16: f.gold = p.gold - 1; break;
        case 17: f.gold = -1; break;
        case 18: f.bag = -1; break;
        case 19: f.round = -1; break;
        case 20: f.actor = -1; break;
        case 21: f.passive_gold = -1; break;
        case 22: f.bank_round = f.round; break;
        case 23: ++f.passive_gold; break;
        case 24: f.capture_gems = 0; break;
        case 25: f.capture_round = f.round + 1; break;
        default: f.capture_group = -2; break;
        }
        CHECK(!r::observe(f, s).exact);
    }
    for (int bad = 0; bad < 5; ++bad) {
        auto p = end();
        if (bad == 0) p.submitting = true;
        if (bad == 1) p.supported = false;
        if (bad == 2) p.alive = false;
        if (bad == 3) p.current = false;
        if (bad == 4) p.passive_gold = -1;
        r::State s; CHECK(!r::finalize(p, s));
        auto f = payout(end()); CHECK(!r::observe(f, s).exact);
    }
}
static void contradictions_and_errors() {
    auto p = end(); auto f = payout(p); r::State s;
    CHECK(r::finalize(p, s)); CHECK(r::observe(f, s).exact);
    auto changed = f; ++changed.gold;
    CHECK(!r::observe(changed, s).exact); CHECK(!r::observe(f, s).exact);
    CHECK(!r::finalize(f, s));
    auto later = f; ++later.round;
    CHECK(!r::observe(later, s).exact); CHECK(r::finalize(later, s));
    CHECK(r::observe(payout(later, 1, 5), s).exact);

    s = {}; CHECK(r::finalize(p, s)); CHECK(r::finalize(p, s));
    auto changed_end = p; --changed_end.gold; ++changed_end.cash_attempt;
    CHECK(!r::finalize(changed_end, s)); CHECK(!r::finalize(p, s));
    CHECK(!r::observe(f, s).exact);

    s = {}; CHECK(r::finalize(p, s)); CHECK(r::observe(f, s).exact);
    r::invalidate(s);
    CHECK(!r::observe(f, s).exact); CHECK(!r::finalize(f, s));
    auto rewind = p; rewind.round = 12;
    CHECK(!r::finalize(rewind, s)); // No serial replay after invalidate.

    s = {}; CHECK(r::finalize(p, s));
    auto idle = p; ++idle.round;
    CHECK(!r::observe(idle, s).exact);
    auto missed_end = payout(idle);
    CHECK(!r::observe(missed_end, s).exact); // No final sample for prior owner.
    CHECK(r::finalize(missed_end, s));
    CHECK(r::observe(payout(missed_end), s).exact);

    s = {}; CHECK(r::finalize(p, s));
    auto first = payout(p); CHECK(r::observe(first, s).exact); CHECK(r::finalize(first, s));
    auto next = payout(first); CHECK(r::observe(next, s).exact);
    CHECK(!r::observe(first, s).exact); // Stale round cannot replay a cached event.
    CHECK(!r::observe(next, s).exact);
}
static void finalized_action_baselines() {
    // A checked post-buy final sample already contains the debit. It must not
    // subtract that debit from a later otherwise-clean capture interval.
    auto p = end(); r::State s; CHECK(r::finalize(p, s));
    auto idle = p; ++idle.round; CHECK(!r::observe(idle, s).exact);
    auto after_buy = idle; after_buy.gold -= 30; ++after_buy.purchase; ++after_buy.cash_attempt;
    CHECK(r::finalize(after_buy, s));
    auto f = payout(after_buy);
    const auto c = r::observe(f, s); CHECK(c.exact && c.gold == 20);
    CHECK(r::finalize(f, s));
    auto banked = f; ++banked.round; banked.bag = 0; banked.gold += 12;
    ++banked.bank; banked.bank_round = banked.round;
    CHECK(!r::observe(banked, s).exact); // Bank income cannot become camp gold.
    CHECK(r::finalize(banked, s));
    CHECK(r::observe(payout(banked, 11, 40, 34), s).exact);

    // Capture plus tribute completion in one interval is deliberately unknown.
    p = end(); p.bag = 6;
    s = {}; CHECK(r::finalize(p, s)); f = payout(p); f.bag = 0; f.gold += 24;
    ++f.bank; f.bank_round = f.round;
    CHECK(!r::observe(f, s).exact);
}
int main() {
    exact_and_split(); passive_and_numeric_bounds(); reject_ambiguous_entries();
    contradictions_and_errors(); finalized_action_baselines();
    std::printf("Runoff receipts PASS%d checks\n", checks);
}
