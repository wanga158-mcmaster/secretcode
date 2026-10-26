#define WB_PARTY_PROGRAM_NATIVE_HOST_FREE
#include "capital_party_program_native.h"
#include <cstdio>
#include <stdexcept>
namespace n=wb::capital::party_program_native;namespace q=wb::capital::party_program;namespace p=wb::capital::party;namespace w=wb::capital::party_wire;
unsigned checks{},failures{};
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %d: %s\n",__LINE__,#x);}}while(false)
q::Contract canonical(){q::Contract c;c.id=9;c.life=3;c.group=2;c.start=10;c.commit_by=20;c.arrive_by=30;c.work_by=60;c.capture_by=70;c.bank_by=90;c.renew_by=110;c.abort_by=120;
    c.camps={41,42};c.camp_count=2;c.capture_mask=7;c.max_refusals=2;c.max_no_progress=5;c.domain={1,2,3,4,5,6,10,110,q::Risk::ConditionalModel};
    for(unsigned i=0;i<3;++i){auto& m=c.members[i];m.id=static_cast<int>(4+i);m.role=static_cast<p::Role>(i);m.rendezvous={2+static_cast<int>(i),2};m.station={6+static_cast<int>(i),6};m.capture={6+static_cast<int>(i),7};m.release={2+static_cast<int>(i),5};m.altar={1,1};m.spawn={0,0};m.renewal={1+static_cast<int>(i),3};}return c;}
