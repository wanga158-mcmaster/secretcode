#include "capital_service_execution.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>
namespace f=wb::capital::service_forecast;
namespace e=wb::capital::service_execution;
static unsigned checks=0,failures=0;
static void check(bool value,std::string_view why){++checks;if(!value){++failures;std::cout<<"FAIL "<<why<<'\n';}}
static f::Verdict allowed(void*,const f::World&,unsigned,const f::Primitive&){return f::Verdict::Allowed;}
static f::Verdict visible(void*,const f::World&,unsigned,int){return f::Verdict::Allowed;}
static f::Verdict spawn(void*,const f::World&,unsigned,f::Point p){return p==f::Point{0,1}?f::Verdict::Allowed:f::Verdict::Blocked;}
static f::Verdict altar(void*,const f::World& w,unsigned a,f::Point p){return p==f::Point{5,1}&&f::distance(w.bodies[a].pose,p)<=1?f::Verdict::Allowed:f::Verdict::Blocked;}
static f::Verdict contested(void*,const f::World&){return f::Verdict::Allowed;}
static f::Reply reply(void*,const f::World&,int){f::Reply r;r.complete=true;r.domain=3;return r;}
static bool arrival(void*,const f::World&,unsigned,f::Point&){return false;}
static f::PurchaseEffect purchase(void*,const f::World& w,unsigned a,const f::Purchase& p){
    f::PurchaseEffect q;if(p.item!=41||p.count!=1||p.quote_revision!=101||p.quoted_cost!=30)return q;
    q.complete=true;q.revision=101;q.cost=30;q.health_growth=100;q.strength_growth=5;q.focus_growth=2;
    q.items=w.bodies[a].items;++q.items[41];q.effective_recall_ticks=5;return q;
}
static bool work(void*,unsigned){return true;}
static f::Body body(){f::Body b;b.id=0;b.actual=true;b.pose={1,1};b.hp=800;b.max_hp=1000;b.strength=70;b.focus=100;
    b.kit=f::Kit::Varg;b.bag_known=b.gold_known=true;b.gold=b.capital_floor=0;b.splash_radius_known=true;b.splash_radius=1;
    b.effective_recall_ticks=5;b.shield_modifier=1;b.shield_duration=2;return b;}
static f::World world(){f::World w;w.round=0;w.horizon=13;w.team=0;w.phase=f::Phase::ActorAction;w.actors=w.npcs=w.slots=1;w.revision=9;
    w.bodies[0]=body();w.camps[0].id=5;w.camps[0].pose={2,1};w.camps[0].hp=w.camps[0].max_hp=120;w.camps[0].actual=true;w.camps[0].empty_batches_known=true;
    w.group.id=8;w.group.life=f::Life::Alive;w.group.roster_complete=w.group.capture_history_known=true;w.group.generation=1;w.group.gold=60;w.group.gems=14;
    w.rules.queried=true;w.rules.capture_ticks=2;w.rules.sleep_batches=2;w.rules.respawn_delay=10;w.rules.reset_cooldown=8;w.rules.shield_cooldown=8;w.rules.heal_cooldown=8;w.rules.recall_ticks=5;
    w.rules.bank_multiplier=.1;w.rules.spawn_healing=1;w.rules.max_rounds=2000;w.rules.winning_gems=1000;
    w.even[0]=w.odd[0]={true,0,-1};return w;}
static f::Program program(){f::Program p;p.id=77;const std::array<std::array<f::Point,5>,6> paths{{
    {{{1,1}}},{{{1,1},{2,1}}},{{{2,1},{3,1}}},{{{3,1},{4,1}}},{{{4,1},{3,1},{2,2},{1,2},{0,1}}},{{{0,1},{1,1}}}}};
    const unsigned lengths[6]={1,2,2,2,5,2};
    for(unsigned k=0;k<6;++k){auto& leg=p.legs[p.leg_count];p.bodies[0].legs[k]=int(p.leg_count++);leg.origin=paths[k][0];leg.begin=p.route_count;leg.count=lengths[k]-1;leg.complete=true;
        for(unsigned n=1;n<lengths[k];++n)p.route[p.route_count++]={paths[k][n],0,13};}
    p.purchase_count=1;p.purchases[0]={41,1,101,30};p.bodies[0].purchase_count=1;p.bodies[0].altar={5,1};p.bodies[0].altar_known=true;p.bodies[0].post_renewal=f::PostRenewal::ConditionalHold;return p;}
