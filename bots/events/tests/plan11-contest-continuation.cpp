#include <cassert>
#include "../common/src/contest_continuation.h"

int main(){
    using p11::contest::Continuation;
    Continuation c;
    assert(!c.pending(0)&&!c.permitted(0,false,true,true,true));
    // The selected cleared group requires an off-spawn step around an ally.
    // On the following owner turn, the old on-spawn-only dispatch would return
    // to the nearest spawn instead of completing that same contest detour.
    c.departed(0,true,false,true);
    assert(c.permitted(0,false,true,true,true));
    assert(!c.permitted(1,false,true,true,true)); // no source-ranking change
    assert(!c.permitted(0,true,true,true,true)); // ordinary capture dispatch
    assert(!c.permitted(0,false,false,true,true)); // no blind pursuit
    assert(!c.permitted(0,false,true,false,true)); // source no longer cleared
    assert(!c.permitted(0,false,true,true,false)); // no stale owner-turn facts
    // Regaining a spawn, failed movement or a crossed epoch does not create a
    // new detour obligation. Death/channel/recovery uses this same clear().
    c.departed(0,true,true,true);assert(!c.pending(0));
    c.departed(0,false,false,true);assert(!c.pending(0));
    c.departed(0,true,false,false);assert(!c.pending(0));
    c.departed(0,true,false,true);c.clear();assert(!c.pending(0));
}