n::Admission admission(){return {{true,true,true,true,true,true,{true,true,true}},true,true};}
struct Host {
    q::Contract c=canonical();q::Frame world{};std::array<std::uint16_t,4> words{};int who{4},when{8};q::Revision input{80},sample{},events{100},foes{1};
    unsigned raw{},writes{},guards{},queries{},fuel_value{100000},throw_on{},false_on{};q::Revision locks{1};bool false_next{},reveal_next{},lock_next{},throw_next{},throw_wire{},stale_guard{},deny_guard{},bad_effect{},independent_ok{true},epoch_after_heal{},jump_on_fuel{};
    Host(){world.fresh=world.priority_contract_current=world.capital_obligation_clear=world.independent_interpreter_ready=world.group_life_known=true;
        world.self.id=who;world.self.role=p::Role::Varg;world.self.position=c.members[0].rendezvous;world.self.hp=world.self.max_hp=1000;world.self.alive=world.self.move=world.self.attack=world.self.cast=true;
        world.self.npc_quiet=true;world.self.shield_ready=world.self.reset_ready=world.self.heal_ready=true;world.self.heal_range=4;
        for(unsigned i=0;i<3;++i){world.peers[i]={world.self,true};world.peers[i].body.id=c.members[i].id;world.peers[i].body.role=static_cast<p::Role>(i);world.peers[i].body.position=c.members[i].station;}
        world.group.id=c.group;world.group.count=2;world.group.observed_now=true;world.group.camps[0]={41,60,true,true,{7,7},3,true};world.group.camps[1]={42,100,true,true,{8,7},3,true};}
    int owner(){++queries;return who;}int round(){++queries;return when;}unsigned fuel(){++queries;if(jump_on_fuel){jump_on_fuel=false;next(when+1);}return fuel_value;}
    q::Revision event(){return ++events;}
    q::Epoch epoch()const{return {who,when,input,0,1,1,foes,locks,1,c.life};}
    void next(int r){when=r;input=static_cast<q::Revision>(r*10);sample=0;fuel_value=100000;world.self.move=world.self.attack=world.self.cast=true;}
    bool observe(q::Epoch e,const q::Contract&,q::Frame& out){fuel_value-=10000;out=world;out.source=e;out.source.sample=++sample;out.source.foes=foes;out.source.locks=locks;out.self.capital_revision=1;out.peers[static_cast<unsigned>(who-4)]={out.self,true};return true;}
    void read_words(std::array<std::uint16_t,4>& out){out=words;}
    bool write_word(unsigned offset,std::uint16_t word,n::Fence fence){if(!fence())return false;const auto fenced=queries;words[offset/2]=word;++writes;CHECK(queries==fenced);if(throw_wire){throw_wire=false;throw 8;}return true;}
    bool guard(const q::Offer&,const n::Word& word,unsigned step,const q::Frame& f,n::Guard& out){fuel_value-=1000;++guards;out={f.source,word.token,step,true,true,true,true};if(stale_guard)--out.source.sample;
        const auto& command=word.steps[step];if(command.kind==n::PrimitiveKind::Basic){const auto* target=n::camp(f,command.target);out.legal=f.self.attack&&target&&target->observed_now&&target->alive&&p::distance(f.self.position,target->position)<=3;}
        else if(command.kind==n::PrimitiveKind::Heal)out.legal=f.self.cast&&command.target==4&&f.peers[0].observed_now&&f.peers[0].body.alive&&f.peers[0].body.hp<f.peers[0].body.max_hp;
        else if(command.kind==n::PrimitiveKind::Move)out.legal=f.self.move;
        else if(command.kind==n::PrimitiveKind::Reset||command.kind==n::PrimitiveKind::Shield)out.legal=f.self.cast;
        else if(command.kind==n::PrimitiveKind::Submit)out.legal=f.self.bag&&f.self.altar_adjacent;
        return !deny_guard;}
    bool execute(const n::Primitive& command,n::Fence fence,bool& accepted){if(!fence())return false;const auto fenced=queries;++raw;
        fuel_value-=1000;if(throw_next||throw_on==raw){throw_next=false;throw 7;}accepted=!false_next&&false_on!=raw;false_next=false;
        if(accepted)switch(command.kind){case n::PrimitiveKind::Basic:for(auto& x:world.group.camps)if(x.id==command.target){x.hp=x.hp>20?x.hp-20:0;x.alive=x.hp>0;}world.self.attack=false;break;
        case n::PrimitiveKind::Reset:world.self.attack=true;world.self.cast=false;break;
        case n::PrimitiveKind::Move:world.self.position=command.destination;world.self.move=false;break;
        case n::PrimitiveKind::Shield:world.self.shield+=50;world.self.cast=false;break;
        case n::PrimitiveKind::Heal:world.peers[0].body.hp+=20;world.self.cast=false;break;
        case n::PrimitiveKind::Submit:world.self.channel=p::Channel::Submit;world.self.channel_remaining=2;break;
        case n::PrimitiveKind::Recall:world.self.channel=p::Channel::Recall;world.self.channel_remaining=5;break;
        case n::PrimitiveKind::Dash:world.self.position=command.destination;world.self.cast=false;break;}
        if(reveal_next){++foes;reveal_next=false;}if(lock_next){++locks;lock_next=false;}world.group.whole_clear=!world.group.camps[0].alive&&!world.group.camps[1].alive;
        CHECK(queries==fenced);return true;}
    bool effects(const q::Frame& before,const q::Frame& after,const n::Primitive& command,bool accepted,const q::Lease& lease,std::span<q::Actual> out,unsigned& count){
        count=0;if(!accepted)return true;if(bad_effect)return false;
        auto add=[&](q::ReceiptKind kind,int target,float a,float b){auto& x=out[count++];x.kind=kind;x.event=event();x.contract=c.id;x.life=c.life;x.actor=who;x.group=c.group;x.target=target;x.word=lease.word;x.before=a;x.after=b;x.observed_actual=x.accepted=true;};
        if(command.kind==n::PrimitiveKind::Basic){for(const auto& a:before.group.camps){const auto* b=n::camp(after,a.id);if(b&&a.hp>b->hp)add(q::ReceiptKind::CampDamage,a.id,a.hp,b->hp);}}
        else if(command.kind==n::PrimitiveKind::Shield&&after.self.shield>before.self.shield)add(q::ReceiptKind::Shield,who,before.self.shield,after.self.shield);
        else if(command.kind==n::PrimitiveKind::Heal){const auto* a=n::peer(before,command.target);const auto* b=n::peer(after,command.target);if(a&&b&&b->hp>a->hp)add(q::ReceiptKind::Heal,command.target,a->hp,b->hp);if(epoch_after_heal)jump_on_fuel=true;}
        return true;}
    bool independent(const n::Independent&,const q::Frame&){return independent_ok;}
    void remote(w::Phase phase){for(unsigned i=0;i<3;++i)if(c.members[i].id!=who)words[i+1]=w::encode_record(when,phase,true).word;}
};
struct Driver {
    Host host{};n::Session<Host> session;
    explicit Driver(unsigned role=0,n::Options options={}):session(host,host.c,static_cast<int>(4+role),admission(),options) {host.who=static_cast<int>(4+role);host.world.self.id=host.who;host.world.self.role=static_cast<p::Role>(role);host.world.self.position=host.c.members[role].rendezvous;
        CHECK(session.begin(host.epoch())==n::Status::Ready);
        if(role==0){auto proposal=session.propose(true);CHECK(proposal.status==n::Status::Complete&&proposal.accepted);}else{host.words[0]=w::encode_header(10,2).word;CHECK(session.arm()==n::Status::Prestart);}CHECK(session.wire_owned());
        host.next(10);CHECK(session.begin(host.epoch())==n::Status::Ready);CHECK(session.observe()==n::Status::Ready);CHECK(session.nominate().offer.kind==q::Kind::PublishCommit);
        const auto o=session.nominate().offer;auto word=make(o);const auto commit=session.execute(o,quote(o,word),word);CHECK(commit.status==n::Status::Complete&&commit.accepted);CHECK(!session.needs_reconciliation());}
    n::Word make(const q::Offer& o){n::Word word;word.source=o.source;word.contract=o.contract;word.token=300+o.serial;word.price.model=17;word.price.domain=host.c.domain.id;word.price.guard.fill(1000);word.price.command.fill(1000);word.price.readback.fill(10000);word.price.exit_tail=25000;word.price.bounded=true;return word;}
    q::Quote quote(const q::Offer& o,const n::Word& word){return {o.source,o.contract,o.serial,word.token,host.c.domain,true,true,true,true,true,true,true,0,word.camp_target};}
    void observe(){CHECK(session.observe()==n::Status::Ready);}
    void work(){host.remote(w::Phase::Arrived);observe();CHECK(session.program().phase()==q::Phase::Enter);host.next(host.when+1);CHECK(session.begin(host.epoch())==n::Status::Ready);host.world.self.position=host.c.members[static_cast<unsigned>(host.who-4)].station;host.remote(w::Phase::Entered);observe();CHECK(session.program().phase()==q::Phase::Work);}
    n::Result basic_reset_basic(){const auto o=session.nominate().offer;CHECK(o.kind==q::Kind::WorkTank);auto word=make(o);word.selected_cast=p::Cast::Reset;word.count=3;word.steps[0]={n::PrimitiveKind::Basic,{},o.target,-1};word.steps[1]={n::PrimitiveKind::Reset,{},-1,0};word.steps[2]=word.steps[0];return session.execute(o,quote(o,word),word);}
};
struct Healing {
    Driver d{1};q::Offer offer{};n::Word word{};q::Quote quote{};
    Healing(){d.work();d.host.world.peers[0].body.hp=500;d.observe();prepare();}
    void prepare(int target=41){offer=d.session.nominate().offer;CHECK(offer.kind==q::Kind::Heal&&offer.target==4);word=d.make(offer);word.selected_cast=p::Cast::Heal;word.camp_target=target;word.count=2;
        word.steps[0]={n::PrimitiveKind::Heal,{},offer.target,0};word.steps[1]={n::PrimitiveKind::Basic,{},target,-1};quote=d.quote(offer,word);}
    n::Result run(){return d.session.execute(offer,quote,word);}
};
void heal_basic(){Healing h;h.d.host.fuel_value=52000;const auto result=h.run();CHECK(result.status==n::Status::Complete&&result.accepted_steps==2&&result.actual_receipts==2&&h.d.host.raw==2);
    CHECK(!h.d.host.world.self.cast&&!h.d.host.world.self.attack);const auto q=h.d.session.program().completion();CHECK(q.quantities[1].useful_heal==20&&q.quantities[1].work==20);CHECK(h.d.session.retained_prefix().count==2);
    const auto& t=h.d.session.telemetry();CHECK(t.entry_fuel==52000&&t.required==49000&&t.precommand_fuel==39000&&t.postreadback_fuel==28000);CHECK(t.guard_overruns==0&&t.command_overruns==0&&t.readback_overruns==0&&t.stage==n::Stage::Done);
    Healing low;low.d.host.fuel_value=48000;const auto denied=low.run();CHECK(denied.status==n::Status::NeedFuel&&!denied.claimed&&low.d.host.raw==0&&low.d.host.world.self.cast&&low.d.host.world.self.attack);
    CHECK(low.d.session.telemetry().required==49000&&!low.d.session.needs_reconciliation());
    Healing no_price;no_price.word.price.bounded=false;CHECK(no_price.run().status==n::Status::NeedModel);CHECK(no_price.d.host.raw==0&&no_price.d.session.telemetry().unpriced==1);
    Healing underpriced;underpriced.word.price.guard.fill(500);CHECK(underpriced.run().status==n::Status::NeedFuel);CHECK(underpriced.d.host.raw==0&&underpriced.d.session.telemetry().guard_overruns==1);
}
void heal_failures(){
    Healing failed;failed.d.host.false_next=true;const auto refused=failed.run();CHECK(refused.status==n::Status::Refused&&failed.d.host.raw==1&&refused.accepted_steps==0);CHECK(failed.d.session.program().completion().quantities[1].useful_heal==0&&failed.d.session.program().completion().quantities[1].work==0);
    Healing basic;basic.d.host.false_on=2;const auto second=basic.run();CHECK(second.status==n::Status::Refused&&second.accepted_steps==1&&basic.d.host.raw==2);CHECK(basic.d.session.program().completion().quantities[1].useful_heal==20&&basic.d.session.program().completion().quantities[1].work==0);
    for(unsigned changed=0;changed<3;++changed){Healing h;if(changed==0)h.d.host.reveal_next=true;else if(changed==1)h.d.host.lock_next=true;else h.d.host.epoch_after_heal=true;
        const auto ended=h.run();CHECK(ended.status==(changed==2?n::Status::Stale:n::Status::Aborting));CHECK(h.d.host.raw==1&&ended.accepted_steps==1);CHECK(h.d.session.program().completion().quantities[1].useful_heal==20&&h.d.session.program().completion().quantities[1].work==0);CHECK(!h.d.session.needs_reconciliation());}
    Healing absent;absent.word.camp_target=-1;absent.quote.camp_target=-1;CHECK(absent.run().status==n::Status::Stale);CHECK(absent.d.host.raw==0);
    Healing outside;outside.prepare(999);CHECK(outside.run().status==n::Status::NeedModel);CHECK(outside.d.host.raw==0);
    Healing dead;dead.d.host.world.group.camps[0].alive=false;dead.d.host.world.group.camps[0].hp=0;dead.d.observe();dead.prepare();CHECK(dead.run().status==n::Status::NeedModel);CHECK(dead.d.host.raw==0);
    Healing missing;missing.d.host.world.group.camps[0].observed_now=false;missing.d.observe();CHECK(missing.d.session.nominate().status==q::Status::NeedObservation);CHECK(missing.d.host.raw==0);
    Healing no_attack;no_attack.d.host.world.self.attack=false;no_attack.d.observe();no_attack.prepare();CHECK(no_attack.run().status==n::Status::NeedModel);CHECK(no_attack.d.host.raw==0&&no_attack.d.host.world.self.cast);
    Healing borrowed;borrowed.quote.required_support_event=999;CHECK(borrowed.run().status==n::Status::NeedModel);CHECK(borrowed.d.host.raw==0);
}
void words(){Driver d;d.work();const auto done=d.basic_reset_basic();CHECK(done.status==n::Status::Complete&&done.accepted_steps==3&&done.actual_receipts==2);CHECK(d.host.raw==3);
    CHECK(d.session.program().completion().quantities[0].work==40);CHECK(d.session.retained_prefix().count==2);CHECK(!d.session.needs_reconciliation());
    // An actual kill invalidates the old-target suffix while preserving the
    // owner's remaining input and enabling a fresh target nomination.
    d.host.next(d.host.when+1);CHECK(d.session.begin(d.host.epoch())==n::Status::Ready);d.observe();const auto next=d.basic_reset_basic();CHECK(next.status==n::Status::Replan&&next.accepted_steps==1);CHECK(d.host.raw==4);CHECK(d.session.nominate().offer.target==42);
    CHECK(d.session.program().completion().quantities[0].work==60);CHECK(d.session.program().phase()==q::Phase::Work);}
