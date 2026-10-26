#include "../src/capital_party.h"
#include "../src/capital_party_wire.h"
#include <cstdio>
#include <limits>
using namespace wb::capital::party;
namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(false)
Contract contract() {
    Contract c;c.id=19;c.group=2;c.start_round=1;c.camp_count=3;c.camp_ids={101,102,103};
    c.members[0]={4,Role::Varg,{7,10},{10,10},{11,10},{7,10},{6,10},{3,10},{2,10},true,true,true,false};
    c.members[1]={1,Role::Willow,{9,15},{10,14},{11,11},{10,15},{6,10},{3,10},{3,9},true,true,true,false};
    c.members[2]={7,Role::Vendetta,{15,10},{13,10},{12,11},{16,10},{6,10},{3,10},{4,10},true,true,true,false};
    c.capture_mask=7;c.capture_ticks=2;c.queried_respawn_ticks=3;c.max_deferrals=3;c.max_refusals=2;c.abort_ticks=4;
    c.reward_gems=11;c.reward_gold=17;c.join_by=3;c.arrive_by=5;c.work_by=10;c.capture_by=15;
    c.release_by=18;c.convert_by=24;c.recover_by=28;c.renew_by=32;
    c.geometry_supported=c.individual_routes_supported=c.actual_order_model_supported=true;
    c.slot_count=5;
    c.slots[0]={6,4,101,-1,Kind::WorkTank,-1};
    c.slots[1]={6,1,-1,4,Kind::SupportHeal,-1};
    c.slots[2]={6,7,102,-1,Kind::Strike,-1};
    c.slots[3]={7,4,101,-1,Kind::WorkTank,1};
    c.slots[4]={7,7,102,-1,Kind::Strike,-1};
    return c;
}
Contract shared_melee_contract() {
    auto c=contract();c.id=20;c.camp_count=1;c.camp_ids={101};
    for(unsigned i=0;i<actors;++i)c.members[i].id=static_cast<int>(i);
    c.members[2].station={12,10}; // Varg and Vendetta both adjacent to (11,10).
    c.slot_count=4;c.slots[0].actor=0;c.slots[1].actor=1;c.slots[1].recipient=0;
    c.slots[2].actor=2;c.slots[2].camp=101;c.slots[3].actor=0;
    return c;
}
Point step(Point a,Point b){return {a.x+(b.x>a.x)-(b.x<a.x),a.y+(b.y>a.y)-(b.y<a.y)};}
struct Simulation {
    Contract c;Controller controller;
    std::array<Body,actors> body{};std::array<bool,actors> committed{},paid{};
    std::array<float,3> hp{100,100,0};std::array<int,actors> channel_start{-1,-1,-1};
    std::array<unsigned,actors> channel_ticks{};
    std::uint64_t score{};bool claimed{};
    explicit Simulation(Contract selected=contract()):c(selected),controller(c) {
        for(unsigned i=0;i<actors;++i){auto& b=body[i];b.id=c.members[i].id;b.role=c.members[i].role;
            b.position=c.members[i].rendezvous;b.hp=b.max_hp=100;b.alive=b.move=b.attack=b.cast=true;
            b.shield_ready=b.reset_ready=b.heal_ready=b.dash_ready=true;b.shield_amount=20;b.heal_amount=25;b.heal_range=4;
            b.capital_revision=5;b.recall_ticks=2;}
    }
    void locations(unsigned i) {
        auto& b=body[i];b.at_station=b.position==c.members[i].station;b.at_capture=b.position==c.members[i].capture;
        b.at_spawn=b.position==c.members[i].spawn;b.altar_adjacent=distance(b.position,c.members[i].altar)<=1;
        b.on_any_source=b.position==Point{11,10}||b.position==Point{11,11}||b.position==Point{12,11};
        b.npc_quiet=distance(b.position,{11,10})>3&&distance(b.position,{11,11})>3;
        b.submit_ticks=static_cast<unsigned>((b.bag+2)/3); // supplied query result in this model fixture
        if(!b.submit_ticks)b.submit_ticks=1;b.team_score=score;
    }
    Frame frame(unsigned i,int round) {
        for(unsigned j=0;j<actors;++j)locations(j);
        Frame f;f.owner=body[i].id;f.round=round;f.input=static_cast<std::uint64_t>(round*10+i+1);f.fresh=true;f.self=body[i];
        for(unsigned j=0;j<actors;++j){f.peers[j]={body[j],true};f.commitment_known[j]=true;f.commitment_matches[j]=committed[j];}
        f.group.id=c.group;f.group.count=c.camp_count;f.group.observed_now=true;f.group.claimed=claimed;f.group.capture_own=true;
        f.group.whole_clear=true;
        const std::array<Point,3> source{{{11,10},{11,11},{12,11}}};
        for(unsigned j=0;j<c.camp_count;++j){f.group.camps[j]={c.camp_ids[j],hp[j],true,hp[j]>0,source[j],3,true};
            f.group.whole_clear&=hp[j]==0;}
        f.payout={paid[i],c.group,7};return f;
    }
    Guard guard(const Frame& f,const WordIntent& w,Cast cast=Cast::None) {
        Guard g;g.owner=f.owner;g.round=f.round;g.input=f.input;g.capital_revision=f.self.capital_revision;
        g.kind=w.kind;g.destination=w.destination;g.camp=w.camp;g.recipient=w.recipient;
        g.word_token=f.input*100+static_cast<unsigned>(w.kind)+1;
        g.observed_hp=f.self.hp;g.observed_shield=f.self.shield;g.hp_lower=1;
        g.horizon_round=f.round+std::max(1u,f.self.submit_ticks);g.selected_cast=cast;
        if(w.kind==Kind::Submit)g.priced_channel_ticks=f.self.submit_ticks;
        if(w.kind==Kind::Recover){g.priced_channel_ticks=f.self.recall_ticks;g.horizon_round=f.round+static_cast<int>(f.self.recall_ticks);}
        g.fresh=g.legal_word=g.npc_covered=g.foes_covered=g.effects_covered=g.refusals_covered=true;
        g.individual_exposure_covered=g.actual_order_covered=g.station_closure_covered=true;
        auto& a=g.aggro;a.round=f.round+1;a.order.basis=OrderBasis::CommittedNextRound;
        a.order.observed_round=f.round;a.order.phase_round=a.round;a.order.no_priority_writes_before_phase=true;
        for(unsigned j=0;j<actors;++j){a.observed_positions[j]=a.phase_positions[j]=f.peers[j].body.position;
            a.positions_covered[j]=a.alive_through_phase_covered[j]=true;a.hp_lower_after_phase[j]=1;
            a.order.members[j]={c.members[j].id,c.members[j].id,c.camp_count==1?0:2-static_cast<int>(j),true,true,true};}
        for(unsigned j=0;j<f.group.count;++j)if(f.group.camps[j].alive) {
            const auto& source=f.group.camps[j];int tank=-1,nearest=source.attack_range+1;
            for(unsigned k=0;k<actors;++k){const int d=distance(source.position,a.phase_positions[k]);
                if(d<nearest){tank=static_cast<int>(k);nearest=d;}}
            a.monsters[a.count++]={source.id,tank<0?-1:c.members[static_cast<unsigned>(tank)].id,true};
        }
        return g;
    }
    Decision issue(unsigned i,int round,Cast cast=Cast::None) {
        auto f=frame(i,round);auto d=controller.tick(f);
        if(d.status==Status::NeedModel&&d.intent.kind!=Kind::None){f.guard=guard(f,d.intent,cast);d=controller.tick(f);}
        return d;
    }
    void add(Tape& t,EffectKind k,int target=-1,float before=0,float after=0,Point from={},Point to={},unsigned timer=0) {
        t.effects[t.count++]={k,target,from,to,before,after,timer,true,true};
    }
    void move(unsigned i,Point destination,Tape& t) {
        if(body[i].position==destination)return;
        const auto from=body[i].position;body[i].position=step(from,destination);body[i].move=false;
        add(t,EffectKind::Move,-1,0,0,from,body[i].position);
    }
    void execute(unsigned i,int round,const Decision& d,Controller* recipient=nullptr) {
        if(d.status!=Status::Ready)return;
        const int script_round=round-c.start_round+1;
        Tape t;t.complete=true;const auto& w=d.intent;t.word_token=w.word_token;
        if(w.kind==Kind::Commit){committed[i]=true;add(t,EffectKind::CommitPublished);}
        else if(w.kind==Kind::WorkTank) {
            if(script_round==6){const auto old=body[i].shield;body[i].shield=20;body[i].cast=false;
                add(t,EffectKind::ShieldCast,-1,old,20);add(t,EffectKind::BasicDamage,101,hp[0],70);hp[0]=70;}
            else {const float remainder=c.camp_count==1?15.f:35.f;
                add(t,EffectKind::BasicDamage,101,hp[0],remainder);hp[0]=remainder;
                add(t,EffectKind::ResetCast);body[i].cast=false;add(t,EffectKind::BasicDamage,101,remainder,0);hp[0]=0;}
            body[i].attack=false;
        } else if(w.kind==Kind::SupportHeal) {
            const int target=detail::member(c,w.recipient);const auto before=body[target].hp;
            body[target].hp=std::min(body[target].max_hp,before+body[i].heal_amount);body[i].cast=false;
            add(t,EffectKind::HealCast,w.recipient,before,body[target].hp);
        } else if(w.kind==Kind::Strike) {
            if(c.camp_count==1) {
                add(t,EffectKind::BasicDamage,101,hp[0],30);hp[0]=30;body[i].attack=false;
            } else {
            const auto station=body[i].position;move(i,{12,10},t);
            add(t,EffectKind::BasicDamage,102,hp[1],script_round==6?40.f:0.f);hp[1]=script_round==6?40.f:0.f;body[i].attack=false;
            const auto from=body[i].position;body[i].position=station;body[i].cast=false;
            add(t,EffectKind::Dash,-1,0,0,from,station);
            }
        } else if(w.kind==Kind::Submit) {
            body[i].channel=Channel::Submit;channel_start[i]=round;channel_ticks[i]=body[i].submit_ticks;
            body[i].channel_remaining=channel_ticks[i];add(t,EffectKind::SubmitStarted,-1,0,0,{}, {},channel_ticks[i]);
        } else if(w.kind==Kind::Recover&&body[i].channel==Channel::None) {
            body[i].channel=Channel::Recall;channel_start[i]=round;channel_ticks[i]=body[i].recall_ticks;
            body[i].channel_remaining=channel_ticks[i];add(t,EffectKind::RecallStarted,-1,0,0,{}, {},channel_ticks[i]);
        } else if(w.kind==Kind::Assemble||w.kind==Kind::Enter||w.kind==Kind::CaptureHold||w.kind==Kind::Release
            ||w.kind==Kind::Renew||w.kind==Kind::EmergencyExit)move(i,w.destination,t);
        else if(w.kind==Kind::ApproachAltar) {
            if(distance(body[i].position,w.destination)>1)move(i,w.destination,t);
        }
        const auto after=frame(i,round);CHECK((recipient?*recipient:controller).confirm(w,after,t));
    }
    void begin_round(int round) {
        const int script_round=round-c.start_round+1;
        if(script_round==6)body[0].hp=75; // actual individual damage, no shared tank pool
        if(c.camp_count==1&&script_round==7)body[0].hp-=25; // next M6 tie hits Varg, before the R7 heal/action.
        if(script_round==11&&hp[0]==0&&(c.camp_count==1||hp[1]==0)) {
            bool all=true;for(unsigned i=0;i<actors;++i)all&=body[i].position==c.members[i].capture;
            CHECK(all);
            if(all){claimed=true;for(unsigned i=0;i<actors;++i){paid[i]=true;body[i].bag+=detail::share(c,i,c.reward_gems);body[i].gold+=detail::share(c,i,c.reward_gold);}}
        }
        for(unsigned i=0;i<actors;++i) {
            auto& b=body[i];
            if(b.channel!=Channel::None&&round>=channel_start[i]+static_cast<int>(channel_ticks[i])) {
                if(b.channel==Channel::Submit){score+=b.bag;b.bag=0;}
                else if(b.channel==Channel::Recall){b.position=c.members[i].spawn;b.hp=50;}
                b.channel=Channel::None;b.channel_remaining=0;
            }
            if(b.channel==Channel::None)b.move=b.attack=b.cast=true;
        }
    }
    void end_round(){for(unsigned i=0;i<actors;++i)if(body[i].position==c.members[i].spawn)body[i].hp=body[i].max_hp;}
};
void day(Simulation& s,int round) {
    s.begin_round(round);
    for(unsigned i=0;i<actors;++i){Cast cast=Cast::None;
        if(s.controller.phase()==Phase::Work&&round==6)cast=i==0?Cast::Shield:i==1?Cast::Heal:s.c.camp_count==1?Cast::None:Cast::Dash;
        if(s.controller.phase()==Phase::Work&&round==7)cast=i==0?Cast::Reset:i==2&&s.c.camp_count!=1?Cast::Dash:Cast::None;
        const auto d=s.issue(i,round,cast);s.execute(i,round,d);}
    s.end_round();
}
void whole_pipeline() {
    Simulation s;
    for(int r=1;r<=32&&s.controller.phase()!=Phase::Complete&&s.controller.phase()!=Phase::Closed;++r) {
        s.begin_round(r);
        for(unsigned i=0;i<actors;++i) {
            Cast cast=Cast::None;
            if(s.controller.phase()==Phase::Work&&r==6)cast=i==0?Cast::Shield:i==1?Cast::Heal:Cast::Dash;
            if(s.controller.phase()==Phase::Work&&r==7)cast=i==0?Cast::Reset:i==2?Cast::Dash:Cast::None;
            const auto d=s.issue(i,r,cast);s.execute(i,r,d);
        }
        s.end_round();
    }
    const auto& result=s.controller.result();
    CHECK(s.controller.phase()==Phase::Complete);CHECK(result.complete_observed&&result.useful_support_observed&&result.cleanup_complete);
    CHECK(result.members[0].camp_work==100&&result.members[2].camp_work==100&&result.members[1].useful_heal==25);
    CHECK(result.members[0].actual_shield==20);
    CHECK(result.members[0].captured_gems==3&&result.members[1].captured_gems==5&&result.members[2].captured_gems==3);
    CHECK(result.members[0].captured_gold==5&&result.members[1].captured_gold==7&&result.members[2].captured_gold==5);
    CHECK(s.score==11);
    for(unsigned i=0;i<actors;++i){CHECK(result.members[i].committed&&result.members[i].arrived&&result.members[i].entered);
        CHECK(result.members[i].receipt&&result.members[i].released&&result.members[i].banked&&result.members[i].recovered&&result.members[i].renewed);
        CHECK(result.members[i].banked_party_gems==result.members[i].captured_gems);}
    for(unsigned i=0;i<3;++i)CHECK(result.residual_known[i]&&result.residual_hp[i]==0);
}
void contract_and_model_separation() {
    auto c=contract();CHECK(detail::valid(c));
    c.members[1].role=Role::Varg;CHECK(!detail::valid(c));c=contract();c.members[0].rendezvous_quiet=false;CHECK(!detail::valid(c));
    c=contract();c.slots[3].required_support_slot=4;CHECK(!detail::valid(c));
    Estimate j;j.supported=true;j.start_round=1;j.horizon_round=32;j.actor={4,1,7};j.banked={3,5,3};j.displaced_inputs={20,20,20};
    std::array<Estimate,actors> solo{};
    for(unsigned i=0;i<actors;++i){solo[i].supported=true;solo[i].start_round=1;solo[i].horizon_round=32;solo[i].actor[i]=j.actor[i];solo[i].banked[i]=2;solo[i].displaced_inputs[i]=10;}
    auto comparison=compare_modeled(j,solo);CHECK(comparison.comparable&&comparison.modeled_only&&comparison.banked_difference==5&&comparison.input_difference==30);
    Controller unplayed(contract());CHECK(!unplayed.result().complete_observed);
    solo[2].horizon_round=33;CHECK(!compare_modeled(j,solo).comparable);
}
void rejection_and_missing_authority() {
    Simulation priced;auto naked=priced.frame(0,1);auto proposed=priced.controller.tick(naked);
    CHECK(proposed.status==Status::NeedModel&&!proposed.intent.guarded);
    naked.guard=priced.guard(naked,proposed.intent);naked.self.npc_quiet=false;
    CHECK(priced.controller.tick(naked).status==Status::NeedModel); // publishing is a physical input, not free waiting
    naked.self.npc_quiet=true;naked.guard.future_support_borrowed=true;
    CHECK(priced.controller.tick(naked).status==Status::NeedModel);
    Simulation s;auto f=s.frame(0,1);auto d=s.issue(0,1);CHECK(d.status==Status::Ready&&d.intent.kind==Kind::Commit);
    CHECK(s.controller.tick(f).status==Status::AlreadyIssued);
    auto after=f;Tape t;t.complete=true;t.word_token=d.intent.word_token;s.add(t,EffectKind::CommitPublished);
    CHECK(!s.controller.confirm(d.intent,after,t)); // unpublished commitment is not an effect
    CHECK(s.controller.phase()==Phase::AbortCleanup);
    Simulation dead;f=dead.frame(0,1);f.self.alive=false;f.self.hp=0;
    CHECK(dead.controller.tick(f).status==Status::Aborting);CHECK(!dead.controller.result().complete_observed);
    f.round=6;f.input=61;CHECK(dead.controller.tick(f).status==Status::Closed);
    CHECK(!dead.controller.result().cleanup_complete);
}
void reach_work(Simulation& s) {
    for(int r=1;r<=5;++r){s.begin_round(r);for(unsigned i=0;i<actors;++i){const auto d=s.issue(i,r);s.execute(i,r,d);}s.end_round();}
    CHECK(s.controller.phase()==Phase::Work);
}
void shared_melee_ownership() {
    Simulation s(shared_melee_contract());reach_work(s);s.begin_round(6);
    auto f=s.frame(0,6);const auto proposed=s.controller.tick(f);
    CHECK(proposed.status==Status::NeedModel&&proposed.intent.kind==Kind::WorkTank);
    const auto good=s.guard(f,proposed.intent,Cast::Shield);
    CHECK(f.group.count==1&&good.aggro.count==1);
    CHECK(distance(f.peers[0].body.position,f.group.camps[0].position)==1);
    CHECK(distance(f.peers[2].body.position,f.group.camps[0].position)==1);
    CHECK(good.aggro.order.members[0].priority==good.aggro.order.members[2].priority);
    CHECK(good.aggro.order.members[0].draft_ordinal<good.aggro.order.members[2].draft_ordinal);
    CHECK(good.aggro.monsters[0].tank==s.c.members[0].id&&detail::aggro(s.c,f,good));
    auto reject=[&](const Guard& g){CHECK(!detail::aggro(s.c,f,g));};
    auto bad=good;bad.aggro.order.basis=OrderBasis::Unknown;reject(bad);
    bad=good;bad.aggro.order.basis=OrderBasis::ActualCurrentRound;reject(bad); // current rank is not next camp rank
    bad=good;bad.aggro.order.observed_round=5;reject(bad);
    bad=good;bad.aggro.order.phase_round=6;reject(bad);
    bad=good;bad.aggro.round=8;reject(bad);
    bad=good;bad.horizon_round=8;reject(bad); // initial proof covers exactly one NPC phase
    bad=good;bad.aggro.order.no_priority_writes_before_phase=false;reject(bad); // includes the existing bank pivot
    bad=good;bad.aggro.order.members[2].next_priority_committed=false;reject(bad);
    bad=good;bad.aggro.order.members[0].priority_queried=false;reject(bad);
    bad=good;bad.aggro.order.members[0].draft_accepted=false;reject(bad);
    bad=good;bad.aggro.order.members[0].actor=99;reject(bad);
    bad=good;bad.aggro.order.members[0].draft_ordinal=3;reject(bad); // contradicts actual actor/draft mapping
    for(unsigned i=0;i<actors;++i) {
        bad=good;bad.aggro.positions_covered[i]=false;reject(bad);
        bad=good;bad.aggro.alive_through_phase_covered[i]=false;reject(bad);
        bad=good;bad.aggro.hp_lower_after_phase[i]=0;reject(bad);
    }
    bad=good;bad.aggro.observed_positions[2]={13,10};reject(bad);
    bad=good;bad.aggro.monsters[0].other_champions_excluded=false;reject(bad);
    bad=good;bad.aggro.monsters[0].tank=s.c.members[2].id;reject(bad);
    bad=good;bad.aggro.count=0;reject(bad);
    bad=good;bad.aggro.monsters[1]=bad.aggro.monsters[0];bad.aggro.count=2;reject(bad);
    bad=good;bad.aggro.order.members[2].priority=1;reject(bad); // stale Varg-first proof after an accepted pivot promotion
    bad.aggro.monsters[0].tank=s.c.members[2].id;
    CHECK(detail::aggro(s.c,f,bad)); // newly priced, actually committed Vendetta-first exposure is a different proof
    auto incomplete=f;incomplete.peers[2].observed_now=false;CHECK(!detail::aggro(s.c,incomplete,good));
    incomplete=f;incomplete.group.camps[0].geometry_known=false;CHECK(!detail::aggro(s.c,incomplete,good));
    incomplete=f;incomplete.group.camps[0].attack_range=-1;CHECK(!detail::aggro(s.c,incomplete,good));
    incomplete=f;incomplete.group.camps[0].alive=false;CHECK(!detail::aggro(s.c,incomplete,good));
    incomplete=f;incomplete.group.count=2;CHECK(!detail::aggro(s.c,incomplete,good));
    f.guard=good;auto issued=s.controller.tick(f);CHECK(issued.status==Status::Ready);
    s.execute(0,6,issued);
    for(unsigned i=1;i<actors;++i){issued=s.issue(i,6,i==1?Cast::Heal:Cast::None);s.execute(i,6,issued);}
    CHECK(s.body[0].hp==100&&s.body[2].hp==100&&s.hp[0]==30);
    s.end_round();
    s.begin_round(7);
    CHECK(s.body[0].hp==75&&s.body[2].hp==100); // one actual individual NPC loss; no shared capacity or later heal borrowed
    for(unsigned i=0;i<actors;++i){issued=s.issue(i,7,i==0?Cast::Reset:Cast::None);s.execute(i,7,issued);}
    s.end_round();
    for(int r=8;r<=32&&s.controller.phase()!=Phase::Complete&&s.controller.phase()!=Phase::Closed;++r)day(s,r);
    const auto& result=s.controller.result();
    CHECK(s.controller.phase()==Phase::Complete&&result.complete_observed);
    CHECK(result.members[0].camp_work==60&&result.members[2].camp_work==40&&result.members[1].useful_heal==25);
    CHECK(s.body[0].hp==100&&s.body[2].hp==100&&result.residual_known[0]&&result.residual_hp[0]==0);
    CHECK(s.score==s.c.reward_gems);
    for(unsigned i=0;i<actors;++i)CHECK(result.members[i].receipt&&result.members[i].released&&result.members[i].banked
        &&result.members[i].recovered&&result.members[i].renewed&&result.members[i].banked_party_gems==detail::share(s.c,i,s.c.reward_gems));
}
void split_monster_ownership() {
    Simulation s;reach_work(s);s.begin_round(6);auto f=s.frame(0,6);const auto d=s.controller.tick(f);
    f.group.camps[1].position={14,10};auto g=s.guard(f,d.intent,Cast::Shield);
    CHECK(g.aggro.count==2&&g.aggro.monsters[0].tank==s.c.members[0].id&&g.aggro.monsters[1].tank==s.c.members[2].id);
    CHECK(detail::aggro(s.c,f,g));
    g.aggro.alive_through_phase_covered[0]=false;CHECK(!detail::aggro(s.c,f,g));
    g.aggro.alive_through_phase_covered[0]=true;g.aggro.monsters[1].tank=s.c.members[0].id;
    CHECK(!detail::aggro(s.c,f,g));
}
void individual_and_cast_guards() {
    Simulation s;reach_work(s);s.begin_round(6);auto f=s.frame(0,6);auto d=s.controller.tick(f);
    CHECK(d.status==Status::NeedModel&&d.intent.kind==Kind::WorkTank);
    f.guard=s.guard(f,d.intent,Cast::Shield);f.guard.future_support_borrowed=true;
    CHECK(s.controller.tick(f).status==Status::NeedModel);
    f.guard.future_support_borrowed=false;f.guard.hp_lower=0;CHECK(s.controller.tick(f).status==Status::NeedModel);
    f.guard.hp_lower=1;f.guard.aggro.positions_covered[0]=false;CHECK(s.controller.tick(f).status==Status::NeedModel);
    f.guard.aggro.positions_covered[0]=true;d=s.controller.tick(f);CHECK(d.status==Status::Ready);
    auto after=f;after.self.shield=20;Tape t;t.complete=true;t.word_token=d.intent.word_token;s.add(t,EffectKind::ShieldCast,-1,0,20);
    s.add(t,EffectKind::BasicDamage,101,100,70);s.add(t,EffectKind::ResetCast);after.group.camps[0].hp=70;
    CHECK(!s.controller.confirm(d.intent,after,t));CHECK(s.controller.phase()==Phase::AbortCleanup);
    CHECK(s.controller.result().residual_known[0]&&s.controller.result().residual_hp[0]==70);
}
void support_and_arrival_failure() {
    Simulation s;reach_work(s);s.begin_round(6);auto d=s.issue(0,6,Cast::Shield);s.execute(0,6,d);
    auto f=s.frame(1,6);d=s.controller.tick(f);f.guard=s.guard(f,d.intent,Cast::Heal);
    f.peers[0].observed_now=false;CHECK(s.controller.tick(f).status==Status::NeedModel);
    f.peers[0].observed_now=true;f.self.heal_range=1;CHECK(s.controller.tick(f).status==Status::NeedModel);
    f.self.heal_range=4;d=s.controller.tick(f);CHECK(d.status==Status::Ready);
    auto after=f;Tape t;t.complete=true;t.word_token=d.intent.word_token; // a promised heal that never happened aborts
    CHECK(!s.controller.confirm(d.intent,after,t));CHECK(s.controller.phase()==Phase::AbortCleanup);
    CHECK(!s.controller.result().useful_support_observed);
    Simulation missing;
    for(unsigned i=0;i<actors;++i){auto x=missing.issue(i,1);missing.execute(i,1,x);}
    auto m=missing.frame(0,2);m.commitment_known[2]=false;
    CHECK(missing.controller.tick(m).status==Status::NeedObservation);
    m.round=3;m.input=31;missing.controller.tick(m);m.round=4;m.input=41;missing.controller.tick(m);
    CHECK(missing.controller.phase()==Phase::AbortCleanup);
}
void effect_and_receipt_authentication() {
    Simulation intent;auto f=intent.frame(0,1);const auto issued=intent.issue(0,1);
    auto forged=issued.intent;forged.kind=Kind::WorkTank;forged.slot=999;Tape empty;empty.complete=true;
    CHECK(!intent.controller.confirm(forged,f,empty));CHECK(intent.controller.phase()==Phase::Commit);
    intent.execute(0,1,issued);CHECK(intent.controller.result().members[0].committed);
    Simulation heal;reach_work(heal);heal.begin_round(6);auto d=heal.issue(0,6,Cast::Shield);heal.execute(0,6,d);
    f=heal.frame(1,6);d=heal.controller.tick(f);f.guard=heal.guard(f,d.intent,Cast::Heal);
    f.peers[0].body.id=99;CHECK(heal.controller.tick(f).status==Status::NeedModel);
    f.peers[0].body.id=4;d=heal.controller.tick(f);CHECK(d.status==Status::Ready);
    auto after=f;after.peers[0].body.id=99;after.peers[0].body.hp=100;
    Tape wrong;wrong.complete=true;wrong.word_token=d.intent.word_token;heal.add(wrong,EffectKind::HealCast,4,75,100);
    CHECK(!heal.controller.confirm(d.intent,after,wrong));CHECK(!heal.controller.result().useful_support_observed);
    Simulation capture;for(int r=1;r<=10;++r)day(capture,r);capture.begin_round(11);
    f=capture.frame(0,11);f.payout.participants_mask=3;
    capture.controller.tick(f);CHECK(capture.controller.phase()==Phase::AbortCleanup);
    CHECK(!capture.controller.result().members[0].receipt&&capture.body[0].bag==3);
}
void bank_failure_refusal_and_partial_death() {
    Simulation bank;for(int r=1;r<=16;++r)day(bank,r);
    CHECK(bank.body[0].channel==Channel::Submit&&bank.body[0].bag==3);
    bank.body[0].channel=Channel::None;bank.begin_round(17);auto f=bank.frame(0,17);
    bank.controller.tick(f);CHECK(bank.controller.phase()==Phase::AbortCleanup);
    CHECK(!bank.controller.result().members[0].banked&&bank.body[0].bag==3);
    Simulation refusal;for(unsigned i=0;i<actors;++i){const auto d=refusal.issue(i,1);refusal.execute(i,1,d);}
    refusal.body[0].position={6,10};
    for(int r=2;r<=3;++r){const auto d=refusal.issue(0,r);CHECK(d.status==Status::Ready);
        Tape failed;failed.word_token=d.intent.word_token;failed.refused=true;
        CHECK(refusal.controller.confirm(d.intent,refusal.frame(0,r),failed));}
    CHECK(refusal.controller.phase()==Phase::AbortCleanup);
    CHECK(refusal.controller.result().failure==Failure::Refusal);
    f=refusal.frame(0,8);CHECK(refusal.controller.tick(f).status==Status::Closed);
    CHECK(!refusal.controller.result().cleanup_complete);
    Simulation death;for(int r=1;r<=6;++r)day(death,r);
    death.body[0].alive=false;death.body[0].hp=0;f=death.frame(0,7);death.controller.tick(f);
    CHECK(death.controller.phase()==Phase::AbortCleanup&&!death.controller.result().complete_observed);
    CHECK(death.controller.result().residual_hp[0]==70&&death.controller.result().residual_hp[1]==40);
    CHECK(death.controller.result().members[0].camp_work==30&&death.controller.result().members[2].camp_work==60);
}
void gap_enum_and_current_closure() {
    Simulation gap;for(int r=1;r<=16;++r)day(gap,r);
    gap.body[0].channel=Channel::None;gap.body[0].bag=0;gap.score=3;
    auto f=gap.frame(0,20);gap.controller.tick(f);
    CHECK(gap.controller.phase()==Phase::AbortCleanup&&!gap.controller.result().members[0].banked);
    CHECK(gap.controller.result().failure==Failure::NeedObservation); // death/respawn could hide in this gap
    Simulation unknown;f=unknown.frame(0,1);auto d=unknown.issue(0,1);
    Tape bad;bad.complete=true;bad.word_token=d.intent.word_token;unknown.add(bad,static_cast<EffectKind>(999));
    CHECK(!unknown.controller.confirm(d.intent,f,bad));CHECK(unknown.controller.phase()==Phase::AbortCleanup);
    Simulation release;for(int r=1;r<=11;++r)day(release,r);
    release.begin_round(12);d=release.issue(0,12);release.execute(0,12,d);
    release.body[2].position=release.c.members[2].capture; // a previously released actor re-enters
    d=release.issue(1,12);release.execute(1,12,d);CHECK(release.controller.phase()==Phase::Release);
    d=release.issue(2,12);release.execute(2,12,d);CHECK(release.controller.phase()==Phase::Convert);
    Simulation renewal;for(int r=1;r<=23;++r)day(renewal,r);
    CHECK(renewal.controller.phase()==Phase::Renew);
    CHECK(renewal.controller.result().members[0].recovered&&renewal.controller.result().members[1].recovered&&renewal.controller.result().members[2].recovered);
    f=renewal.frame(0,24);f.commitment_matches[2]=false;renewal.controller.tick(f);
    CHECK(renewal.controller.phase()==Phase::AbortCleanup&&!renewal.controller.result().cleanup_complete);
    CHECK(!renewal.controller.result().complete_observed);
}
void immediate_postword_death() {
    Simulation s;reach_work(s);s.begin_round(6);auto f=s.frame(0,6);auto d=s.controller.tick(f);
    f.guard=s.guard(f,d.intent,Cast::Reset);d=s.controller.tick(f);CHECK(d.status==Status::Ready);
    auto after=f;after.self.alive=false;after.self.hp=0;after.self.position={-1,-1};after.peers[0].body=after.self;
    after.group.camps[0].hp=40;
    Tape t;t.complete=true;t.word_token=d.intent.word_token;s.add(t,EffectKind::BasicDamage,101,100,70);
    s.add(t,EffectKind::ResetCast);s.add(t,EffectKind::BasicDamage,101,70,40);
    s.controller.confirm(d.intent,after,t);
    CHECK(s.controller.phase()==Phase::AbortCleanup&&s.controller.result().failure==Failure::Death);
    CHECK(s.controller.result().residual_hp[0]==40&&!s.controller.result().complete_observed);
}
namespace wire=wb::capital::party_wire;
Contract distributed_contract() {
    auto c=shared_melee_contract();c.id=21;++c.start_round;
    ++c.join_by;++c.arrive_by;++c.work_by;++c.capture_by;++c.release_by;++c.convert_by;++c.recover_by;++c.renew_by;
    for(unsigned i=0;i<c.slot_count;++i)++c.slots[i].round;
    return c;
}
wire::Frame wire_frame(const Frame& f){return {f.owner,f.round,f.input,f.fresh};}
wire::Snapshot snapshot(const std::array<std::uint16_t,4>& memory,const wire::Frame& f){return {f,memory,true,true};}
Milestone milestone(wire::Phase p) {
    switch(p) {
    case wire::Phase::Unpublished:return Milestone::Unpublished;case wire::Phase::Committed:return Milestone::Committed;
    case wire::Phase::Arrived:return Milestone::Arrived;case wire::Phase::Entered:return Milestone::Entered;
    case wire::Phase::WorkPrefix:return Milestone::WorkPrefix;case wire::Phase::Payout:return Milestone::Payout;
    case wire::Phase::Released:return Milestone::Released;case wire::Phase::Banked:return Milestone::Banked;
    case wire::Phase::Recovered:return Milestone::Recovered;case wire::Phase::Renewed:return Milestone::Renewed;
    case wire::Phase::Abort:return Milestone::Abort;case wire::Phase::CleanupDone:return Milestone::CleanupDone;
    case wire::Phase::ClosedClean:return Milestone::ClosedClean;case wire::Phase::ClosedFailed:return Milestone::ClosedFailed;
    }return Milestone::Unpublished;
}
wire::Phase wire_phase(Milestone p) {
    for(unsigned i=0;i<=static_cast<unsigned>(wire::Phase::ClosedFailed);++i) {
        const auto phase=static_cast<wire::Phase>(i);if(milestone(phase)==p)return phase;
    }return wire::Phase::Unpublished;
}
TrustedPredicateReceipt received(const Contract& c,const wire::View& view,unsigned role,const Frame& f) {
    const auto& claim=view.claims[role];TrustedPredicateReceipt r;
    r.contract=c.id;r.actor=claim.actor;r.role=static_cast<Role>(role);r.source_round=claim.payload.round;
    r.milestone=milestone(claim.payload.phase);r.predicate=claim.payload.predicate;
    r.read_owner=f.owner;r.read_round=f.round;r.read_input=f.input;
    r.header_start=view.header.start;r.header_group=view.header.group;
    r.active_armed=view.armed&&view.status==wire::Status::Active;r.whole_read=true;r.trusted_partition_and_producer=true;
    return r;
}
void independent_controllers() {
    const auto c=distributed_contract();Simulation s(c);
    std::array<Controller,actors> controllers{Controller(c,0),Controller(c,1),Controller(c,2)};
    const wire::Roles roles{{0,1,2},true,true,true};
    std::array<wire::Endpoint,actors> endpoints{wire::Endpoint(roles,0),wire::Endpoint(roles,1),wire::Endpoint(roles,2)};
    std::array<std::uint16_t,4> memory{};
    const wire::Frame proposing{0,0,900,true};
    auto proposal=endpoints[0].prepare_proposal(snapshot(memory,proposing),proposing,static_cast<unsigned>(c.group),{true,true});
    CHECK(proposal.prepared);memory[0]=proposal.word;
    CHECK(endpoints[0].confirm_proposal(proposal,snapshot(memory,proposing),proposing).status==wire::Status::ArmedFuture);
    for(unsigned i=1;i<actors;++i){const wire::Frame f{static_cast<int>(i),0,900+i,true};CHECK(endpoints[i].observe(snapshot(memory,f),f).armed);}
    Controller missed(c,0),late_arrival(c,0);bool saved_missed=false,recovered_prefix=false,saved_late=false,late_rejected=false;
    for(int r=c.start_round;r<=c.renew_by;++r) {
        s.begin_round(r);
        for(unsigned i=0;i<actors;++i) {
            const int script=r-c.start_round+1;
            auto& controller=controllers[i];auto f=s.frame(i,r);const auto wf=wire_frame(f);
            auto view=endpoints[i].observe(snapshot(memory,wf),wf);CHECK(view.status==wire::Status::Active);
            for(unsigned j=0;j<actors;++j)if(j!=i&&view.claims[j].status==wire::ClaimStatus::DatedPayload) {
                const auto report=received(c,view,j,f);const auto status=controller.ingest(c,report,f);
                CHECK(status==IngestStatus::Accepted||status==IngestStatus::Duplicate);
                if(status!=IngestStatus::Accepted&&status!=IngestStatus::Duplicate){std::printf("ingest r%d i%u j%u status%u milestone%u source%d phase%u failure%u\n",r,i,j,static_cast<unsigned>(status),static_cast<unsigned>(report.milestone),report.source_round,static_cast<unsigned>(controller.phase()),static_cast<unsigned>(controller.result().failure));return;}
                if(saved_late&&!late_rejected&&i==0&&script==6) {
                    const auto status_late=late_arrival.ingest(c,report,f);
                    CHECK(status_late==IngestStatus::Accepted||status_late==IngestStatus::Duplicate);
                }
                if(saved_missed&&!recovered_prefix&&i==0&&j==1&&report.milestone==Milestone::Payout) {
                    CHECK(!missed.progress().slots[1]);
                    CHECK(missed.ingest(c,report,f)==IngestStatus::Accepted);
                    CHECK(missed.progress().slots[1]&&missed.progress().members[1].useful_support);
                    CHECK(missed.result().members[1].useful_heal==0&&!missed.result().quantity_history_known[1]);
                    CHECK(!missed.result().complete_observed);recovered_prefix=true;
                }
            }
            if(saved_late&&!late_rejected&&i==0&&script==6) {
                // An actual arrival first observed AFTER the local deadline
                // cannot borrow the on-time peer-report allowance.
                CHECK(late_arrival.tick(f).status==Status::Aborting);
                CHECK(late_arrival.result().failure==Failure::Deadline&&!late_arrival.result().members[0].entered);
                late_rejected=true;
            }
            auto d=controller.tick(f);Cast cast=Cast::None;
            if(d.intent.kind==Kind::WorkTank)cast=script==6?Cast::Shield:Cast::Reset;
            if(d.intent.kind==Kind::SupportHeal)cast=Cast::Heal;
            if(d.status==Status::NeedModel&&d.intent.kind!=Kind::None){f.guard=s.guard(f,d.intent,cast);d=controller.tick(f);}
            CHECK(d.status!=Status::Aborting&&d.status!=Status::Closed&&d.status!=Status::Invalid);
            if(d.status==Status::Aborting||d.status==Status::Closed||d.status==Status::Invalid){std::printf("decision r%d i%u status%u phase%u failure%u\n",r,i,static_cast<unsigned>(d.status),static_cast<unsigned>(controller.phase()),static_cast<unsigned>(controller.result().failure));return;}
            if(i==0&&script==5&&d.status==Status::Ready) {
                late_arrival=controller;Tape refused;refused.word_token=d.intent.word_token;refused.refused=true;
                CHECK(late_arrival.confirm(d.intent,f,refused));saved_late=true;
            }
            // Commit writes are the declaration being confirmed, not a
            // fabricated preexisting local commitment effect.
            auto publish=[&](const ProducerReceipt& p,const Frame& actual) {
                if(!p.valid)return;
                CHECK(p.source_input==actual.input&&p.source_round==actual.round&&p.actor==actual.owner);
                const auto source=wire_frame(actual);
                auto write=endpoints[i].prepare_record(snapshot(memory,source),source,wire_phase(p.milestone),p.predicate);
                CHECK(write.prepared);if(!write.prepared)return;
                memory[write.byte_offset/2]=write.word;
                CHECK(endpoints[i].confirm_record(write,snapshot(memory,source),source)==wire::Status::Published);
            };
            if(d.status==Status::Ready&&d.intent.kind==Kind::Commit)publish(controller.publication(f),f);
            s.execute(i,r,d,&controller);
            f=s.frame(i,r);
            if(d.intent.kind!=Kind::Commit)publish(controller.publication(f),f);
            // Ingestion-only branch: this snapshot misses Willow's later
            // WorkPrefix. It is NOT allowed to execute through the missing
            // support or a gap in personal inputs; only test later history
            // recovery from the actual cumulative Payout publication.
            if(i==0&&script==6){missed=controller;saved_missed=true;CHECK(!missed.progress().slots[1]);}
        }
        s.end_round();
        bool done=true;for(const auto& controller:controllers)done&=controller.progress().attested_complete;
        if(done)break;
    }
    CHECK(recovered_prefix&&late_rejected);
    for(unsigned i=0;i<actors;++i) {
        const auto& result=controllers[i].result();CHECK(controllers[i].progress().attested_complete&&!result.complete_observed);
        CHECK(controllers[i].phase()==Phase::Complete&&result.quantity_history_known[i]);
        for(unsigned j=0;j<actors;++j)if(i!=j) {
            const auto& remote=result.members[j];CHECK(!result.quantity_history_known[j]);
            CHECK(remote.camp_work==0&&remote.useful_heal==0&&remote.actual_shield==0&&remote.captured_gems==0&&remote.banked_party_gems==0);
            CHECK(!remote.committed&&!remote.receipt&&!remote.renewed);
        }
    }
    CHECK(controllers[0].result().members[0].camp_work==60&&controllers[1].result().members[1].useful_heal==25
        &&controllers[2].result().members[2].camp_work==40);
    auto f=s.frame(0,c.renew_by);TrustedPredicateReceipt withdraw;
    withdraw.contract=c.id;withdraw.actor=1;withdraw.role=Role::Willow;withdraw.source_round=c.start_round;
    withdraw.milestone=Milestone::Abort;withdraw.read_owner=0;withdraw.read_round=f.round;withdraw.read_input=f.input;
    withdraw.header_start=c.start_round;withdraw.header_group=c.group;
    withdraw.active_armed=withdraw.whole_read=withdraw.trusted_partition_and_producer=true;
    CHECK(controllers[0].ingest(c,withdraw,f)==IngestStatus::Cancelled); // older abort still wins
    CHECK(controllers[0].tick(f).status==Status::Closed&&!controllers[0].progress().attested_complete);
    CHECK(controllers[0].result().members[0].camp_work==60);
}
TrustedPredicateReceipt predicate(const Contract& c,const Frame& f,unsigned role,Milestone m,int source_round) {
    TrustedPredicateReceipt r;r.contract=c.id;r.actor=c.members[role].id;r.role=static_cast<Role>(role);
    r.source_round=source_round;r.milestone=m;r.predicate=true;r.read_owner=f.owner;r.read_round=f.round;r.read_input=f.input;
    r.header_start=c.start_round;r.header_group=c.group;r.active_armed=r.whole_read=r.trusted_partition_and_producer=true;return r;
}
void receipt_rejections_and_binding() {
    auto c=distributed_contract();Simulation s(c);auto f=s.frame(0,c.start_round);
    Controller base(c,0);auto committed=predicate(c,f,1,Milestone::Committed,c.start_round);
    CHECK(base.ingest(c,committed,f)==IngestStatus::Accepted);
    CHECK(base.ingest(c,committed,f)==IngestStatus::Duplicate);
    auto changed=committed;changed.source_round=c.start_round-1;CHECK(base.ingest(c,changed,f)==IngestStatus::Stale);
    changed=committed;changed.source_round=f.round+1;CHECK(base.ingest(c,changed,f)==IngestStatus::Stale);
    changed=committed;changed.role=Role::Vendetta;CHECK(base.ingest(c,changed,f)==IngestStatus::Invalid);
    changed=committed;++changed.read_input;CHECK(base.ingest(c,changed,f)==IngestStatus::Invalid);
    changed=committed;changed.trusted_partition_and_producer=false;CHECK(base.ingest(c,changed,f)==IngestStatus::Unverified);
    changed=committed;changed.predicate=false;CHECK(base.ingest(c,changed,f)==IngestStatus::Unverified);
    changed=committed;changed.actor=0;changed.role=Role::Varg;CHECK(base.ingest(c,changed,f)==IngestStatus::Invalid);
    auto conflicting=c;++conflicting.members[1].release.x;auto copy=base;
    CHECK(copy.ingest(conflicting,committed,f)==IngestStatus::Conflict&&copy.progress().cancelled);
    conflicting=c;++conflicting.slots[1].round;copy=base;CHECK(copy.ingest(conflicting,committed,f)==IngestStatus::Conflict);
    conflicting=c;++conflicting.reward_gems;copy=base;CHECK(copy.ingest(conflicting,committed,f)==IngestStatus::Conflict);
    changed=committed;changed.milestone=Milestone::Payout;copy=base;
    CHECK(copy.ingest(c,changed,f)==IngestStatus::Conflict); // before any mandatory work/capture can have completed
    changed=committed;changed.milestone=Milestone::Arrived;copy=base;CHECK(copy.ingest(c,changed,f)==IngestStatus::Accepted);
    CHECK(copy.ingest(c,committed,f)==IngestStatus::Conflict); // same-date regression
    Controller vacuous(c,0);changed=committed;changed.milestone=Milestone::WorkPrefix;
    CHECK(vacuous.ingest(c,changed,f)==IngestStatus::Accepted);
    CHECK(!vacuous.progress().slots[1]&&!vacuous.progress().members[1].useful_support);
    auto at_work=s.frame(2,c.slots[0].round);Controller prefix(c,2);
    auto partial=predicate(c,at_work,0,Milestone::WorkPrefix,c.slots[0].round);
    CHECK(prefix.ingest(c,partial,at_work)==IngestStatus::Accepted);
    CHECK(prefix.progress().slots[0]&&!prefix.progress().slots[3]&&prefix.progress().members[0].positive_work);
    CHECK(prefix.result().members[0].camp_work==0&&!prefix.result().quantity_history_known[0]);
    std::swap(c.slots[0],c.slots[2]);CHECK(detail::valid(c));Controller unsorted(c,2);
    partial=predicate(c,at_work,0,Milestone::WorkPrefix,c.slots[2].round);
    CHECK(unsorted.ingest(c,partial,at_work)==IngestStatus::Accepted);
    CHECK(unsorted.progress().slots[2]&&!unsorted.progress().slots[3]);
    c=distributed_contract();Controller local(c,0);auto d=local.tick(f);CHECK(d.status==Status::NeedModel);
    f.guard=s.guard(f,d.intent);d=local.tick(f);CHECK(d.status==Status::Ready);
    CHECK(local.publication(f).valid);
    copy=local;auto abort=committed;abort.milestone=Milestone::Abort;abort.predicate=false;
    CHECK(copy.ingest(c,abort,f)==IngestStatus::Cancelled);
    CHECK(copy.tick(f).status==Status::Aborting);
    s.execute(0,f.round,d,&copy);CHECK(copy.result().members[0].committed&&copy.phase()==Phase::AbortCleanup);
    CHECK(copy.progress().cancelled&&!copy.result().complete_observed);
    CHECK(local.ingest(c,committed,f)==IngestStatus::PendingLocalWord);
    CHECK(!local.result().members[0].committed);
    s.execute(0,f.round,d,&local);auto actual=s.frame(0,f.round);CHECK(local.publication(actual).valid);
    auto unprocessed=actual;++unprocessed.round;++unprocessed.input;CHECK(!local.publication(unprocessed).valid);
    unprocessed=actual;unprocessed.self.hp=50;CHECK(!local.publication(unprocessed).valid);
    unprocessed=actual;unprocessed.self.alive=false;unprocessed.self.hp=0;CHECK(!local.publication(unprocessed).valid);
    Controller outsider(c,99);unprocessed.owner=unprocessed.self.id=99;
    CHECK(!outsider.publication(unprocessed).valid&&outsider.ingest(c,committed,unprocessed)==IngestStatus::Invalid);
    Simulation premature;reach_work(premature);premature.hp={0,0,0};auto clear=premature.frame(0,6);
    CHECK(premature.controller.tick(clear).status==Status::Aborting);
    CHECK(premature.controller.result().failure==Failure::MissedWorkSlot&&premature.controller.result().members[0].captured_gems==0);
}
}
int main(){whole_pipeline();contract_and_model_separation();rejection_and_missing_authority();individual_and_cast_guards();support_and_arrival_failure();
    effect_and_receipt_authentication();bank_failure_refusal_and_partial_death();
    gap_enum_and_current_closure();immediate_postword_death();shared_melee_ownership();split_monster_ownership();independent_controllers();receipt_rejections_and_binding();
    std::printf("capital_party: %u checks, %u failures\n",checks,failures);return failures?1:0;}
