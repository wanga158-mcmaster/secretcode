#include "word_search.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace mosaic::word;
int main() {
    unsigned checks = 0;
    const auto check = [&](bool condition) { assert(condition); ++checks; };
    Body body{{0, 0}, 1000, 0, 1, 0, true, true, false, false};
    std::array<Target, 2> targets{{
        {0, {1, 0}, 40, 0, TargetKind::camp, true, 1, 45, {100, 0}},
        {1, {0, 1}, 200, 0, TargetKind::camp, true, 1, 45, {100, 0}}
    }};
    Input in; in.body = body; in.targets = targets;
    in.beam_width = 16; in.node_quota = 1000;
    in.abilities[0] = {AbilityKind::damage, true, true, 4, 0, camps};
    auto result = search(in);
    check(result.valid && result.score == 145 && result.length == 2);
    check(result.first.kind == Kind::basic || result.first.kind == Kind::cast);
    // A dead primary never receives the retained second action. Search over
    // actual post-basic residuals retargets the cast to the surviving member.
    bool coherent = true;
    if (result.word[0].kind == Kind::basic && result.word[0].target_id == 0)
        coherent = result.word[1].kind == Kind::cast && result.word[1].target_id == 1;
    check(coherent);
    auto after_basic = in;
    std::array<Target, 2> remaining = targets; remaining[0].hp = 0;
    after_basic.targets = remaining; after_basic.body.can_attack = false; after_basic.body.attack_spent = true;
    result = search(after_basic);
    check(result.first.kind == Kind::cast && result.first.target_id == 1 && result.score == 100);
    // Concrete stale-target counterexample: the unique best cast before the
    // actual basic is target0; after that basic kills it, target1 is best.
    auto cast_only = after_basic;
    remaining = targets; remaining[0].spell_hit[0] = 40; remaining[1].spell_hit[0] = 30;
    cast_only.targets = remaining;
    result = search(cast_only);
    check(result.first.kind == Kind::cast && result.first.target_id == 0 && result.score == 40);
    remaining[0].hp = 0;
    result = search(cast_only);
    check(result.first.kind == Kind::cast && result.first.target_id == 1 && result.score == 30);

    // Three-action reset word beats a lower-damage cast/basic alternative.
    in.abilities[0] = {AbilityKind::reset, true, false, 0, 0, self};
    in.abilities[1] = {AbilityKind::damage, true, false, 4, 0, camps};
    targets[0].hp = 500; targets[0].spell_hit[1] = 20;
    targets[1].hp = 0;
    result = search(in);
    check(result.length == 3 && result.score == 90);
    check(result.word[0].kind == Kind::basic && result.word[1].kind == Kind::reset && result.word[2].kind == Kind::basic);
    auto spent = in; spent.body.can_attack = false; spent.body.attack_spent = false;
    result = search(spent);
    check(result.first.kind != Kind::reset);
    spent.body.attack_spent = true;
    result = search(spent);
    check(result.first.kind == Kind::reset && result.length == 2 && result.score == 45);

    // Actual walking excludes an exclusive spell; cooldown/readiness does not
    // replace the independent movement restriction.
    in.abilities[0] = {AbilityKind::damage, true, true, 4, 0, camps};
    in.abilities[1] = {};
    targets[0].spell_hit[0] = 100;
    in.body.walked = true;
    result = search(in);
    check(result.length == 1 && result.first.kind == Kind::basic && result.score == 45);
    in.body.can_attack = false; in.body.attack_spent = false;
    result = search(in);
    check(result.length == 0 && result.score == 0);
    in.body = body;

    // Shield and HP residuals use repeated float32 subtraction, not product
    // estimates, and one cast cannot be followed by a second slot's cast.
    targets[0].hp = 100; targets[0].shield = 50; targets[0].basic_hit = 45;
    targets[0].spell_hit[0] = 60;
    in.shield_progress = 0;
    result = search(in);
    check(result.length == 2 && result.score == 55);
    in.abilities[1] = {AbilityKind::damage, true, false, 4, 0, camps};
    targets[0].spell_hit[1] = 90;
    result = search(in);
    check(result.score == 85 && result.length == 2);
    check(!(result.word[0].kind == Kind::cast && result.word[1].kind == Kind::cast));

    // Varg-style basic splash and a Ryth-style off-target ground center affect
    // every supplied living body in radius, retaining their independent HP.
    targets = {{{0, {1, 0}, 100, 0, TargetKind::camp, true, 1, 45, {70, 0}},
                {1, {2, 0}, 100, 0, TargetKind::camp, true, 1, 45, {70, 0}}}};
    in.abilities = {}; in.body.splash = 1;
    result = search(in);
    check(result.first.kind == Kind::basic && result.score == 90);
    in.body.range = 0; in.body.can_attack = false; in.body.splash = 0;
    std::array<Point, 1> centers{{{1, 0}}}; in.ground_centers = centers;
    in.abilities[0] = {AbilityKind::damage, true, true, 4, 1, ground};
    result = search(in);
    check(result.first.kind == Kind::cast && result.first.target_index == -1 && result.score == 140);

    // Explicit utility may prefer a shield; it is still the one cast.
    in.abilities[1] = {AbilityKind::shield, true, false, 0, 0, self, 200};
    in.self_shield_value = 1;
    result = search(in);
    check(result.first.kind == Kind::shield && result.score == 200 && result.length == 1);

    in.node_quota = 1;
    result = search(in);
    check(result.nodes <= 1 && !result.complete);
    in.node_quota = 1000; in.beam_width = 1;
    result = search(in);
    check(!result.complete && result.beam_pruned > 0);
    targets[0].hp = std::numeric_limits<float>::quiet_NaN();
    result = search(in);
    check(!result.valid && result.issue == Issue::invalid_input);
    Residual subtraction{649.0f, 0};
    for (unsigned i = 0; i < 5; ++i) subtraction = hit(subtraction, 129.8f);
    check(subtraction.hp > 0);
    check(hit(subtraction, 129.8f).hp == 0);

    // Independent integer closed-form oracle over the whole two-target family:
    // one cast and one basic, optionally replace the cast with a reset/basic.
    // It enumerates target assignments, not the search implementation's states.
    constexpr std::array health{0, 1, 40, 45, 46, 69, 70, 71, 115, 116};
    for (int cast_hit : {20, 70}) for (int h0 : health) for (int h1 : health) {
        std::array<Target, 2> pair{{
            {0, {1, 0}, float(h0), 0, TargetKind::camp, true, 1, 45, {float(cast_hit), 0}},
            {1, {0, 1}, float(h1), 0, TargetKind::camp, true, 1, 45, {float(cast_hit), 0}}
        }};
        Input exact; exact.body = body; exact.targets = pair; exact.beam_width = 16; exact.node_quota = 2000;
        exact.abilities[0] = {AbilityKind::damage, true, false, 4, 0, camps};
        exact.abilities[1] = {AbilityKind::reset, true, false, 0, 0, self};
        int expected = 0;
        const std::array hp{h0, h1};
        for (int i = -1; i < 2; ++i) for (int j = -1; j < 2; ++j) {
            int value = 0;
            if (i >= 0 && i == j) value = std::min(hp[std::size_t(i)], 45 + cast_hit);
            else value = (i >= 0 ? std::min(hp[std::size_t(i)], 45) : 0) +
                         (j >= 0 ? std::min(hp[std::size_t(j)], cast_hit) : 0);
            expected = std::max(expected, value);
            if (i >= 0 && j >= 0) expected = std::max(expected, i == j ? std::min(hp[std::size_t(i)], 90) :
                std::min(hp[std::size_t(i)], 45) + std::min(hp[std::size_t(j)], 45));
        }
        result = search(exact);
        check(result.valid && result.score == expected && result.nodes <= exact.node_quota);
    }
    // Different utility weights can express enemy pressure without changing
    // the scalar mechanics or pretending that a visible foe is harmless.
    targets = {{{0, {1, 0}, 100, 0, TargetKind::camp, true, 1, 45, {0, 0}},
                {1, {0, 1}, 100, 0, TargetKind::enemy, true, 4, 45, {0, 0}}}};
    in = {}; in.body = body; in.targets = targets;
    result = search(in);
    check(result.first.kind == Kind::basic && result.first.target_id == 1 && result.score == 180);
    in.node_quota = 0;
    result = search(in);
    check(result.valid && !result.complete && result.nodes == 0 && result.length == 0);
    in.node_quota = 10;
    targets[1].kind = TargetKind::camp; targets[1].id = targets[0].id;
    result = search(in);
    check(!result.valid && result.issue == Issue::invalid_input);
    targets[1].kind = TargetKind::enemy;
    result = search(in);
    check(result.valid); // camp/enemy native ID namespaces may overlap.
    in.abilities[0].reset_basic_multiplier = std::numeric_limits<float>::max();
    result = search(in);
    check(!result.valid && result.issue == Issue::invalid_input);
    std::printf("word search: %u checks\n", checks);
}