static const f::Result& selected(const f::World& initial=world(),const f::Program& policy=program()){static auto s=std::make_unique<f::Simulator>();
    f::Domain d;d.initial_world=9;d.physics=2;d.reply=3;d.services=4;d.assumptions=5;d.physics_closed=d.replies_closed=d.local_sources_closed=d.conditional=d.service_tail_closed=true;
    const f::Callbacks cb{nullptr,&allowed,&visible,&spawn,&altar,&contested,&reply,&arrival,&purchase,&work,&visible};
    check(s->begin(initial,policy,d,cb),"actual simulator admits complete selected programme");unsigned calls=0;while(s->result().status==f::Status::Building&&calls++<1000)s->advance(64);
    check(s->result().status==f::Status::Complete,"actual selected simulation completes");check(s->result().bodies[0].banked==14&&s->result().bodies[0].renewed,"simulation has payout and renewal");
    return s->result();}
static e::Frame frame(){const auto w=world();e::Frame x;x.stamp={0,0,1,1,0};x.body=w.bodies[0];x.group=w.group;x.npcs=1;x.camps[0]=w.camps[0];
    x.observed=x.fresh=x.permissions_known=x.roster_complete=x.in_group_known=x.on_spawn_known=true;return x;}
static e::Binding binding(const e::Frame& x){return {x.stamp,8,1,77,9,5,13};}
static f::World observed_source(const e::Frame& x){auto w=world();w.round=x.stamp.round;w.bodies[0]=x.body;w.group=x.group;w.npcs=x.npcs;w.camps=x.camps;return w;}
static std::unique_ptr<e::Interpreter> begin(e::Frame& x){auto i=std::make_unique<e::Interpreter>();const auto source=observed_source(x);check(i->begin(x,program(),selected(source),binding(x),source),"interpreter joins selected real forecast");return i;}
static void input(e::Frame& x,int round){x.receipts={};x.stamp.round=round;++x.stamp.input;++x.stamp.sample;x.body.m=x.body.a=x.body.c=true;x.body.walked=x.body.cast_used=x.body.completed=false;
    if(x.body.cooldown[0])--x.body.cooldown[0];if(x.body.cooldown[1])--x.body.cooldown[1];}
static void readback(e::Frame& x){x.receipts={};++x.stamp.sample;++x.stamp.effect;}
static e::Receipt receipt(const e::Frame& x,std::uint64_t event,e::ReceiptKind kind,std::uint64_t directive=0){
    e::Receipt r;r.event=event;r.program=77;r.source_directive=directive;r.observed=x.stamp;r.kind=kind;r.evidence=e::Evidence::SupportedConservation;
    r.group=8;r.generation=1;r.source_known=r.amount_known=true;return r;}
static e::Directive command(e::Interpreter& i,const e::Frame& x,f::Action action){auto d=i.choose(x);if(d.kind!=e::Kind::Physical||d.primitive.action!=action)std::cout<<"command diagnostic round="<<x.stamp.round<<" stage="<<unsigned(i.stage())<<" kind="<<unsigned(d.kind)<<" reason="<<unsigned(d.reason)<<" state="<<unsigned(i.state())<<" wanted="<<unsigned(action)<<'\n';check(d.kind==e::Kind::Physical&&d.primitive.action==action,"expected actual physical intention");return d;}
static void move(e::Interpreter& i,e::Frame& x,f::Point destination){auto d=command(i,x,f::Action::Move);check(d.primitive.destination==destination,"exact owned route destination");readback(x);x.body.pose=destination;x.body.m=false;
    if(x.body.kit==f::Kit::Willow)x.body.a=false;x.in_group=destination==f::Point{2,1};x.on_spawn=destination==f::Point{0,1};check(i.attempt(d,e::Attempt::Accepted,x),"accepted move needs actual post pose");}