void stale_and_refused(){Driver d;d.work();d.host.stale_guard=true;const auto stale=d.basic_reset_basic();CHECK(stale.status==n::Status::Replan&&!stale.attempted&&d.host.raw==0);CHECK(!d.session.needs_reconciliation());
    d.host.stale_guard=false;const auto o=d.session.nominate().offer;auto word=d.make(o);word.count=2;word.steps[0]={n::PrimitiveKind::Move,{6,5},-1,-1};word.steps[1]={n::PrimitiveKind::Basic,{},o.target,-1};d.host.false_next=true;
    const auto refused=d.session.execute(o,d.quote(o,word),word);CHECK(refused.status==n::Status::Refused&&refused.attempted&&refused.accepted_steps==0&&d.host.raw==1);CHECK(!d.session.needs_reconciliation());
    CHECK(d.session.program().completion().quantities[0].work==0);CHECK(d.session.program().phase()==q::Phase::Work);}
void funded_readback(){Driver d;d.work();d.host.fuel_value=59000;const auto blocked=d.basic_reset_basic();CHECK(blocked.status==n::Status::NeedFuel&&!blocked.claimed&&!blocked.attempted&&d.host.raw==0);CHECK(!d.session.needs_reconciliation());CHECK(d.session.program().completion().quantities[0].work==0);CHECK(d.session.telemetry().entry_fuel==59000&&d.session.telemetry().required==61000);}
void revelation(){Driver d;d.work();d.host.reveal_next=true;const auto r=d.basic_reset_basic();CHECK(r.status==n::Status::Aborting&&r.accepted_steps==1&&d.host.raw==1);
    CHECK(d.session.program().completion().quantities[0].work==20);CHECK(d.session.program().failure()==q::Failure::ChangedProtocol);CHECK(!d.session.needs_reconciliation());
    d.host.world.source_released=true;d.observe();n::Independent grant{d.session.actual().source,21,22,true,true,true};d.host.independent_ok=false;CHECK(!d.session.handoff(grant));d.host.independent_ok=true;
    auto stale=grant;--stale.source.sample;CHECK(!d.session.handoff(stale));CHECK(d.session.handoff(grant));CHECK(d.session.wire_owned());
    const auto cleanup=d.session.cleanup();CHECK(cleanup.status==n::Status::Complete);CHECK(d.session.wire_owned());d.host.remote(w::Phase::CleanupDone);d.observe();CHECK(!d.session.wire_owned());}
