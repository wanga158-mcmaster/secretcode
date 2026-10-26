#include <cassert>
#include "../common/src/turn_completion.h"
int main(){
 using namespace p11;
 // Ordinary post-attack frame: a melee clear retains legal movement, while
 // the ranged shared allowance or a movement-exclusive cast can remove it.
 assert(finish_clear({true,false,true,true})==ClearContinuation::enter_spawn);
 assert(finish_clear({true,false,false,true})==ClearContinuation::hold);
 assert(finish_clear({false,false,true,true})==ClearContinuation::hold);
 assert(finish_clear({true,true,true,true})==ClearContinuation::hold);
 // Fuel suspension must never let an old continuation execute next round.
 assert(finish_clear({true,false,true,false})==ClearContinuation::hold);
}
