#include "../src/capital_party_program.h"
#include <cstdio>
#include <limits>
namespace q=wb::capital::party_program;
namespace p=wb::capital::party;
unsigned checks{},failures{};
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %u %s\n",__LINE__,#x);}}while(false)
q::Contract contract() {
    q::Contract c;c.id=9;c.life=3;c.group=2;c.start=10;c.commit_by=20;c.arrive_by=30;c.work_by=60;c.capture_by=70;c.bank_by=90;c.renew_by=110;c.abort_by=120;
    c.camps={41,42};c.camp_count=2;c.capture_mask=7;c.max_refusals=2;c.max_no_progress=5;c.require_useful_support=true;
    c.domain={1,2,3,4,5,6,10,110,q::Risk::ConditionalModel};
    for(unsigned i=0;i<3;++i){auto& m=c.members[i];m.id=static_cast<int>(i+4);m.role=static_cast<p::Role>(i);
        m.rendezvous={2+static_cast<int>(i),2};m.station={6+static_cast<int>(i),6};m.capture={6+static_cast<int>(i),7};m.release={2+static_cast<int>(i),5};
        m.altar={1,1};m.spawn={0,0};m.renewal={1+static_cast<int>(i),3};}
    return c;
}
q::Admission admission(){return {true,true,true,true,true,true,{true,true,true}};}
struct Driver {
    q::Contract c=contract();unsigned index{};q::Program program;q::Frame frame{};q::Revision event{100};
    explicit Driver(unsigned who=0):index(who),program(c,c.members[who].id,admission()) {
        frame.source={c.members[who].id,10,100,1,1,1,1,1,1,c.life};frame.fresh=frame.protocol_armed=frame.canonical_header=frame.priority_contract_current=true;
        frame.group_life_known=frame.capital_obligation_clear=frame.independent_interpreter_ready=true;frame.commitment_current={true,true,true};
        frame.self.id=c.members[who].id;frame.self.role=static_cast<p::Role>(who);frame.self.position=c.members[who].rendezvous;
        frame.self.hp=frame.self.max_hp=100;frame.self.alive=frame.self.move=frame.self.attack=frame.self.cast=true;frame.self.npc_quiet=true;
        frame.self.heal_ready=frame.self.shield_ready=frame.self.reset_ready=true;frame.self.heal_range=4;
        for(unsigned i=0;i<3;++i){frame.peers[i].observed_now=true;frame.peers[i].body=frame.self;frame.peers[i].body.id=c.members[i].id;
            frame.peers[i].body.role=static_cast<p::Role>(i);frame.peers[i].body.position=c.members[i].station;}
        frame.group.id=c.group;frame.group.count=2;frame.group.observed_now=true;
        frame.group.camps[0]={41,60,true,true,{7,7},3,true};frame.group.camps[1]={42,100,true,true,{8,7},3,true};
        CHECK(program.observe(frame)==q::Status::Ready);
    }
    void observe(){++frame.source.sample;CHECK(program.observe(frame)==q::Status::Ready);}
    void input(){++frame.source.round;++frame.source.input;++frame.source.sample;frame.self.move=frame.self.attack=frame.self.cast=true;CHECK(program.observe(frame)==q::Status::Ready);}
    q::Quote quote(const q::Offer& o){return {frame.source,c.id,o.serial,300+o.serial,c.domain,true,true,true,true,true,true,true,0};}
    q::Lease seal(q::Kind kind){auto result=program.next();CHECK(result.status==q::Status::Ready);CHECK(result.offer.kind==kind);auto priced=program.seal(result.offer,quote(result.offer));CHECK(priced.status==q::Status::Ready);CHECK(priced.lease.valid);return priced.lease;}
    q::Actual actual(q::ReceiptKind kind,q::Revision word=0){q::Actual a;a.kind=kind;a.event=++event;a.contract=c.id;a.life=c.life;a.actor=frame.self.id;a.group=c.group;
        a.word=word;a.observed_actual=a.accepted=a.complete_word=true;return a;}
    void finish(q::Lease l,q::Actual a) {CHECK(program.begin_attempt(l));CHECK(!program.begin_attempt(l));++frame.source.sample;a.word=l.word;
        CHECK(program.reconcile(l,frame,a)==q::Status::Ready);CHECK(!program.awaiting_actual());CHECK(program.reconcile(l,frame,a)==q::Status::Stale);}
    void report(unsigned who,p::Milestone phase) {p::TrustedPredicateReceipt r;r.contract=c.id;r.actor=c.members[who].id;r.role=static_cast<p::Role>(who);r.source_round=frame.source.round;
        r.milestone=phase;r.predicate=r.active_armed=r.whole_read=r.trusted_partition_and_producer=true;r.read_owner=frame.self.id;r.read_round=frame.source.round;
        r.read_input=frame.source.input;r.header_start=c.start;r.header_group=c.group;
        CHECK(program.ingest(r)==p::IngestStatus::Accepted);}
    void peers(p::Milestone phase){for(unsigned i=0;i<3;++i)if(i!=index)report(i,phase);}
    void work() {
        finish(seal(q::Kind::PublishCommit),actual(q::ReceiptKind::CommitPublished));peers(p::Milestone::Committed);
        CHECK(program.phase()==q::Phase::Assemble);peers(p::Milestone::Arrived);CHECK(program.phase()==q::Phase::Enter);
        frame.self.position=c.members[index].station;observe();peers(p::Milestone::Entered);CHECK(program.phase()==q::Phase::Work);
    }
};
void lifecycle() {
    Driver d;d.work();auto initial=d.program.next();CHECK(initial.offer.target==41);CHECK(initial.offer.kind==q::Kind::WorkTank);
    auto l=d.program.seal(initial.offer,d.quote(initial.offer)).lease;auto hit=d.actual(q::ReceiptKind::CampDamage);hit.target=41;hit.before=60;hit.after=0;
    d.frame.group.camps[0].hp=0;d.frame.group.camps[0].alive=false;d.finish(l,hit);
    CHECK(d.program.completion().quantities[0].work==60);CHECK(d.program.next().offer.target==42); // early kill invalidates primary
    d.input();d.frame.nominated_target=41;d.observe();CHECK(d.program.next().offer.target==42); // stale nomination cannot resurrect dead target
    auto second=d.seal(q::Kind::WorkTank);auto damage=d.actual(q::ReceiptKind::CampDamage);damage.target=42;damage.before=100;damage.after=0;
    d.frame.group.camps[1].hp=0;d.frame.group.camps[1].alive=false;d.frame.group.whole_clear=true;d.finish(second,damage);
    CHECK(d.program.phase()==q::Phase::Capture);CHECK(d.program.next().offer.kind==q::Kind::Capture);CHECK(d.program.completion().quantities[0].received_gems==0);
    auto reward=d.actual(q::ReceiptKind::GroupPayout);reward.gems=14;reward.gold=50;reward.source_covered=true;d.frame.self.bag=14;d.observe();CHECK(d.program.receipt(reward));CHECK(!d.program.receipt(reward));
    CHECK(d.program.completion().quantities[0].received_gems==14);d.peers(p::Milestone::Payout);CHECK(d.program.phase()==q::Phase::Release);
    d.frame.self.position=d.c.members[0].release;d.frame.source_released=true;d.observe();d.peers(p::Milestone::Released);CHECK(d.program.phase()==q::Phase::Bank);
    auto bank=d.seal(q::Kind::Bank);auto start=d.actual(q::ReceiptKind::None);d.frame.self.channel=p::Channel::Submit;d.frame.self.channel_remaining=2;d.finish(bank,start);
    CHECK(d.program.completion().quantities[0].banked_gems==0);CHECK(d.program.next().offer.kind==q::Kind::HoldChannel);
    d.input();d.frame.self.channel_remaining=1;d.observe();CHECK(d.program.completion().quantities[0].banked_gems==0);
    d.input();d.frame.self.channel=p::Channel::None;d.frame.self.channel_remaining=0;d.frame.self.bag=0;d.observe();auto payout=d.actual(q::ReceiptKind::BankPayout);payout.gems=14;
    CHECK(!d.program.receipt(payout));CHECK(d.program.completion().quantities[0].banked_gems==0); // bag0 alone cannot remint source attribution
    payout.event=++d.event;payout.bank_source_attribution_known=true;CHECK(d.program.receipt(payout));d.peers(p::Milestone::Banked);CHECK(d.program.phase()==q::Phase::Recover);
    d.frame.self.position=d.c.members[0].spawn;d.frame.self.at_spawn=true;d.frame.capital_obligation_clear=false;d.observe();CHECK(d.program.phase()==q::Phase::Recover);
    d.frame.capital_obligation_clear=true;d.observe();d.peers(p::Milestone::Recovered);CHECK(d.program.phase()==q::Phase::Renew);
    auto renewal=d.seal(q::Kind::Renew);d.finish(renewal,d.actual(q::ReceiptKind::Renewed));d.peers(p::Milestone::Renewed);CHECK(d.program.phase()==q::Phase::Complete);
    auto complete=d.program.completion();CHECK(complete.lifecycle_attested);CHECK(!complete.complete_observed);CHECK(complete.quantity_history_known[0]);CHECK(!complete.quantity_history_known[1]);
    CHECK(complete.quantities[0].work==160);CHECK(complete.quantities[0].banked_gems==14);CHECK(!complete.useful_support_observed);CHECK(d.program.wire_owned());
    CHECK(d.program.cleanup(4,d.c.id,true,true));CHECK(d.program.cleanup(5,d.c.id,true,true));CHECK(d.program.wire_owned());CHECK(d.program.cleanup(6,d.c.id,true,true));CHECK(!d.program.wire_owned());
}
void support_and_guards() {
    Driver d(1);d.work();d.frame.peers[0].body.hp=70;d.observe();auto o=d.program.next();CHECK(o.offer.kind==q::Kind::Heal);CHECK(o.offer.target==4);
    auto quote=d.quote(o.offer);quote.station_commitments=false;CHECK(d.program.seal(o.offer,quote).status==q::Status::NeedModel);quote.station_commitments=true;
    quote.required_support_event=99;CHECK(d.program.seal(o.offer,quote).status==q::Status::NeedModel);quote.required_support_event=0;
    auto l=d.program.seal(o.offer,quote).lease;auto heal=d.actual(q::ReceiptKind::Heal);heal.target=4;heal.before=70;heal.after=95;
    d.frame.peers[0].body.hp=95;d.finish(l,heal);CHECK(d.program.completion().quantities[1].useful_heal==25);CHECK(d.program.support_event()==heal.event);
    d.frame.self.cast=false;d.observe();CHECK(d.program.next().offer.kind==q::Kind::Strike); // cooldown/permission prevents borrowing the prior cast
    d.frame.self.cast=true;d.frame.peers[0].observed_now=false;d.observe();CHECK(d.program.next().offer.kind==q::Kind::Strike); // hidden tank not guaranteed support target
    auto fresh=d.program.next();auto q=d.quote(fresh.offer);q.required_support_event=heal.event;q.individual_npc=false;CHECK(d.program.seal(fresh.offer,q).status==q::Status::NeedModel);
    q.individual_npc=true;auto valid=d.program.seal(fresh.offer,q);CHECK(valid.status==q::Status::Ready);CHECK(d.program.next().status==q::Status::AwaitActual);
    auto bad=valid.lease;bad.token++;CHECK(!d.program.begin_attempt(bad));CHECK(d.program.begin_attempt(valid.lease));
    auto refused=d.actual(q::ReceiptKind::None);refused.accepted=false;refused.refused=true;refused.word=valid.lease.word;++d.frame.source.sample;
    CHECK(d.program.reconcile(valid.lease,d.frame,refused)==q::Status::Refused);CHECK(d.program.completion().quantities[1].useful_heal==25);
}
void abort_and_unknown() {
    Driver d;d.work();auto l=d.seal(q::Kind::WorkTank);auto hit=d.actual(q::ReceiptKind::CampDamage);hit.target=41;hit.before=60;hit.after=15;hit.unknown_suffix=true;
    CHECK(d.program.begin_attempt(l));++d.frame.source.sample;hit.word=l.word;CHECK(d.program.reconcile(l,d.frame,hit)==q::Status::Aborting);
    CHECK(d.program.completion().quantities[0].work==45);CHECK(!d.program.completion().quantity_history_known[0]);CHECK(d.program.failure()==q::Failure::UnknownEffect);
    CHECK(d.program.next().offer.kind==q::Kind::AbortExit);CHECK(!d.program.handoff(d.frame,true));d.frame.source_released=true;d.frame.self.npc_quiet=true;d.observe();
    CHECK(d.program.next().status==q::Status::Handoff);CHECK(d.program.handoff(d.frame,true));CHECK(d.program.wire_owned());
    CHECK(!d.program.cleanup(4,d.c.id,false,true));CHECK(d.program.cleanup(4,d.c.id,true,true));CHECK(d.program.cleanup(5,d.c.id,true,true));CHECK(d.program.cleanup(6,d.c.id,true,true));CHECK(!d.program.wire_owned());
    Driver lost;lost.work();lost.frame.commitment_current[2]=false;lost.observe();CHECK(lost.program.next().offer.kind==q::Kind::AbortExit);CHECK(lost.program.failure()==q::Failure::Commitment);
    Driver deadline;deadline.work();deadline.frame.source.round=61;deadline.frame.source.input++;deadline.observe();CHECK(deadline.program.phase()==q::Phase::Abort);CHECK(deadline.program.failure()==q::Failure::Deadline);
}
void comparison() {
    const auto c=contract();std::array<q::Service,3> joint{},solo{};
    for(unsigned i=0;i<3;++i){joint[i]={c.members[i].id,c.domain,q::Evidence::Forecast,8,true,true,true,true,20,0,50};solo[i]=joint[i];solo[i].scored=15;}
    auto a=q::compare(c,joint,solo);CHECK(a.comparable&&a.party_better);CHECK(a.party_scored==60&&a.independent_scored==45);
    solo[1].complete=false;CHECK(!q::compare(c,joint,solo).comparable);solo[1].complete=true;
    solo[1].carried=1;CHECK(!q::compare(c,joint,solo).comparable);solo[1].carried=0;
    solo[1].gold=0;CHECK(!q::compare(c,joint,solo).comparable);solo[1].gold=50;
    solo[1].evidence=q::Evidence::Actual;CHECK(!q::compare(c,joint,solo).comparable);solo[1].evidence=q::Evidence::Forecast;
    solo[1].reply=9;CHECK(!q::compare(c,joint,solo).comparable);solo[1].reply=8;
    solo[1].domain.horizon++;CHECK(!q::compare(c,joint,solo).comparable);solo[1].domain=c.domain;
    joint[0].scored=UINT64_MAX;CHECK(!q::compare(c,joint,solo).comparable);
}
void invalid_and_freshness() {
    auto c=contract();auto a=admission();a.exclusive_wire=false;q::Program disabled(c,4,a);Driver d;CHECK(disabled.observe(d.frame)==q::Status::NoOffer);
    q::Program stranger(c,99,admission());CHECK(stranger.observe(d.frame)==q::Status::NoOffer);
    c.members[1].id=4;q::Program invalid(c,4,admission());CHECK(invalid.observe(d.frame)==q::Status::NoOffer);
    auto o=d.program.next();auto q=d.quote(o.offer);q.source.effects++;CHECK(d.program.seal(o.offer,q).status==q::Status::Stale);
    q=d.quote(o.offer);q.domain.replies++;CHECK(d.program.seal(o.offer,q).status==q::Status::Stale);
    auto stale=d.frame;stale.source.sample=0;CHECK(d.program.observe(stale)==q::Status::NeedObservation);
    d.work();d.frame.group.camps[0].observed_now=false;d.observe();CHECK(d.program.next().status==q::Status::NeedObservation);
}
void preattempt_revocation() {
    // Every owner/epoch/dependency dimension revokes a prepared but unattempted
    // word. This is deliberately distinct from retaining attempted prefixes.
    for(unsigned field=0;field<9;++field){Driver d;d.work();auto l=d.seal(q::Kind::WorkTank);
        switch(field){case 0:++d.frame.source.sample;break;case 1:++d.frame.source.effects;break;case 2:++d.frame.source.geometry;break;
            case 3:++d.frame.source.foes;break;case 4:++d.frame.source.locks;break;case 5:++d.frame.source.order;break;
            case 6:++d.frame.source.round;++d.frame.source.input;break;case 7:d.frame.self.hp-=1;break;case 8:d.frame.peers[1].observed_now=false;break;}
        const auto status=d.program.observe(d.frame);CHECK(status==q::Status::Ready||status==q::Status::NeedObservation);CHECK(!d.program.begin_attempt(l));CHECK(!d.program.awaiting_actual());
    }
    Driver attempted;attempted.work();auto l=attempted.seal(q::Kind::WorkTank);CHECK(attempted.program.begin_attempt(l));++attempted.frame.source.foes;++attempted.frame.source.sample;
    CHECK(attempted.program.observe(attempted.frame)==q::Status::AwaitActual);CHECK(attempted.program.awaiting_actual());auto hit=attempted.actual(q::ReceiptKind::CampDamage);hit.word=l.word;
    hit.before=60;hit.after=15;hit.target=41;++attempted.frame.source.sample;CHECK(attempted.program.reconcile(l,attempted.frame,hit)==q::Status::Ready);
    CHECK(attempted.program.completion().quantities[0].work==45);
}
void primitive_prefixes() {
    Driver d;d.work();auto l=d.seal(q::Kind::WorkTank);CHECK(d.program.begin_attempt(l));
    auto hit=d.actual(q::ReceiptKind::CampDamage,l.word);hit.target=41;hit.before=60;hit.after=15;hit.complete_word=false;
    ++d.frame.source.sample;d.frame.group.camps[0].hp=15;d.frame.self.attack=false;
    CHECK(d.program.prefix(l,d.frame,hit)==q::Status::Ready);CHECK(d.program.awaiting_actual());CHECK(d.program.completion().physical_owned);
    CHECK(d.program.completion().quantities[0].work==45);CHECK(d.program.prefix(l,d.frame,hit)==q::Status::NeedObservation);
    // A reset was attempted but its return/effect became unknown across a new
    // owner input. Its alleged damage/reset is never credited; actual Basic45
    // survives and the whole input is not replayed.
    ++d.frame.source.round;++d.frame.source.input;d.frame.source.sample=1;d.frame.source.effects++;
    auto unknown=d.actual(q::ReceiptKind::None,l.word);unknown.observed_actual=unknown.accepted=unknown.complete_word=false;unknown.unknown_suffix=true;
    CHECK(d.program.reconcile(l,d.frame,unknown)==q::Status::Aborting);CHECK(d.program.failure()==q::Failure::UnknownEffect);
    CHECK(!d.program.awaiting_actual());CHECK(!d.program.completion().physical_owned);CHECK(d.program.wire_owned());
    CHECK(d.program.completion().quantities[0].work==45);CHECK(!d.program.completion().quantity_history_known[0]);CHECK(!d.program.begin_attempt(l));

    Driver shield;shield.work();auto s=shield.seal(q::Kind::WorkTank);CHECK(shield.program.begin_attempt(s));
    auto buff=shield.actual(q::ReceiptKind::Shield,s.word);buff.before=0;buff.after=30;buff.complete_word=false;++shield.frame.source.sample;shield.frame.self.shield=30;shield.frame.self.cast=false;
    CHECK(shield.program.prefix(s,shield.frame,buff)==q::Status::Ready);
    auto damage=shield.actual(q::ReceiptKind::CampDamage,s.word);damage.target=41;damage.before=60;damage.after=15;damage.complete_word=false;
    ++shield.frame.source.sample;shield.frame.group.camps[0].hp=15;shield.frame.self.attack=false;
    CHECK(shield.program.prefix(s,shield.frame,damage)==q::Status::Ready);
    auto complete=shield.program.completion();CHECK(complete.quantities[0].shield==30);CHECK(complete.quantities[0].work==45);CHECK(complete.physical_owned);
    q::Actual done;done.word=s.word;done.complete_word=done.accepted=true;
    CHECK(shield.program.reconcile(s,shield.frame,done)==q::Status::Ready);CHECK(!shield.program.completion().physical_owned);
    CHECK(shield.program.completion().quantities[0].shield==30);CHECK(shield.program.completion().quantities[0].work==45);
}
void spans_and_atomicity() {
    Driver d;d.work();auto l=d.seal(q::Kind::WorkTank);CHECK(d.program.begin_attempt(l));
    std::array<q::Actual,2> actuals{d.actual(q::ReceiptKind::CampDamage,l.word),d.actual(q::ReceiptKind::CampDamage,l.word)};
    actuals[0].target=41;actuals[0].before=60;actuals[0].after=15;actuals[1].target=42;actuals[1].before=100;actuals[1].after=55;
    ++d.frame.source.sample;d.frame.group.camps[0].hp=15;d.frame.group.camps[1].hp=55;
    const auto genuine=actuals[1];actuals[1].event=actuals[0].event;CHECK(d.program.prefix(l,d.frame,actuals)==q::Status::NeedModel);
    CHECK(d.program.completion().quantities[0].work==0);CHECK(d.program.awaiting_actual());
    actuals[1]=genuine;actuals[1].word++;CHECK(d.program.prefix(l,d.frame,actuals)==q::Status::NeedModel);CHECK(d.program.completion().quantities[0].work==0);
    actuals[1]=genuine;actuals[1].target=999;CHECK(d.program.prefix(l,d.frame,actuals)==q::Status::NeedModel);CHECK(d.program.completion().quantities[0].work==0);
    actuals[1]=genuine;CHECK(d.program.prefix(l,d.frame,actuals)==q::Status::Ready);CHECK(d.program.completion().quantities[0].work==90);
    ++d.frame.source.sample;CHECK(d.program.prefix(l,d.frame,actuals)==q::Status::NeedModel);CHECK(d.program.completion().quantities[0].work==90);
    std::array<q::Actual,p::max_effects+1> too_many{};CHECK(d.program.prefix(l,d.frame,too_many)==q::Status::NeedModel);
    CHECK(d.program.interrupt(l,d.frame)==q::Status::Ready);CHECK(!d.program.awaiting_actual());CHECK(d.program.completion().quantities[0].work==90);
}
void typed_interruptions() {
    for(unsigned change=0;change<4;++change){Driver d;d.work();auto l=d.seal(q::Kind::WorkTank);CHECK(d.program.begin_attempt(l));
        auto hit=d.actual(q::ReceiptKind::CampDamage,l.word);hit.target=41;hit.before=60;hit.after=15;++d.frame.source.sample;d.frame.group.camps[0].hp=15;
        CHECK(d.program.prefix(l,d.frame,hit)==q::Status::Ready);++d.frame.source.sample;
        if(change==0)++d.frame.source.foes;else if(change==1)++d.frame.source.locks;else if(change==2)++d.frame.source.geometry;else ++d.frame.source.life;
        CHECK(d.program.interrupt(l,d.frame,q::Failure::Interrupted)==q::Status::Aborting);
        CHECK(d.program.failure()==(change==3?q::Failure::ChangedLife:q::Failure::Interrupted));CHECK(d.program.completion().quantities[0].work==45);
        CHECK(d.program.completion().quantity_history_known[0]);CHECK(!d.program.awaiting_actual());CHECK(d.program.wire_owned());
    }
    // Killing the old primary invalidates only its unissued suffix. Fresh
    // nomination can use remaining actions against the next actual live camp.
    Driver retarget;retarget.work();auto l=retarget.seal(q::Kind::WorkTank);CHECK(retarget.program.begin_attempt(l));
    auto kill=retarget.actual(q::ReceiptKind::CampDamage,l.word);kill.target=41;kill.before=60;kill.after=0;++retarget.frame.source.sample;
    retarget.frame.group.camps[0].hp=0;retarget.frame.group.camps[0].alive=false;retarget.frame.self.attack=false;
    CHECK(retarget.program.prefix(l,retarget.frame,kill)==q::Status::Ready);CHECK(retarget.program.interrupt(l,retarget.frame)==q::Status::Ready);
    CHECK(retarget.program.failure()==q::Failure::None);CHECK(retarget.program.next().offer.target==42);CHECK(retarget.program.phase()==q::Phase::Work);
    auto next=retarget.seal(q::Kind::WorkTank);CHECK(retarget.program.interrupt(next,retarget.frame,q::Failure::Refusal)==q::Status::NeedModel);
    CHECK(retarget.program.interrupt(next,retarget.frame,q::Failure::UnknownEffect)==q::Status::NeedModel);
    CHECK(retarget.program.interrupt(next,retarget.frame)==q::Status::Ready);CHECK(retarget.program.completion().quantities[0].work==60);
}
void actual_time_ordering() {
    Driver d;d.work();auto l=d.seal(q::Kind::WorkTank);CHECK(d.program.begin_attempt(l));auto hit=d.actual(q::ReceiptKind::CampDamage,l.word);
    hit.target=41;hit.before=60;hit.after=15;
    CHECK(d.program.prefix(l,d.frame,hit)==q::Status::NeedObservation); // same-input unchanged sample
    auto conflicting=d.frame;conflicting.self.hp--;CHECK(d.program.prefix(l,conflicting,hit)==q::Status::NeedObservation);
    auto other_input=d.frame;other_input.source.input++;other_input.source.sample++;CHECK(d.program.prefix(l,other_input,hit)==q::Status::NeedObservation);
    ++d.frame.source.round;++d.frame.source.input;d.frame.source.sample=1;d.frame.group.camps[0].hp=15;
    CHECK(d.program.prefix(l,d.frame,hit)==q::Status::Ready);CHECK(d.program.completion().quantities[0].work==45);
    auto stale=d.frame;stale.source.sample=0;CHECK(d.program.interrupt(l,stale)==q::Status::NeedObservation);CHECK(d.program.awaiting_actual());
    auto wrong=d.frame;wrong.source.owner++;CHECK(d.program.interrupt(l,wrong)==q::Status::NeedObservation);
    CHECK(d.program.interrupt(l,d.frame)==q::Status::Ready);CHECK(d.program.completion().quantity_history_known[0]);
    d.frame.source.round++;d.frame.source.input++;d.frame.source.sample=1;CHECK(d.program.observe(d.frame)==q::Status::Ready);
}
void heal_basic_word() {
    Driver d(1);d.work();d.frame.peers[0].body.hp=40;d.observe();auto offered=d.program.next();CHECK(offered.offer.kind==q::Kind::Heal&&offered.offer.target==d.c.members[0].id);
    auto quoted=d.quote(offered.offer);quoted.camp_target=41;auto sealed=d.program.seal(offered.offer,quoted);CHECK(sealed.status==q::Status::Ready&&sealed.lease.camp_target==41);const auto l=sealed.lease;
    auto altered=l;altered.camp_target=42;CHECK(!d.program.begin_attempt(altered));CHECK(d.program.begin_attempt(l));
    auto hit=d.actual(q::ReceiptKind::CampDamage,l.word);hit.target=41;hit.before=60;hit.after=5;++d.frame.source.sample;
    CHECK(d.program.prefix(l,d.frame,hit)==q::Status::NeedModel);CHECK(d.program.completion().quantities[1].work==0); // no future heal borrowed
    auto heal=d.actual(q::ReceiptKind::Heal,l.word);heal.target=d.c.members[0].id;heal.before=40;heal.after=70;d.frame.peers[0].body.hp=70;d.frame.self.cast=false;++d.frame.source.sample;
    CHECK(d.program.prefix(l,d.frame,heal)==q::Status::Ready);CHECK(d.program.support_event()==heal.event);
    hit=d.actual(q::ReceiptKind::CampDamage,l.word);hit.target=42;hit.before=100;hit.after=45;++d.frame.source.sample;
    CHECK(d.program.prefix(l,d.frame,hit)==q::Status::NeedModel);CHECK(d.program.completion().quantities[1].work==0);
    hit.target=41;hit.before=60;hit.after=5;d.frame.group.camps[0].hp=5;d.frame.self.attack=false;
    CHECK(d.program.prefix(l,d.frame,hit)==q::Status::Ready);q::Actual done;done.word=l.word;done.accepted=done.complete_word=true;CHECK(d.program.reconcile(l,d.frame,done)==q::Status::Ready);
    CHECK(d.program.completion().quantities[1].useful_heal==30&&d.program.completion().quantities[1].work==55);CHECK(!d.frame.self.cast&&!d.frame.self.attack);
    for(int target:{-1,41,42,999}){Driver invalid(1);invalid.work();invalid.frame.peers[0].body.hp=40;invalid.observe();auto o=invalid.program.next();auto q=invalid.quote(o.offer);q.camp_target=target;
        if(target==41){invalid.frame.group.camps[0].hp=0;invalid.frame.group.camps[0].alive=false;invalid.observe();o=invalid.program.next();q=invalid.quote(o.offer);q.camp_target=target;}
        if(target==42){invalid.frame.group.camps[1].observed_now=false;invalid.observe();CHECK(invalid.program.next().status==q::Status::NeedObservation);continue;}
        CHECK(invalid.program.seal(o.offer,q).status==(target==-1?q::Status::Ready:q::Status::NeedModel));}
    Driver refused(1);refused.work();refused.frame.peers[0].body.hp=40;refused.observe();auto o=refused.program.next();auto quote=refused.quote(o.offer);quote.camp_target=41;auto lease=refused.program.seal(o.offer,quote).lease;CHECK(refused.program.begin_attempt(lease));
    ++refused.frame.source.sample;q::Actual failure;failure.word=lease.word;failure.refused=failure.complete_word=true;CHECK(refused.program.reconcile(lease,refused.frame,failure)==q::Status::Refused);
    CHECK(refused.program.completion().quantities[1].useful_heal==0&&refused.program.completion().quantities[1].work==0);
}
void paid_exit_handoff(){Driver d;d.work();CHECK(!d.program.handoff_paid_exit(d.frame,true));d.frame.self.npc_quiet=false;d.frame.source_released=false;
    d.frame.priority_contract_current=false;d.observe();CHECK(d.program.phase()==q::Phase::Abort&&d.program.next().offer.kind==q::Kind::AbortExit);
    CHECK(!d.program.handoff(d.frame,true));CHECK(!d.program.handoff_paid_exit(d.frame,false));auto stale=d.frame;--stale.source.sample;CHECK(!d.program.handoff_paid_exit(stale,true));
    auto altered=d.frame;--altered.self.hp;CHECK(!d.program.handoff_paid_exit(altered,true));
    CHECK(d.program.handoff_paid_exit(d.frame,true));const auto done=d.program.completion();CHECK(done.members[0].handoff&&!done.members[0].released&&!done.members[0].payout&&!done.members[0].renewed);
    CHECK(!done.lifecycle_attested&&done.quantities[0].received_gems==0&&done.quantities[0].banked_gems==0&&d.program.wire_owned());
    CHECK(d.program.cleanup(4,d.c.id,true,true));CHECK(d.program.wire_owned());CHECK(d.program.cleanup(5,d.c.id,true,true));CHECK(d.program.cleanup(6,d.c.id,true,true));CHECK(!d.program.wire_owned());
    Driver pending;pending.work();const auto lease=pending.seal(q::Kind::WorkTank);CHECK(pending.program.begin_attempt(lease));pending.frame.priority_contract_current=false;++pending.frame.source.sample;
    CHECK(pending.program.observe(pending.frame)==q::Status::AwaitActual);CHECK(!pending.program.handoff_paid_exit(pending.frame,true));
}
int main(){lifecycle();support_and_guards();abort_and_unknown();comparison();invalid_and_freshness();preattempt_revocation();primitive_prefixes();spans_and_atomicity();typed_interruptions();actual_time_ordering();heal_basic_word();paid_exit_handoff();std::printf("capital_party_program %u checks %u failures\n",checks,failures);return failures?1:0;}