void interrupted(){Driver d;d.work();const auto o=d.session.nominate().offer;auto word=d.make(o);word.count=1;word.steps[0]={n::PrimitiveKind::Basic,{},o.target,-1};d.host.throw_next=true;bool threw=false;
    try{(void)d.session.execute(o,d.quote(o,word),word);}catch(int){threw=true;}CHECK(threw&&d.session.needs_reconciliation());CHECK(d.session.retained_prefix().count==0);
    d.host.next(d.host.when+1);CHECK(d.session.begin(d.host.epoch())==n::Status::Pending);const auto recovered=d.session.reconcile_pending();CHECK(recovered.status==n::Status::Complete&&recovered.unknown_effect&&recovered.physical_released);
    CHECK(d.host.raw==1);CHECK(d.session.program().completion().quantities[0].work==0);CHECK(!d.session.program().completion().quantity_history_known[0]);CHECK(d.session.program().failure()==q::Failure::UnknownEffect);}
void known_prefix_unknown_tail(){Driver d;d.work();d.host.throw_on=2;bool threw=false;try{(void)d.basic_reset_basic();}catch(int){threw=true;}
    CHECK(threw&&d.host.raw==2&&d.session.needs_reconciliation());CHECK(d.session.retained_prefix().count==1);CHECK(d.session.program().completion().quantities[0].work==20);
    d.host.next(d.host.when+1);CHECK(d.session.begin(d.host.epoch())==n::Status::Pending);const auto result=d.session.reconcile_pending();CHECK(result.status==n::Status::Complete&&result.unknown_effect);
    CHECK(d.host.raw==2&&d.session.program().completion().quantities[0].work==20);CHECK(!d.session.program().completion().quantity_history_known[0]);}