static void initial_work(e::Interpreter& i,e::Frame& x){
    auto d=command(i,x,f::Action::Basic);check(d.primitive.target==5,"fresh first in-range target");readback(x);x.body.a=false;x.camps[0].hp=50;check(i.attempt(d,e::Attempt::Accepted,x),"observed first basic delta");
    d=command(i,x,f::Action::Reset);readback(x);x.body.a=true;x.body.c=false;x.body.cast_used=true;x.body.cooldown[0]=8;check(i.attempt(d,e::Attempt::Accepted,x),"actual reset refreshes A only");
    d=command(i,x,f::Action::Basic);readback(x);x.body.a=false;x.camps[0].hp=0;x.group.life=f::Life::Reward;check(i.attempt(d,e::Attempt::Accepted,x),"observed clear basic");
    move(i,x,{2,1});check(i.choose(x).kind==e::Kind::Wait,"actual capture waits without invented timer");}
static void capture(e::Interpreter& i,e::Frame& x){
    input(x,1);check(i.choose(x).kind==e::Kind::Wait,"first actual reward wait");input(x,2);check(i.choose(x).kind==e::Kind::Wait,"second actual reward wait");
    input(x,3);x.group.life=f::Life::Respawning;x.body.bag=14;x.body.gold=60;auto r=receipt(x,1,e::ReceiptKind::GroupPayout);r.gems=14;r.gold=60;
    x.receipts=std::span<const e::Receipt>{&r,1};auto d=command(i,x,f::Action::Move);check(d.primitive.destination==f::Point{3,1},"receipt releases source before altar");
    readback(x);x.body.pose={3,1};x.body.m=false;x.in_group=false;check(i.attempt(d,e::Attempt::Accepted,x),"release actual readback");check(i.facts().capture_attributed&&i.facts().captured_gems==14,"actual attributed custody exactly once");}
static std::uint64_t bank(e::Interpreter& i,e::Frame& x){input(x,4);move(i,x,{4,1});auto d=command(i,x,f::Action::Bank);const auto token=d.id;
    check(d.primitive.destination==f::Point{5,1},"Submit addresses actual altar");readback(x);x.body.channel=f::Channel::Bank;x.body.channel_ticks=2;x.body.m=x.body.a=x.body.c=false;
    check(i.attempt(d,e::Attempt::Accepted,x),"actual accepted N2 channel start");check(!i.facts().bank_attributed&&i.facts().banked_gems==0,"start is never payout");
    input(x,5);x.body.channel_ticks=1;x.body.m=x.body.a=x.body.c=false;check(i.choose(x).kind==e::Kind::Wait,"continued real channel");return token;}
static void full_cycle(bool healing){auto x=frame();auto i=begin(x);initial_work(*i,x);capture(*i,x);const auto token=bank(*i,x);
    input(x,6);x.body.channel=f::Channel::None;x.body.channel_ticks=0;x.body.bag=0;auto r=receipt(x,2,e::ReceiptKind::BankPayout,token);r.gems=r.source_bag=14;x.receipts={&r,1};
    auto d=command(*i,x,f::Action::Move);check(d.primitive.destination==f::Point{3,1},"attributed payout continues actual return");readback(x);x.body.pose={3,1};x.body.m=false;check(i->attempt(d,e::Attempt::Accepted,x),"return first move");
    input(x,7);x.group.life=f::Life::Unknown;x.group.generation=0;x.group.roster_complete=false;x.npcs=0;x.roster_complete=false;move(*i,x,{2,2});
    input(x,8);move(*i,x,{1,2});input(x,9);move(*i,x,{0,1});check(i->choose(x).kind==e::Kind::Wait,"on-spawn partial HP waits real owner END while distant source hidden");
    input(x,10);x.body.hp=x.body.max_hp;r=receipt(x,3,e::ReceiptKind::Healing);r.healing_amount=200;if(healing)x.receipts={&r,1};d=command(*i,x,f::Action::Buy);
    check(d.purchase.item==41&&d.purchase.quoted_cost==30,"exact priced purchase retained");readback(x);x.body.items[41]=1;x.body.gold=30;x.body.max_hp=x.body.hp=1100;x.body.strength=75;x.body.focus=102;
    r=receipt(x,4,e::ReceiptKind::Purchase,d.id);r.item=41;r.count=1;r.cost=30;r.evidence=e::Evidence::ActualDirect;x.receipts={&r,1};check(i->attempt(d,e::Attempt::Accepted,x),"accepted buy requires actual inventory and receipt");
    move(*i,x,{1,1});check(i->choose(x).kind==e::Kind::Preparation,"renew arrival requests actual source observation");
    input(x,11);check(i->choose(x).reason==e::Reason::UnknownObservation,"unseen renewal never infers respawn from elapsed time");
    input(x,12);x.group.life=f::Life::Respawning;x.group.generation=1;x.group.roster_complete=x.roster_complete=true;x.npcs=1;check(i->choose(x).kind==e::Kind::Wait,"known actual respawn wait");
    input(x,13);x.group.life=f::Life::Alive;x.group.generation=2;x.camps[0].hp=120;d=command(*i,x,f::Action::Basic);readback(x);x.body.a=false;x.camps[0].hp=45;
    check(i->attempt(d,e::Attempt::Accepted,x),"actual renewed HP work");check(i->state()==e::State::Complete&&i->choose(x).kind==e::Kind::Complete,"whole actual programme closes");
    const auto& v=i->facts();check(v.clear_observed&&v.capture_attributed&&v.source_released&&v.bank_attributed&&v.recovered_actual_state&&v.healing_observed==healing&&v.renewed,"actual Ready state and attributed healing stay distinct");
    check(v.work_damage==195&&v.renewal_damage==75&&v.captured_gems==14&&v.banked_gems==14&&v.purchases==1&&v.spent_gold==30,"actual sums count once");}
