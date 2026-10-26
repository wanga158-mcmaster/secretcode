#include "../src/config.h"
#include "../src/program.h"
#include <cassert>
using h12_capability_portfolio::CargoRenewal;
int main(){
    CargoRenewal cargo;
    assert(!cargo.tick(200,179,7,false));
    assert(!cargo.tick(201,180,0,false));
    assert(!cargo.tick(202,181,7,true));
    assert(cargo.tick(203,182,7,false));
    assert(cargo.active(203));
    assert(cargo.tick(250,0,14,false)); // A fresh payout keeps the delivery owed.
    assert(!cargo.tick(299,278,7,false)); // Exactly 96 turns, then ordinary work.
    assert(!cargo.tick(382,361,7,false));
    assert(cargo.tick(383,362,7,false));
    assert(!cargo.tick(384,363,0,false)); // A real empty bag completes delivery.
    assert(!cargo.active(384));
    assert(!cargo.tick(385,0,7,false));
}