void metadata_readback(){Host h;n::Session<Host> session(h,h.c,4,admission());CHECK(session.begin(h.epoch())==n::Status::Ready);CHECK(session.propose(true).status==n::Status::Complete);
    h.next(10);CHECK(session.begin(h.epoch())==n::Status::Ready);CHECK(session.observe()==n::Status::Ready);const auto offer=session.nominate().offer;
    n::Word word;word.source=offer.source;word.contract=offer.contract;word.token=77;q::Quote quote{offer.source,offer.contract,offer.serial,word.token,h.c.domain,true,true,true,true,true,true,true,0};
    h.throw_wire=true;bool threw=false;try{(void)session.execute(offer,quote,word);}catch(int){threw=true;}CHECK(threw&&session.needs_reconciliation());CHECK(h.writes==2);
    h.next(11);CHECK(session.begin(h.epoch())==n::Status::Pending);const auto readback=session.reconcile_pending();CHECK(readback.status==n::Status::Complete&&!readback.unknown_effect);CHECK(h.writes==2);
    CHECK(session.program().completion().members[0].committed);CHECK(session.program().completion().quantities[0].work==0);}
void protocol_regression(){Driver d;d.work();d.host.remote(w::Phase::Arrived);CHECK(d.session.observe()==n::Status::NeedModel);CHECK(d.session.nominate().status==q::Status::NeedModel);CHECK(d.host.raw==0);
    const auto withdrew=d.session.publish();CHECK(withdrew.status==n::Status::Complete);CHECK(w::decode_record(d.host.words[1]).phase==w::Phase::Abort);CHECK(d.session.observe()==n::Status::Ready);CHECK(d.session.program().phase()==q::Phase::Abort);}