static void failures_and_freshness(){
    {auto x=frame();auto i=begin(x);const auto d=command(*i,x,f::Action::Basic);check(i->choose(x).reason==e::Reason::AwaitAttempt,"unattempted intention cannot issue another");
        check(i->cancel(d),"funding denial can cancel unattempted intention");check(!i->cancel(d),"intention cancel consumed once");check(i->choose(x).kind==e::Kind::Physical,"cancel preserves real permissions");}
    {auto x=frame();auto i=begin(x);auto d=command(*i,x,f::Action::Basic);check(!i->attempt(d,e::Attempt::Accepted,x)&&i->reason()==e::Reason::AwaitFresh,"accepted claim without fresh readback rejected");}
    {auto x=frame();auto i=begin(x);auto d=command(*i,x,f::Action::Basic);readback(x);check(!i->attempt(d,e::Attempt::Refused,x)&&i->reason()==e::Reason::Refused,"actual refusal typed handoff");check(!i->attempt(d,e::Attempt::Accepted,x),"refused intention cannot replay");}
    {auto x=frame();auto i=begin(x);auto d=command(*i,x,f::Action::Basic);check(!i->attempt(d,e::Attempt::Unknown,x)&&i->reason()==e::Reason::UnknownAttempt,"unknown host effect cannot authorize suffix");}
    {auto x=frame();auto i=begin(x);auto d=command(*i,x,f::Action::Basic);readback(x);++x.stamp.round;check(!i->attempt(d,e::Attempt::Accepted,x),"suspended cross-round host return revokes intention");}
    {auto x=frame();auto i=begin(x);x.body.hp=799;check(i->choose(x).reason==e::Reason::Readback,"changed physical values at same sample rejected");}
    {auto x=frame();auto i=begin(x);input(x,1);x.body.alive=false;x.body.hp=0;check(i->choose(x).reason==e::Reason::Death,"death distinct from empty custody");check(!i->facts().bank_attributed,"death does not bank");}
    {auto x=frame();auto i=begin(x);initial_work(*i,x);input(x,3);x.group.life=f::Life::Respawning;x.body.bag=14;x.body.gold=60;check(i->choose(x).reason==e::Reason::MissingPayout,"unattributed capture cannot hold forever");}
    {auto x=frame();auto i=begin(x);initial_work(*i,x);capture(*i,x);bank(*i,x);input(x,6);x.body.channel=f::Channel::None;x.body.channel_ticks=0;x.body.bag=0;
        check(i->choose(x).reason==e::Reason::MissingPayout&&!i->facts().bank_attributed,"bag0 alone supplies no payout");}
    {auto x=frame();auto i=begin(x);initial_work(*i,x);capture(*i,x);const auto token=bank(*i,x);input(x,5);x.body.channel=f::Channel::None;x.body.bag=14;
        auto r=receipt(x,2,e::ReceiptKind::Interrupted,token);x.receipts={&r,1};check(i->choose(x).reason==e::Reason::Interrupted,"retained bag interruption has its own outcome");}
    {auto x=frame();auto i=begin(x);initial_work(*i,x);capture(*i,x);bank(*i,x);input(x,5);x.body.channel=f::Channel::None;x.body.bag=14;x.body.stun_present=true;x.body.completed=true;
        check(i->choose(x).reason==e::Reason::Interrupted,"actual stopped channel reconciles before stun or completed-input return");}
    {auto x=frame();auto i=begin(x);input(x,1);x.group.id=9;check(i->choose(x).reason==e::Reason::Binding,"different source group cannot continue");}
    {auto x=frame();auto i=begin(x);input(x,1);x.group.generation=2;check(i->choose(x).reason==e::Reason::Generation,"unearned generation cannot become Renew");}
    {auto x=frame();auto i=begin(x);input(x,14);check(i->choose(x).reason==e::Reason::Date,"expired forecast dates require replan");}
    {auto x=frame();auto i=begin(x);input(x,1);x.roster_complete=false;check(i->choose(x).reason==e::Reason::UnknownRoster,"partial current roster cannot nominate attack");}
    {auto x=frame();auto i=begin(x);input(x,1);x.camps[0].id=6;check(i->choose(x).reason==e::Reason::UnknownRoster,"same group label cannot replace actual member identity");}
    {auto x=frame();auto i=begin(x);input(x,1);x.body.kit=f::Kit::Willow;check(i->choose(x).reason==e::Reason::ChangedPremise,"actual kit change invalidates selected physical policy");}
    {auto x=frame();auto i=begin(x);i->invalidate();check(i->choose(x).reason==e::Reason::ChangedPremise,"known new foe or model change hands off");}
}
static void target_and_receipt_cases(){
    {auto x=frame();x.npcs=2;x.camps[1]=x.camps[0];x.camps[1].id=6;x.camps[1].pose={2,2};x.camps[0].hp=50;auto i=begin(x);
        auto d=command(*i,x,f::Action::Basic);readback(x);x.body.a=false;x.camps[0].hp=0;check(i->attempt(d,e::Attempt::Accepted,x),"first target kill retains owner input");
        d=command(*i,x,f::Action::Reset);readback(x);x.body.a=true;x.body.c=false;x.body.cooldown[0]=8;check(i->attempt(d,e::Attempt::Accepted,x),"reset after target death");
        d=command(*i,x,f::Action::Basic);check(d.primitive.target==6,"fresh live second target gets remaining Basic");readback(x);x.body.a=false;x.camps[1].hp=50;check(i->attempt(d,e::Attempt::Accepted,x),"second target actual damage");check(i->facts().work_damage==120,"observed member deltas accumulate");}
    {auto x=frame();x.npcs=2;x.camps[1]=x.camps[0];x.camps[1].id=6;x.camps[1].pose={3,1};
        auto p=program();p.route[p.legs[2].begin].destination={3,2};p.legs[3].origin={3,2};const auto source=observed_source(x);const auto proof=selected(source,p);e::Interpreter i;
        check(i.begin(x,p,proof,binding(x),source),"initial complete splash family joins actual world");input(x,1);x.camps[0].hp=50;
        auto d=command(i,x,f::Action::Basic);readback(x);x.body.a=false;x.camps[0].hp=0;x.camps[1].hp=50;check(i.attempt(d,e::Attempt::Accepted,x),"fresh partial camp work changes the remaining standing family");
        const auto next=i.choose(x);check(next.kind==e::Kind::Handoff&&next.reason==e::Reason::Target,"standing solo cannot wait for omitted help on out-of-range member");}
    {auto x=frame();auto i=begin(x);auto r=receipt(x,1,e::ReceiptKind::CustodyLost);r.program=100;x.receipts={&r,1};check(i->choose(x).kind==e::Kind::Physical,"other programme receipt gives no authority or refutation");}
    {auto x=frame();auto i=begin(x);std::array<e::Receipt,2> rs{receipt(x,1,e::ReceiptKind::Healing),receipt(x,1,e::ReceiptKind::Healing)};rs[0].healing_amount=1;rs[1].healing_amount=2;x.receipts=rs;
        check(i->choose(x).reason==e::Reason::Receipt,"conflicting duplicate receipt batch rejected before credit");check(!i->facts().healing_observed,"conflict earns no healing credit");}
    {auto x=frame();auto i=begin(x);std::array<e::Receipt,2> rs{receipt(x,2,e::ReceiptKind::Healing),receipt(x,1,e::ReceiptKind::Healing)};x.receipts=rs;
        check(i->choose(x).reason==e::Reason::Receipt,"unordered receipt batch does not silently drop a credit");}
    {auto x=frame();auto i=begin(x);auto r=receipt(x,1,e::ReceiptKind::Healing);r.observed.effect=1;r.healing_amount=1;x.receipts={&r,1};
        check(i->choose(x).reason==e::Reason::Receipt,"receipt may not claim a future physical effect");}
    {auto x=frame();x.stamp.sample=50;x.stamp.effect=10;auto i=begin(x);input(x,1);auto r=receipt(x,1,e::ReceiptKind::Healing);r.observed.sample=1;r.observed.effect=0;r.healing_amount=1;x.receipts={&r,1};
        check(i->choose(x).reason==e::Reason::Receipt&&!i->facts().healing_observed,"later input cannot reset receipt sample/effect below episode origin");}
    {auto x=frame();auto i=begin(x);initial_work(*i,x);capture(*i,x);const auto token=bank(*i,x);const auto old=x.stamp;input(x,6);x.body.channel=f::Channel::None;x.body.bag=0;
        auto r=receipt(x,2,e::ReceiptKind::BankPayout,token);r.source_bag=r.gems=14;r.observed=old;x.receipts={&r,1};
        check(i->choose(x).reason==e::Reason::Receipt&&!i->facts().bank_attributed,"payout cannot predate the next owner start after Submit");}
    {auto x=frame();auto i=begin(x);initial_work(*i,x);capture(*i,x);const auto token=bank(*i,x);input(x,6);x.body.channel=f::Channel::None;x.body.bag=0;
        auto r=receipt(x,2,e::ReceiptKind::BankPayout,token);r.source_bag=r.gems=14;r.observed.sample=1;r.observed.effect=0;x.receipts={&r,1};
        check(i->choose(x).reason==e::Reason::Receipt&&!i->facts().bank_attributed,"later payout time cannot reuse pre-Submit sample/effect");}
    {auto x=frame();auto i=begin(x);auto d=command(*i,x,f::Action::Basic);readback(x);x.body.a=false;x.camps[0].hp=121;check(!i->attempt(d,e::Attempt::Accepted,x),"target growth after Basic requires fresh model");}
    {auto x=frame();auto p=program();p.bodies[0].use_recall=true;e::Interpreter i;check(!i.begin(x,p,selected(),binding(x),world())&&i.reason()==e::Reason::RecallUnsupported,"walking class never fabricates Recall legality");}
    {auto x=frame();auto p=program();p.bodies[0].heal_for_work=true;e::Interpreter i;check(!i.begin(x,p,selected(),binding(x),world())&&i.reason()==e::Reason::HealUnsupported,"unrepresented healer recipient is explicit unsupported");}
    {auto x=frame();auto p=program();p.legs[0].complete=false;e::Interpreter i;check(!i.begin(x,p,selected(),binding(x),world())&&i.reason()==e::Reason::Route,"missing whole leg rejected before physical command");}
    {auto x=frame();auto p=program();p.route_count=f::route_limit+1;e::Interpreter i;check(!i.begin(x,p,selected(),binding(x),world())&&i.reason()==e::Reason::Capacity,"capacity checked before route read");}
    {auto x=frame();auto proof=selected();proof.status=f::Status::Unknown;e::Interpreter i;check(!i.begin(x,program(),proof,binding(x),world()),"incomplete forecast cannot be relabeled complete policy");}
    {auto x=frame();x.body.hp=799;e::Interpreter i;check(!i.begin(x,program(),selected(),binding(x),world())&&i.reason()==e::Reason::Binding,"actual root HP must join the immutable captured source");}
    {auto x=frame();auto source=world();source.camps[0].hp=119;e::Interpreter i;check(!i.begin(x,program(),selected(),binding(x),source)&&i.reason()==e::Reason::Binding,"actual member HP must join captured source");}
}
int main(){full_cycle(true);full_cycle(false);failures_and_freshness();target_and_receipt_cases();std::cout<<"capital_service_execution "<<checks<<" checks "<<failures<<" failures\n";return failures?EXIT_FAILURE:EXIT_SUCCESS;}
