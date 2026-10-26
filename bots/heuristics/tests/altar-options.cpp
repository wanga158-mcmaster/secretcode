#include "../altar-options/src/service.h"
#include "../altar-options/src/policy.h"
#include <cassert>
#include <string>
#include <vector>

namespace altar=h12_altar_options;
// Mutable host: walking, basic/reset allowances, locks and channel payout all
// change physically. This is the same Service::tick used by the SDK adapter.
struct World {
    struct Place {int x=1,hits=0;bool hidden=false,reachable=true,danger=false;};
    std::array<Place,2> places{{{1,5},{20,0}}};
    int turn=10,cargo=12,points=0,x=0,reset_cd=0,channel_ticks=0;
    int hits=0,walks=0,resets=0,submits=0,observations=0;
    int failed_walks=0,failed_hits=0,failed_submits=0;
    int channel_length=2;
    bool living=true,ranged_actor=false,basic=true,cast=true,movement=true;
    bool external_channel=false,fail_moves=false,fail_hits=false;
    bool lock_during_submit=false,no_score=false;
    std::vector<std::string> events;
    int round()const{return turn;}
    int bag()const{return cargo;}
    int score()const{return points;}
    bool alive()const{return living;}
    bool channeling()const{return channel_ticks>0||external_channel;}
    bool submitting()const{return channel_ticks>0;}
    bool ranged()const{return ranged_actor;}
    bool can_attack()const{return basic;}
    bool reset_ready()const{return cast&&reset_cd==0;}
    int tribute_ticks()const{return channel_length;}
    bool unsafe(int id)const{return places[id].danger;}
    altar::Altar observe(int id) {
        ++observations;
        const auto&p=places[id];
        const int d=std::abs(x-p.x),travel=std::max(0,d-1);
        const bool visible=!p.hidden||travel==0;
        return {id,travel,visible?p.hits:-1,visible,d<=(ranged_actor?3:1),d<=1,p.reachable};
    }
    std::array<altar::Altar,2> options(){return {observe(0),observe(1)};}
    bool walk(int id) {
        if(!movement||fail_moves||!places[id].reachable) {++failed_walks;return false;}
        if(std::abs(x-places[id].x)<=1)return false;
        x+=x<places[id].x?1:-1;movement=false;++walks;
        if(ranged_actor)basic=false;
        return true;
    }
    bool hit(int id) {
        const auto fresh=observe(id);
        if(!basic||!fresh.visible||!fresh.in_attack||places[id].hits<=0||fail_hits) {
            ++failed_hits;return false;
        }
        --places[id].hits;basic=false;++hits;return true;
    }
    bool reset() {
        if(!reset_ready())return false;
        cast=false;reset_cd=8;basic=true;++resets;return true;
    }
    bool submit(int id) {
        ++submits;
        if(lock_during_submit) {places[id].hits=5;lock_during_submit=false;}
        if(places[id].hits>0) {hit(id);++failed_submits;return false;}
        if(cargo<=0||std::abs(x-places[id].x)>1) {++failed_submits;return false;}
        channel_ticks=channel_length;return true;
    }
    void event(const char*name,int id,int value) {
        events.push_back(std::string(name)+":"+std::to_string(id)+":"+std::to_string(value));
    }
    bool saw(const std::string&prefix)const {
        for(const auto&e:events)if(e.starts_with(prefix))return true;
        return false;
    }
    void next() {
        ++turn;basic=cast=movement=true;
        if(reset_cd>0)--reset_cd;
        if(channel_ticks>0&&--channel_ticks==0) {
            if(!no_score)points+=cargo;
            cargo=0;
        }
    }
};

static void finish(altar::Service&s,World&w,int limit=60) {
    for(int i=0;i<limit&&w.cargo>0;++i) {s.tick(w,true);w.next();}
    s.tick(w,false);
}