void rejected_packet_authority(){Driver d;d.work();auto changed=d.host.epoch();changed.life=4;CHECK(d.session.begin(changed)==n::Status::Ready);CHECK(d.session.observe()==n::Status::NeedObservation);
    q::Actual reward;reward.kind=q::ReceiptKind::GroupPayout;reward.event=d.host.event();reward.contract=d.host.c.id;reward.life=d.host.c.life;reward.actor=4;reward.group=d.host.c.group;reward.gems=14;
    reward.observed_actual=reward.accepted=reward.source_covered=true;CHECK(!d.session.receipt(reward));CHECK(d.session.nominate().status==q::Status::NoOffer);CHECK(d.session.program().completion().quantities[0].received_gems==0);}
void cumulative_publication(){Driver d;d.work();const auto o=d.session.nominate().offer;auto word=d.make(o);word.count=1;word.steps[0]={n::PrimitiveKind::Basic,{},o.target,-1};CHECK(d.session.execute(o,d.quote(o,word),word).status==n::Status::Complete);
    for(auto& c:d.host.world.group.camps){c.hp=0;c.alive=false;}d.host.world.group.whole_clear=true;d.observe();q::Actual reward;reward.kind=q::ReceiptKind::GroupPayout;reward.event=d.host.event();reward.contract=d.host.c.id;reward.life=d.host.c.life;reward.actor=4;reward.group=d.host.c.group;
    reward.observed_actual=reward.accepted=reward.source_covered=true;CHECK(d.session.receipt(reward));CHECK(d.session.program().completion().members[0].banked);CHECK(!d.session.program().completion().members[0].released);
    CHECK(d.session.publish().status==n::Status::Complete);CHECK(w::decode_record(d.host.words[1]).phase==w::Phase::Payout);
    Host rejected;rejected.c.require_useful_support=true;n::Session<Host> unsupported(rejected,rejected.c,4,admission());CHECK(unsupported.begin(rejected.epoch())==n::Status::NoOffer);CHECK(rejected.writes==0&&rejected.raw==0);}
