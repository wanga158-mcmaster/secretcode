#include <cassert>
#include "../common/src/event_kernel.h"

int main() {
    using namespace p11::kernel;
    // Begin an ordinary action-ready owner turn. A channel is an alternative,
    // whose pending-zero state must survive the next caller's early checks.
    Allowances ready;assert(ready.move==Truth::yes&&ready.attack==Truth::yes&&ready.cast==Truth::yes);
    ChannelSnapshot none;assert(!preserve_channel(none));
    for(const auto kind:{ChannelKind::recall,ChannelKind::tribute,ChannelKind::teleport}) {
        ChannelSnapshot c{kind,1,10};assert(preserve_channel(c));
        const auto end=owner_end(c);assert(end.status==Status::applied&&end.banked==0);
        assert(c.remaining==0&&preserve_channel(c));
        assert(owner_end(c).status==Status::unchanged&&c.remaining==0);
    }
    // First qualifying capture phase initializes five; only later phases tick.
    CaptureProgress hold;assert(capture_start(hold,5,true).status==Status::applied);
    assert(hold.remaining==5);
    for(int i=0;i<4;++i)assert(!capture_phase(hold,true).completed);
    ChannelSnapshot tribute{ChannelKind::tribute,1,10};owner_end(tribute);
    assert(tribute.remaining==0&&tribute.bag==10);
    assert(capture_phase(hold,true).completed); // next round capture precedes owner start
    const int current_bag=tribute.bag+20; // externally witnessed personal capture award
    const auto bank=owner_start(tribute,current_bag);
    assert(bank.banked==30&&bank.status==Status::applied);
    assert(tribute.bag==0&&!preserve_channel(tribute));
    assert(owner_start(tribute,0).banked==0&&!capture_phase(hold,true).completed);

    ChannelSnapshot recall{ChannelKind::recall,0,7};
    const auto arrival=owner_start(recall,7);
    assert(arrival.recall_arrival&&arrival.banked==0&&recall.bag==7);
    assert(!spawn_recovery_due(false,true)); // arrival boundary performs no healing
    assert(!spawn_recovery_due(true,false)); // leaving spawn prevents recovery
    assert(spawn_recovery_due(true,true));
    ChannelSnapshot teleport{ChannelKind::teleport,0,7};
    const auto landing=owner_start(teleport,7);
    assert(landing.teleport_landing_required&&landing.status==Status::unsupported);
    assert(preserve_channel(teleport)); // no invented successful destination

    for(const auto kind:{ChannelKind::recall,ChannelKind::tribute,ChannelKind::teleport}) {
        for(const auto cause:{Interrupt::damage,Interrupt::stun,Interrupt::death,Interrupt::enemy_lock}) {
            ChannelSnapshot c{kind,0,10};
            const bool stops=cause==Interrupt::stun||cause==Interrupt::death
                ||(cause==Interrupt::damage&&kind==ChannelKind::recall)
                ||(cause==Interrupt::enemy_lock&&kind==ChannelKind::tribute);
            const auto result=interrupt(c,cause,true);
            assert((result.status==Status::applied)==stops);
            assert(preserve_channel(c)!=stops&&c.bag==10&&result.banked==0);
        }
    }
    ChannelSnapshot uncertain_stun{ChannelKind::teleport,0,7};
    assert(interrupt(uncertain_stun,Interrupt::stun).status==Status::unknown);
    assert(interrupt(uncertain_stun,Interrupt::stun,false).status==Status::unchanged);
    assert(preserve_channel(uncertain_stun));
    Allowances melee;assert(successful_walk(melee)==Status::applied);
    assert(melee.attack==Truth::yes&&successful_basic(melee)==Status::applied);
    Allowances ranged;ranged.ranged=true;
    assert(successful_walk(ranged)==Status::applied&&ranged.attack==Truth::no);
    assert(successful_basic(ranged)==Status::unsupported);
    assert(successful_cast(ranged,true)==Status::applied);
    assert(ranged.attack==Truth::yes&&ranged.move==Truth::unknown&&ranged.cast==Truth::no);
    assert(successful_basic(ranged)==Status::applied&&ranged.attack==Truth::no);
    assert(successful_cast(ranged,true)==Status::unsupported); // second slot cannot bypass cast cap
    Allowances observed;observed.ranged=true;successful_walk(observed);
    successful_cast(observed,true,false,Truth::no);assert(observed.move==Truth::no);
    Allowances attack_first;attack_first.ranged=true;successful_basic(attack_first);
    assert(successful_walk(attack_first)==Status::unsupported);

    CaptureProgress unknown;
    assert(capture_start(unknown,5,false).status==Status::unknown&&!unknown.remaining);
    assert(capture_phase(unknown,true).status==Status::unknown&&!unknown.remaining);
    capture_start(unknown,5,true);capture_phase(unknown,true);assert(unknown.remaining==4);
    observation_gap(unknown);assert(!unknown.remaining&&!unknown.witnessed);
    assert(!capture_phase(unknown,true).completed&&!unknown.remaining);
    ChannelSnapshot missing{ChannelKind::tribute,std::nullopt,10};
    assert(preserve_channel(missing)&&owner_start(missing,10).status==Status::unknown);
    assert(owner_end(missing).status==Status::unknown&&missing.bag==10);
}
