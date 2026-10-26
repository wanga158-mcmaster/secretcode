#include <cstdio>
#include <limits>
#include "capital_contact.h"

using namespace mb::capital::contact;
static int failures;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line%d: %s\n", __LINE__, #x); ++failures; } } while (0)

static Input fixture()
{
    Input input;
    input.count = 1;
    input.monsters[0] = {7, 30, 20, true};
    input.basic_damage = 10;
    input.health = 100;
    input.health_floor = 0;
    input.horizon = 30;
    input.opener_before_volley = input.attack_ready = input.readiness_known = input.known = true;
    return input;
}

int main()
{
    auto input = fixture();
    auto result = simulate(input);
    CHECK(result.status == Status::cleared && result.turns == 3 && result.hits == 3);
    CHECK(result.casts == 0 && result.first_target == 7 && result.kill_order[0] == 7 && result.killed == 1);
    CHECK(result.damage == 40 && result.end_health == 60 && result.minimum_health == 60);

    input = fixture();
    input.count = 2;
    input.monsters[0] = {9, 26, 0, true};
    input.monsters[1] = {2, 26, 0, true};
    input.basic_damage = 25;
    result = simulate(input);
    CHECK(result.status == Status::cleared && result.hits == 4); // not ceil(52/25)=3
    CHECK(result.first_target == 2 && result.kill_order[0] == 2 && result.kill_order[1] == 9);
    input.horizon = 3;
    result = simulate(input);
    CHECK(result.status == Status::horizon && result.turns == 3 && result.hits == 3 && result.killed == 1);

    input = fixture();
    input.count = 2;
    input.health = 1000;
    input.monsters[0] = {4, 50, 10, true};
    input.monsters[1] = {2, 100, 30, true};
    input.basic_damage = 30;
    CHECK(next_target(std::span{input.monsters.data(), input.count}) == 1);
    result = simulate(input);
    CHECK(result.status == Status::cleared && result.hits == 6 && result.turns == 6);
    CHECK(result.kill_order[0] == 2 && result.kill_order[1] == 4 && result.damage == 140);

    input = fixture();
    input.monsters[0].hp = 10;
    input.monsters[0].incoming = 60;
    input.health_floor = 50;
    result = simulate(input);
    CHECK(result.status == Status::cleared && result.turns == 1 && result.damage == 0); // kill before next volley
    input.opener_before_volley = false;
    result = simulate(input);
    CHECK(result.status == Status::health_floor && result.hits == 0 && result.end_health == 40);
    input.health_floor = 0;
    result = simulate(input);
    CHECK(result.status == Status::cleared && result.damage == 60);

    input = fixture();
    input.opener_before_volley = false;
    input.health_floor = 60;
    result = simulate(input);
    CHECK(result.status == Status::health_floor && result.hits == 2 && result.turns == 3 && result.end_health == 40);
    input.monsters[0].hp = 20;
    result = simulate(input);
    CHECK(result.status == Status::cleared && result.end_health == 60); // equality permits the finishing attack
    input.health = 59;
    CHECK(simulate(input).status == Status::health_floor && simulate(input).turns == 0);

    input = fixture();
    input.monsters[0].hp = 10;
    input.monsters[0].incoming = 10;
    input.attack_ready = false;
    result = simulate(input);
    CHECK(result.status == Status::cleared && result.turns == 2 && result.hits == 1 && result.damage == 10);
    input.readiness_known = false;
    CHECK(simulate(input).status == Status::unknown);

    input = fixture();
    input.count = 2;
    input.monsters[0] = {0, 10, 5, true};
    input.monsters[1] = {1, 10, 5, true};
    result = simulate(input);
    CHECK(result.status == Status::cleared && result.turns == 2 && result.damage == 5); // first kill removes next volley source
    input.monsters[0].hp = 0;
    CHECK(next_target(std::span{input.monsters.data(), input.count}) == 1);
    input.monsters[1].observed = false;
    CHECK(simulate(input).status == Status::unknown && next_target(std::span{input.monsters.data(), input.count}) == -1);

    input = fixture();
    input.monsters[0].incoming = 200;
    input.opener_before_volley = false;
    result = simulate(input);
    CHECK(result.status == Status::dead && result.end_health == 0 && result.damage == 100 && result.hits == 0);
    input = fixture();
    input.monsters[0].hp = 0;
    result = simulate(input);
    CHECK(result.status == Status::cleared && result.turns == 0 && result.hits == 0);

    input = fixture();
    input.horizon = 0;
    CHECK(simulate(input).status == Status::horizon && simulate(input).turns == 0);
    input.horizon = 10000;
    input.monsters[0].hp = 100000;
    input.monsters[0].incoming = 0;
    result = simulate(input);
    CHECK(result.status == Status::horizon && result.turns == max_turns);

    input = fixture();
    input.count = 0;
    CHECK(simulate(input).status == Status::invalid_input);
    input.count = 11;
    CHECK(simulate(input).status == Status::invalid_input);
    input = fixture();
    input.health = 0;
    CHECK(simulate(input).status == Status::invalid_input);
    input = fixture();
    input.basic_damage = 0;
    CHECK(simulate(input).status == Status::invalid_input);
    input = fixture();
    input.health = std::numeric_limits<float>::quiet_NaN();
    CHECK(simulate(input).status == Status::invalid_input);
    input = fixture();
    input.monsters[0].hp = -1;
    CHECK(simulate(input).status == Status::invalid_input);
    input = fixture();
    input.count = 2;
    input.monsters[1] = input.monsters[0];
    CHECK(simulate(input).status == Status::invalid_input);
    input = fixture();
    input.monsters[0].incoming = std::numeric_limits<float>::infinity();
    CHECK(simulate(input).status == Status::invalid_input);

    std::printf("capital fixed-contact cases: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