void delayed_receipt(){Driver d;d.work();for(auto& c:d.host.world.group.camps){c.hp=0;c.alive=false;}d.host.world.group.whole_clear=true;d.observe();CHECK(d.session.program().phase()==q::Phase::Capture);
    d.host.world.self.bag=14;d.observe();auto a=q::Actual{};a.kind=q::ReceiptKind::GroupPayout;a.event=d.host.event();a.contract=d.host.c.id;a.life=d.host.c.life;a.actor=4;a.group=d.host.c.group;a.gems=14;a.gold=30;a.observed_actual=a.accepted=a.source_covered=true;
    CHECK(d.session.receipt(a));CHECK(!d.session.receipt(a));CHECK(d.session.program().completion().quantities[0].received_gems==14);
    d.host.world.self.bag=14;d.host.remote(w::Phase::Payout);d.observe();CHECK(d.session.program().phase()==q::Phase::Release);
    d.host.world.self.position=d.host.c.members[0].release;d.host.world.source_released=true;d.host.remote(w::Phase::Released);d.observe();CHECK(d.session.program().phase()==q::Phase::Bank);
    d.host.world.self.position={1,2};d.host.world.self.altar_adjacent=true;d.observe();const auto o=d.session.nominate().offer;auto word=d.make(o);word.count=1;word.steps[0]={n::PrimitiveKind::Submit,o.destination,-1,-1};const auto start=d.session.execute(o,d.quote(o,word),word);
    CHECK(start.status==n::Status::Complete);CHECK(d.session.program().completion().quantities[0].banked_gems==0);CHECK(d.session.nominate().offer.kind==q::Kind::HoldChannel);
    d.host.next(d.host.when+1);d.host.world.self.channel_remaining=1;CHECK(d.session.begin(d.host.epoch())==n::Status::Ready);d.observe();CHECK(d.session.program().completion().quantities[0].banked_gems==0);
    d.host.next(d.host.when+1);d.host.world.self.channel=p::Channel::None;d.host.world.self.channel_remaining=0;d.host.world.self.bag=0;d.host.world.self.team_score=14;CHECK(d.session.begin(d.host.epoch())==n::Status::Ready);d.observe();
    a.kind=q::ReceiptKind::BankPayout;a.event=d.host.event();a.gems=14;a.bank_source_attribution_known=true;CHECK(d.session.receipt(a));CHECK(d.session.program().completion().quantities[0].banked_gems==14);}
void passive_arming(){Host h;h.who=5;h.world.self.id=5;h.world.self.role=p::Role::Willow;h.when=9;h.input=90;h.words[0]=w::encode_header(10,2).word;
    n::Session<Host> peer(h,h.c,5,admission());CHECK(peer.begin(h.epoch())==n::Status::Ready);CHECK(peer.arm()==n::Status::Prestart);CHECK(h.raw==0&&h.writes==0);h.next(10);CHECK(peer.begin(h.epoch())==n::Status::Ready);CHECK(peer.observe()==n::Status::Ready);
    Host late;late.who=5;late.world.self.id=5;late.world.self.role=p::Role::Willow;late.when=10;late.input=100;late.words[0]=w::encode_header(10,2).word;n::Session<Host> missing(late,late.c,5,admission());CHECK(missing.begin(late.epoch())==n::Status::Ready);CHECK(missing.arm()==n::Status::NoOffer);CHECK(missing.observe()==n::Status::NoOffer);CHECK(late.raw==0);}