int main() {
    // Removal is an executable plan: five hit-count basics despite arbitrary
    // damage stats, a real reset adds one, disappearance then own bank follows.
    World w;altar::Service s;
    const auto stale=w.observe(0);
    assert(s.tick(w,true)&&s.target()==0&&s.phase()==altar::Phase::remove);
    assert(w.hits==2&&w.resets==1&&w.places[0].hits==3);
    assert(stale.hits==5&&w.observe(0).hits==3);
    finish(s,w);
    assert(w.hits==5&&w.points==12&&w.cargo==0&&s.confirmed==1);
    assert(w.submits==1&&w.saw("hit:0:0")&&w.saw("confirmed:0:12"));

    // A cheap public outside option changes the actual steps and score: leave
    // the obstruction untouched, walk to the other altar, then channel there.
    w=World{};w.places[1].x=3;s=altar::Service{};
    assert(s.tick(w,true)&&s.target()==1&&w.walks==1&&w.hits==0);
    finish(s,w);
    assert(w.points==12&&w.places[0].hits==5&&s.confirmed==1&&w.walks==2);
    assert(w.saw("divert:1:0")&&w.saw("channel:1:12"));
    // Changing only alternative service travel flips the production choice.
    w=World{};w.places[1].x=9;s=altar::Service{};
    assert(s.tick(w,true)&&s.target()==0&&w.hits==2);

    // Ranged walking consumes the basic. A ready reset creates exactly one
    // actual attack on arrival, rather than the two modeled for a standing user.
    w=World{};w.ranged_actor=true;w.places[0].x=4;w.places[1].x=30;
    s=altar::Service{};assert(s.tick(w,true));
    assert(w.walks==1&&w.hits==1&&w.resets==1&&w.places[0].hits==4);
    w=World{};w.ranged_actor=true;w.reset_cd=4;w.basic=false;
    s=altar::Service{};assert(s.tick(w,true)&&w.hits==0&&w.resets==0);
    w.next();assert(s.tick(w,true)&&w.hits==1&&w.resets==0);

    // Hidden alternate is only an inspection option. Arrival reveals a real
    // lock; the controller removes it, never submits on an assumed empty altar.
    w=World{};w.places[1]={3,5,true,true,false};w.reset_cd=4;s=altar::Service{};
    assert(w.observe(1).hits==-1);
    assert(s.tick(w,true)&&s.target()==1&&w.hits==0&&w.submits==0);
    finish(s,w);
    assert(w.places[1].hits==0&&w.hits==5&&w.points==12&&s.confirmed==1);
    // Newly installed obstruction can reverse the choice after productive
    // progress. Previous hits are sunk and do not force continued removal.
    w=World{};w.places[0].hits=2;w.places[1].x=2;s=altar::Service{};
    assert(s.tick(w,true)&&s.target()==0&&w.places[0].hits==0);
    w.next();w.places[0].hits=5;
    assert(s.tick(w,true)&&s.target()==1&&w.walks==1&&w.places[0].hits==5);
    finish(s,w);assert(w.points==12&&s.confirmed==1);

    // No constant rescoring out of useful removal: a one-step alternative does
    // not beat the switching cost after the incumbent has made physical progress.
    w=World{};w.places[1].x=6;s=altar::Service{};
    assert(s.tick(w,true)&&s.target()==0&&w.places[0].hits==3);
    w.places[1].x=2;w.next();
    assert(s.tick(w,true)&&s.target()==0&&w.places[0].hits==2);

    // Physical movement failures are bounded. First divert to the remaining
    // option; another three failures release the turn to ordinary production.
    w=World{};w.places[0]={5,0};w.places[1]={9,0};w.fail_moves=true;s=altar::Service{};
    bool owned=true;
    for(int i=0;i<10&&owned;++i) {owned=s.tick(w,true);w.next();}
    assert(!owned&&!s.active()&&w.failed_walks<=6&&w.saw("refuse"));
    assert(!s.tick(w,true)); // retry cooldown means production really gets a turn
    w=World{};w.places[0]={30,0};w.places[1].reachable=false;s=altar::Service{};
    assert(s.tick(w,true));w.turn=s.deadline();
    assert(!s.tick(w,true)&&!s.active()&&w.saw("expiry"));

    // Failed hits and a silently attack-converted submit cannot claim either
    // channel start or banking. A new fresh hostile lock drives later work.
    w=World{};w.fail_hits=true;w.places[1].reachable=false;s=altar::Service{};
    for(int i=0;i<4&&s.tick(w,true);++i)w.next();
    assert(!s.active()&&w.hits==0&&w.points==0&&s.confirmed==0);
    w=World{};w.places[0].hits=0;w.places[1].reachable=false;
    w.lock_during_submit=true;s=altar::Service{};
    assert(s.tick(w,true)&&!w.submitting()&&s.phase()!=altar::Phase::confirm);
    assert(w.places[0].hits==4&&w.hits==1&&s.confirmed==0);
    finish(s,w);assert(w.hits==5&&s.confirmed==1&&w.points==12);

    // Preserve an already active channel, even with a zero bag or changed bag;
    // this service does not claim another program's completion.
    w=World{};w.cargo=0;w.external_channel=true;s=altar::Service{};
    assert(s.tick(w,true)&&w.walks==0&&w.submits==0&&!s.active());
    w.external_channel=false;w.points=50;assert(!s.tick(w,true)&&s.confirmed==0);
    w=World{};w.places[0].hits=0;s=altar::Service{};
    assert(s.tick(w,true)&&s.phase()==altar::Phase::confirm);
    w.cargo=24;assert(s.tick(w,true)&&w.channel_ticks>0&&w.submits==1);
    w.next();w.next();assert(!s.tick(w,true)&&s.confirmed==1&&w.points==24);

    // A peer score change, death, zero-cargo income or a bag disappearance
    // without the owned channel and matching score cannot forge a receipt.
    w=World{};s=altar::Service{};s.tick(w,true);w.cargo=0;w.points=99;
    assert(!s.tick(w,true)&&s.confirmed==0);
    w=World{};w.places[0].hits=0;s=altar::Service{};s.tick(w,true);
    w.living=false;w.cargo=0;w.points=99;w.channel_ticks=0;
    assert(!s.tick(w,true)&&s.confirmed==0&&!s.active());
    w=World{};w.cargo=0;s=altar::Service{};
    assert(!s.tick(w,true)&&w.walks==0&&w.submits==0); // gold-only production fallback
    w=World{};w.places[0].hits=0;w.no_score=true;s=altar::Service{};
    finish(s,w);assert(w.cargo==0&&w.points==0&&s.confirmed==0&&w.saw("unconfirmed"));

    // Unsafe current service uses the safe alternative, or immediately yields
    // for ordinary recovery if both options become unsafe.
    w=World{};s=altar::Service{};s.tick(w,true);w.next();w.places[0].danger=true;
    assert(s.tick(w,true)&&s.target()==1&&w.walks==1);
    w.next();w.places[1].danger=true;
    assert(!s.tick(w,true)&&!s.active());

    // Non-acting sync supports the executor's early capture/recovery paths.
    // It confirms only an owned completed channel and executes no new action.
    w=World{};w.places[0].hits=0;s=altar::Service{};s.tick(w,true);
    w.next();w.next();const int before_walks=w.walks,before_submits=w.submits;
    s.sync(w);assert(s.confirmed==1&&!s.active());
    assert(w.walks==before_walks&&w.submits==before_submits);

    // The production policy prices Marcella's actual spendable conversion at
    // the next queried purchase price. Already affordable gear creates no debt.
    altar::Policy policy;heuristics::State own;
    own.identity=7;own.gold=18;own.bag=6;own.altar=10;
    own.hp=own.maximum=1000;own.round=50;
    assert(policy.next_price==30&&policy.bank(own));
    policy.next_price=40;assert(!policy.bank(own));
    policy.next_price=30;own.gold=30;assert(!policy.bank(own));
    own.gold=18;own.identity=1;assert(!policy.bank(own));
    own.identity=7;own.mature=true;assert(!policy.bank(own));
    own.mature=false;own.bag=0;assert(!policy.bank(own));

    std::array<heuristics::Job,2> jobs{};
    jobs[0].id=0;jobs[0].gold=12;jobs[0].known=jobs[0].affordable=true;
    jobs[0].original_hp=jobs[0].remaining_hp=100;
    jobs[1]=jobs[0];jobs[1].id=1;jobs[1].gold=24;jobs[1].travel=10;
    altar::Policy cheap;altar::Policy expensive;expensive.next_price=40;
    assert(heuristics::choose(cheap,own,jobs)==0);
    assert(heuristics::choose(expensive,own,jobs)==1);
    // Once already funded, gold alone is no longer the useful currency.
    own.gold=40;jobs[1].gems=4;altar::Policy funded;
    assert(heuristics::choose(funded,own,jobs)==1);

    // Physical identities drive actual shop plans in the adapter: Vesper uses
    // Focus, Varg's second family uses Strength, and conversion workers become
    // durable. Draft indexes remain a separate append-only namespace.
    using P=heuristics::Purchase;
    assert(altar::Policy::purchase(1,0,0)==P::monster_focus);
    assert(altar::Policy::purchase(7,0,0)==P::monster_strength);
    assert(altar::Policy::purchase(10,5,0)==P::jungle_strength);
    assert(altar::Policy::purchase(7,5,0)==P::jungle_health);
    assert(altar::Policy::purchase(1,5,5)==P::power_focus);
    assert(altar::Policy::purchase(7,5,5)==P::power_health);
    assert(altar::Policy::purchase(0,5,5)==P::power_health);
    assert(altar::Policy::purchase(11,5,5)==P::power_strength);
    assert((altar::Policy::roster==std::array{4,0,3,1,9}));
}