void measure_now(){
    auto single=[](Driver& d){const auto offer=d.session.nominate().offer;auto word=d.make(offer);word.count=1;word.steps[0]={n::PrimitiveKind::Basic,{},offer.target,-1};
        word.price={};word.price.model=17;word.price.domain=d.host.c.domain.id;word.price.exit_tail=25000;word.price.readback[0]=10000;word.price.measure_now=true;return std::pair{offer,word};};
    n::Options options;options.allow_measure_now=true;Driver d(0,options);d.work();auto [offer,word]=single(d);const auto result=d.session.execute(offer,d.quote(offer,word),word);
    CHECK(result.status==n::Status::Complete&&result.accepted_steps==1&&result.actual_receipts==1&&d.host.raw==1);
    const auto& measured=d.session.telemetry();CHECK(measured.measured_word.price.measure_now&&!measured.measured_word.price.bounded&&measured.measured_word.count==1);
    CHECK(measured.guards[0].known&&measured.commands[0].known&&measured.readbacks[0].known);
    CHECK(measured.commands[0].cost==1000&&measured.readbacks[0].cost==10000);
    CHECK(measured.guard_overruns==0&&measured.command_overruns==0&&measured.readback_overruns==0);
    Driver disabled;disabled.work();auto [off_offer,off_word]=single(disabled);CHECK(disabled.session.execute(off_offer,disabled.quote(off_offer,off_word),off_word).status==n::Status::NeedModel&&disabled.host.raw==0);
    Driver compound(0,options);compound.work();auto [many_offer,many_word]=single(compound);many_word.count=2;many_word.steps[1]=many_word.steps[0];
    CHECK(compound.session.execute(many_offer,compound.quote(many_offer,many_word),many_word).status==n::Status::NeedModel&&compound.host.raw==0);
    Driver low(0,options);low.work();auto [low_offer,low_word]=single(low);low.host.fuel_value=34999;
    CHECK(low.session.execute(low_offer,low.quote(low_offer,low_word),low_word).status==n::Status::NeedFuel&&low.host.raw==0);
    Driver denied(0,options);denied.work();auto [no_offer,no_word]=single(denied);denied.host.deny_guard=true;
    CHECK(denied.session.execute(no_offer,denied.quote(no_offer,no_word),no_word).status==n::Status::Replan&&denied.host.raw==0);
    CHECK(denied.session.telemetry().guards[0].known&&!denied.session.telemetry().commands[0].known);
    Driver drift(0,options);drift.work();auto [drift_offer,drift_word]=single(drift);drift.host.jump_on_fuel=true;
    CHECK(drift.session.execute(drift_offer,drift.quote(drift_offer,drift_word),drift_word).status==n::Status::Stale&&drift.host.raw==0);
    CHECK(!drift.session.telemetry().commands[0].known&&!drift.session.telemetry().readbacks[0].known);
}
void paid_exit_handoff(){
    Driver disabled;disabled.work();disabled.host.world.priority_contract_current=false;disabled.host.world.self.npc_quiet=false;disabled.observe();
    n::Independent off{disabled.session.actual().source,21,22,true,true,true};CHECK(!disabled.session.handoff_paid_exit(off));CHECK(!disabled.session.handoff(off));
    n::Options options;options.allow_paid_exit_handoff=true;Driver d(0,options);d.work();d.host.world.priority_contract_current=false;d.host.world.self.npc_quiet=false;d.host.world.source_released=false;d.observe();
    CHECK(d.session.program().phase()==q::Phase::Abort);n::Independent grant{d.session.actual().source,21,22,true,true,true};d.host.independent_ok=false;CHECK(!d.session.handoff_paid_exit(grant));d.host.independent_ok=true;
    auto invalid=grant;invalid.funded=false;CHECK(!d.session.handoff_paid_exit(invalid));invalid=grant;invalid.complete=false;CHECK(!d.session.handoff_paid_exit(invalid));
    invalid=grant;invalid.accepted=false;CHECK(!d.session.handoff_paid_exit(invalid));invalid=grant;--invalid.source.sample;CHECK(!d.session.handoff_paid_exit(invalid));
    CHECK(d.session.handoff_paid_exit(grant));CHECK(d.session.program().phase()==q::Phase::Handoff&&d.session.wire_owned());CHECK(!d.session.actual().source_released&&!d.session.actual().self.npc_quiet);
    const auto done=d.session.program().completion();CHECK(done.members[0].handoff&&!done.members[0].released&&!done.members[0].payout&&!done.members[0].renewed&&done.quantities[0].banked_gems==0);
    CHECK(d.session.cleanup().status==n::Status::Complete&&d.session.wire_owned());d.host.remote(w::Phase::CleanupDone);d.observe();CHECK(!d.session.wire_owned()&&d.host.raw==0);
}
int main(){words();stale_and_refused();funded_readback();heal_basic();heal_failures();revelation();interrupted();known_prefix_unknown_tail();metadata_readback();protocol_regression();rejected_packet_authority();cumulative_publication();delayed_receipt();passive_arming();measure_now();paid_exit_handoff();std::printf("capital_party_program_native: %u checks, %u failures\n",checks,failures);return failures?1:0;}
