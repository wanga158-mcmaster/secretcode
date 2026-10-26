#define main original_party_fixture_main
#include "capital_party.cpp"
#undef main
#define WB_PARTY_NATIVE_HOST_FREE
#include "capital_party_native.h"
namespace n=wb::capital::party_native;
struct FakeHost {
    Simulation& sim;std::array<std::uint16_t,4>& memory;int who,now{};unsigned available{1000000};
    unsigned reads{},writes{},actions{},legal_queries{};bool suspend_read{},suspend_write{},suspend_action{},refuse{},throw_action{};
    bool fail_facts{},deny_guard{},deny_metadata{},abort_after_action{};unsigned guard_calls{};
    unsigned facts_fail_after{},write_advance{1},interrupt_on_action{},refuse_on_action{},throw_on_action{},core_fail_after{},guard_suspend_after{},legal_suspend_after{},finish_suspend_after{},round_advance_after{};
    FakeHost(Simulation& s,std::array<std::uint16_t,4>& bytes,int owner):sim(s),memory(bytes),who(owner){}
    int owner(){return who;}int round(){if(round_advance_after&&!--round_advance_after)++now;return now;}unsigned fuel(){return available;}
    void read_words(std::array<std::uint16_t,4>& out){++reads;out=memory;if(suspend_read){suspend_read=false;++now;}}
    void write_word(unsigned offset,std::uint16_t word){CHECK(offset%2==0&&offset<=6);++writes;memory[offset/2]=word;if(suspend_write){suspend_write=false;now+=static_cast<int>(write_advance);}}
    bool observe(n::Epoch e,const Contract&,n::Observation& out) {
        out={};if(core_fail_after&&actions>=core_fail_after)return false;out.frame=sim.frame(static_cast<unsigned>(who),now);out.frame.input=e.input;
        out.frame.self.capital_revision=e.capital_revision;out.frame.peers[static_cast<unsigned>(who)].body=out.frame.self;
        for(auto& peer:out.frame.peers)peer.body.npc_quiet=false;out.frame.self.npc_quiet=false;
        out.own_core_known=true;out.private_economy_known[static_cast<unsigned>(who)]=true;
        out.move_speed=1;out.attack_range=1;
        out.abilities[0]={who==0?Cast::Shield:who==1?Cast::Heal:Cast::Dash,0,4,who==1?4u:who==2?16u:8u,false,false,who==2};
        out.abilities[1]={Cast::Reset,0,0,8,false,false,false};return true;
    }
    bool legal(n::Epoch e,const n::Observation& o,const WordIntent& intent,const n::Primitive& step,bool walked,bool cast_used,bool exclusive_used) {
        ++legal_queries;if(legal_suspend_after&&actions>=legal_suspend_after){legal_suspend_after=0;++now;}
        if(now!=e.round||!o.frame.self.alive||o.frame.self.stunned||o.frame.self.channel!=Channel::None)return false;
        const auto& b=o.frame.self;
        if(step.kind==n::PrimitiveKind::Move)return b.move&&!exclusive_used&&distance(b.position,step.destination)<=1;
        if(step.kind==n::PrimitiveKind::Basic)return b.attack&&step.target==intent.camp;
        if(step.kind==n::PrimitiveKind::Submit)return b.bag&&b.altar_adjacent;
        if(step.kind==n::PrimitiveKind::Recall)return b.recall_ticks>0;
        if(!b.cast||cast_used||step.slot<0||step.slot>=2)return false;
        return !o.abilities[static_cast<unsigned>(step.slot)].exclusive||!walked;
    }
    bool apply(n::Epoch,const n::Primitive& step) {
        ++actions;if(throw_action||actions==throw_on_action)throw std::runtime_error("error fixture action");
        if(refuse||actions==refuse_on_action){refuse=false;if(actions==interrupt_on_action)++now;return false;}
        auto& b=sim.body[static_cast<unsigned>(who)];
        const int script=now-sim.c.start_round+1;
        switch(step.kind) {
        case n::PrimitiveKind::Move:b.position=step.destination;b.move=false;break;
        case n::PrimitiveKind::Basic:sim.hp[0]=std::max(0.f,sim.hp[0]-(who==2?40.f:script==6?30.f:15.f));b.attack=false;break;
        case n::PrimitiveKind::Shield:b.shield=20;b.cast=false;break;
        case n::PrimitiveKind::Reset:b.attack=true;b.cast=false;break;
        case n::PrimitiveKind::Heal:sim.body[static_cast<unsigned>(step.target)].hp=100;b.cast=false;break;
        case n::PrimitiveKind::Dash:b.position=step.destination;b.cast=false;break;
        case n::PrimitiveKind::Submit:case n::PrimitiveKind::Recall:
            b.channel=step.kind==n::PrimitiveKind::Submit?Channel::Submit:Channel::Recall;
            sim.channel_start[static_cast<unsigned>(who)]=now;
            sim.channel_ticks[static_cast<unsigned>(who)]=step.kind==n::PrimitiveKind::Submit?b.submit_ticks:b.recall_ticks;
            b.channel_remaining=sim.channel_ticks[static_cast<unsigned>(who)];break;
        }
        if(abort_after_action)memory[2]=wire::encode_record(now,wire::Phase::Abort,false).word;
        if(suspend_action||actions==interrupt_on_action){suspend_action=false;++now;}return true;
    }
};
n::Admission admitted(){return {true,true,true,true,true,true,true};}
n::Epoch epoch(FakeHost& h){return {h.who,h.now,static_cast<std::uint64_t>(h.now*10+h.who+1),5};}
bool facts(void* context,const n::Observation& observation,n::Auxiliary& out) {
    auto& h=*static_cast<FakeHost*>(context);if(h.fail_facts&&h.actions>h.facts_fail_after)return false;
    const auto& f=observation.frame;out.owner=f.owner;out.round=f.round;out.input=f.input;out.contract=h.sim.c.id;out.fresh=true;
    for(unsigned i=0;i<actors;++i){out.npc_quiet_known[i]=true;out.npc_quiet[i]=h.sim.body[i].npc_quiet;}
    out.capture_state_known=out.capture_own=true;out.claimed_known=true;out.claimed=h.sim.claimed;
    out.payout={h.sim.paid[static_cast<unsigned>(h.who)],h.sim.c.group,h.sim.c.capture_mask};out.recall_quote_known=true;out.recall_ticks=2;return true;
}
bool permit(void* context,const n::Observation&,const WordIntent&,const n::Word&,unsigned) {
    auto& h=*static_cast<FakeHost*>(context);++h.guard_calls;
    if(h.guard_suspend_after&&h.actions>=h.guard_suspend_after){h.guard_suspend_after=0;++h.now;}
    if(h.finish_suspend_after&&h.actions>=h.finish_suspend_after){h.finish_suspend_after=0;h.round_advance_after=2;return false;}
    return !h.deny_guard;
}
bool funding(void* context,n::Epoch e,const Contract& c){auto& h=*static_cast<FakeHost*>(context);return !h.deny_metadata&&e.owner==h.who&&e.round==h.now&&c.id==h.sim.c.id;}
n::Word compile_word(const WordIntent& proposal,const n::Observation& observation) {
    n::Word word;word.contract=proposal.contract;word.owner=proposal.owner;word.round=proposal.round;word.input=proposal.input;
    word.token=proposal.input*100+static_cast<unsigned>(proposal.kind)+1;
    auto add=[&](n::PrimitiveKind kind,int slot=-1,int target=-1,Point destination={}){word.steps[word.count++]={kind,destination,target,slot};};
    const int script=proposal.round-1;
    if(proposal.kind==Kind::WorkTank) {
        if(script==6)add(n::PrimitiveKind::Shield,0);
        add(n::PrimitiveKind::Basic,-1,proposal.camp);
        if(script==7){add(n::PrimitiveKind::Reset,1);add(n::PrimitiveKind::Basic,-1,proposal.camp);}
    } else if(proposal.kind==Kind::Strike)add(n::PrimitiveKind::Basic,-1,proposal.camp);
    else if(proposal.kind==Kind::SupportHeal)add(n::PrimitiveKind::Heal,0,proposal.recipient);
    else if(proposal.kind==Kind::Submit)add(n::PrimitiveKind::Submit,-1,-1,proposal.destination);
    else if(proposal.kind==Kind::Recover&&observation.frame.self.channel==Channel::None&&!observation.frame.self.at_spawn)add(n::PrimitiveKind::Recall);
    else if(proposal.kind!=Kind::Commit&&proposal.kind!=Kind::HoldChannel&&proposal.kind!=Kind::HoldRecovery
        &&proposal.kind!=Kind::ApproachAltar&&observation.frame.self.position!=proposal.destination)
        add(n::PrimitiveKind::Move,-1,-1,step(observation.frame.self.position,proposal.destination));
    else if(proposal.kind==Kind::ApproachAltar&&distance(observation.frame.self.position,proposal.destination)>1)
        add(n::PrimitiveKind::Move,-1,-1,step(observation.frame.self.position,proposal.destination));
    return word;
}
void full_bridge() {
    const auto c=distributed_contract();Simulation sim(c);std::array<std::uint16_t,4> memory{};
    FakeHost a(sim,memory,0),b(sim,memory,1),d(sim,memory,2);
    std::array<FakeHost*,3> hosts{&a,&b,&d};
    n::Session<FakeHost> sa(a,c,0,admitted()),sb(b,c,1,admitted()),sd(d,c,2,admitted());
    std::array<n::Session<FakeHost>*,3> sessions{&sa,&sb,&sd};
    std::array<Controller,3> controllers{Controller(c,0),Controller(c,1),Controller(c,2)};
    CHECK(sa.begin(epoch(a))==n::Status::Ready);
    CHECK(sa.propose(static_cast<unsigned>(c.group),true,{&a,funding}).status==n::Status::Complete);
    for(unsigned i=1;i<3;++i){CHECK(sessions[i]->begin(epoch(*hosts[i]))==n::Status::Ready);n::Observation o;
        CHECK(sessions[i]->observe(controllers[i],o,{hosts[i],facts})==n::Status::Prestart);}
    unsigned hp_receipts=0,shield_receipts=0,heal_receipts=0,submit_receipts=0,recall_receipts=0;
    for(int r=c.start_round;r<=c.renew_by;++r) {
        sim.begin_round(r);
        for(unsigned i=0;i<3;++i) {
            auto& h=*hosts[i];auto& session=*sessions[i];auto& controller=controllers[i];h.now=r;
            CHECK(session.begin(epoch(h))==n::Status::Ready);n::Observation o;
            CHECK(session.observe(controller,o,{&h,facts})==n::Status::Ready);
            auto proposal=controller.tick(o.frame);
            if(proposal.status==Status::NeedModel) {
                auto word=compile_word(proposal.intent,o);Cast cast=Cast::None;
                for(unsigned s=0;s<word.count;++s){if(word.steps[s].kind==n::PrimitiveKind::Shield)cast=Cast::Shield;
                    if(word.steps[s].kind==n::PrimitiveKind::Reset)cast=Cast::Reset;if(word.steps[s].kind==n::PrimitiveKind::Heal)cast=Cast::Heal;}
                const auto guard=sim.guard(o.frame,proposal.intent,cast);
                if(i==0&&r==7) {
                    const auto saved=sim;const auto bytes=memory;const auto attempted=h.actions;
                    auto branch=session;auto local=controller;auto issued_branch=branch.admit(local,guard,word);
                    CHECK(issued_branch.status==Status::Ready);h.deny_guard=true;
                    const auto denied=branch.execute(local,issued_branch.intent,word,{&h,facts},{&h,permit});h.deny_guard=false;
                    CHECK(denied.status==n::Status::NeedModel&&!denied.claimed&&!denied.attempted&&h.actions==attempted);
                    CHECK(local.result().members[0].camp_work==0&&local.result().members[0].actual_shield==0);
                    sim=saved;memory=bytes;
                    auto cancelled_branch=session;auto cancelled_local=controller;
                    const auto cancelled_issued=cancelled_branch.admit(cancelled_local,guard,word);h.abort_after_action=true;
                    const auto cancelled=cancelled_branch.execute(cancelled_local,cancelled_issued.intent,word,{&h,facts},{&h,permit});h.abort_after_action=false;
                    CHECK(cancelled.status==n::Status::NeedModel&&cancelled.claimed&&cancelled.accepted_steps==1);
                    CHECK(cancelled.tape.count==1&&cancelled.tape.effects[0].kind==EffectKind::ShieldCast);
                    CHECK(cancelled_local.result().members[0].actual_shield==20&&cancelled_local.result().members[0].camp_work==0&&cancelled_local.progress().cancelled);
                    sim=saved;memory=bytes;
                }
                if(i==0&&r==8) {
                    const auto saved=sim;const auto bytes=memory;
                    auto branch=session;auto local=controller;auto issued_branch=branch.admit(local,guard,word);
                    CHECK(issued_branch.status==Status::Ready);h.fail_facts=true;h.facts_fail_after=h.actions;
                    const auto partial=branch.execute(local,issued_branch.intent,word,{&h,facts},{&h,permit});h.fail_facts=false;
                    CHECK(partial.status==n::Status::NeedModel&&partial.claimed&&partial.after_known&&!partial.unknown_effect);
                    CHECK(partial.tape.count==1&&partial.tape.effects[0].kind==EffectKind::BasicDamage
                        &&partial.tape.effects[0].before==30&&partial.tape.effects[0].after==15);
                    CHECK(local.result().members[0].camp_work==45&&branch.retained_prefix().count==1);
                    sim=saved;memory=bytes;
                    // Every branch starts from the same actual pre-word state.
                    // The first basic is dated/observed; the second reset may
                    // return, refuse, throw, or lose readback across the input.
                    for(unsigned mode=0;mode<9;++mode) {
                        const auto baseline=sim;const auto bytes_before=memory;const auto baseline_actions=h.actions;
                        auto interrupted_session=session;auto interrupted_controller=controller;
                        const auto interrupted_intent=interrupted_session.admit(interrupted_controller,guard,word);
                        CHECK(interrupted_intent.status==Status::Ready);
                        auto forged=interrupted_intent.intent;++forged.serial;Tape empty;empty.word_token=word.token;
                        CHECK(!interrupted_controller.interrupt(forged,o.frame,empty,r+1).matched&&!interrupted_controller.progress().cancelled);
                        n::Result interrupted;
                        if(mode!=5) {
                            if(mode==0||mode==1)h.interrupt_on_action=baseline_actions+2;
                            if(mode==1)h.refuse_on_action=baseline_actions+2;
                            if(mode==2)h.throw_on_action=baseline_actions+2;
                            if(mode==3)h.core_fail_after=baseline_actions+2;
                            if(mode==4)h.interrupt_on_action=baseline_actions+1;
                            if(mode==6)h.guard_suspend_after=baseline_actions+1;
                            if(mode==7)h.legal_suspend_after=baseline_actions+1;
                            if(mode==8)h.finish_suspend_after=baseline_actions+1;
                            bool threw=false;
                            try {interrupted=interrupted_session.execute(interrupted_controller,interrupted_intent.intent,word,{&h,facts},{&h,permit});}
                            catch(const std::runtime_error&){threw=true;}
                            CHECK(threw==(mode==2));
                            if(mode==8)CHECK(interrupted.status==n::Status::Stale&&!interrupted.after_known&&!interrupted.after.frame.fresh);
                            CHECK(interrupted_session.unknown_action()==(mode<5)&&interrupted_session.claimed()&&interrupted_session.needs_reconciliation());
                            const auto no_replay=h.actions;
                            const auto replay=interrupted_session.execute(interrupted_controller,interrupted_intent.intent,word,{&h,facts},{&h,permit});
                            CHECK((replay.status==n::Status::Pending||replay.status==n::Status::NeedModel)&&h.actions==no_replay);
                        }
                        h.interrupt_on_action=h.refuse_on_action=h.throw_on_action=h.core_fail_after=h.guard_suspend_after=h.legal_suspend_after=h.finish_suspend_after=h.round_advance_after=0;
                        const int next_round=r+(mode==0?9:1);h.now=next_round;sim.begin_round(h.now);
                        // Actual current permissions are returned, never an
                        // invented all-ready or all-spent closure.
                        if(mode==4){sim.body[0].stunned=true;sim.body[0].move=false;sim.body[0].attack=false;sim.body[0].cast=false;}
                        CHECK(interrupted_session.begin(epoch(h))==n::Status::Stale);
                        const auto calls=h.actions;const auto writes=h.writes;
                        Controller foreign(c,0);
                        CHECK(interrupted_session.reconcile(foreign,{&h,facts}).status==n::Status::Pending);
                        if(mode==0) {
                            h.available=0;const auto deferred=interrupted_session.reconcile(interrupted_controller,{&h,facts});h.available=1000000;
                            CHECK(!deferred.actual_known&&!deferred.physical_released&&interrupted_session.unknown_action());
                        }
                        const auto closed=interrupted_session.reconcile(interrupted_controller,{&h,facts});
                        CHECK(closed.status==n::Status::Complete&&closed.actual_known&&closed.physical_released&&closed.prefix_retained);
                        CHECK(closed.controller_reconciled&&!interrupted_session.unknown_action()&&!interrupted_session.claimed());
                        CHECK(!closed.prefix_confirmed); // planned Reset was never semantically observed
                        CHECK(closed.intent.serial==interrupted_intent.intent.serial&&closed.prefix.word_token==interrupted_intent.intent.word_token);
                        CHECK(closed.prefix.count==((mode<4||mode>=6)?1u:0u));
                        CHECK(closed.unknown_effect==(mode<5)&&closed.uncertain.attempted==(mode<5));
                        CHECK(closed.uncertain.returned==(mode<5&&mode!=2));
                        CHECK(closed.uncertain.accepted==(mode<5&&mode!=1&&mode!=2));
                        CHECK(interrupted_controller.result().members[0].camp_work==((mode<4||mode>=6)?45.f:30.f));
                        CHECK(interrupted_controller.phase()==Phase::AbortCleanup&&interrupted_controller.progress().cancelled);
                        CHECK(!interrupted_controller.result().complete_observed&&!interrupted_controller.progress().attested_complete);
                        CHECK(closed.actual.frame.round==next_round&&closed.actual.frame.input==epoch(h).input);
                        CHECK(closed.actual.frame.self.move==(mode!=4)&&closed.actual.frame.self.attack==(mode!=4)&&closed.actual.frame.self.cast==(mode!=4));
                        CHECK(h.actions==calls&&h.writes==writes);
                        CHECK(!interrupted_session.needs_reconciliation());
                        CHECK(interrupted_session.execute(interrupted_controller,interrupted_intent.intent,word,{&h,facts},{&h,permit}).status==n::Status::NeedModel&&h.actions==calls);
                        const auto duplicate=interrupted_session.reconcile(interrupted_controller,{&h,facts});
                        CHECK(duplicate.status==n::Status::NoOffer&&!duplicate.prefix_retained&&!duplicate.physical_released&&h.actions==calls);
                        // The caller may now price a new cleanup word using
                        // this actual frame; no old ticket remains live.
                        const auto cleanup=interrupted_controller.tick(closed.actual.frame);
                        CHECK(cleanup.status==Status::NeedModel||cleanup.status==Status::Aborting);
                        CHECK(interrupted_controller.result().members[0].camp_work==((mode<4||mode>=6)?45.f:30.f));
                        sim=baseline;memory=bytes_before;h.now=r;
                    }
                    auto stale_branch=session;auto stale_local=controller;const auto stale_issued=stale_branch.admit(stale_local,guard,word);h.suspend_action=true;
                    const auto stale=stale_branch.execute(stale_local,stale_issued.intent,word,{&h,facts},{&h,permit});
                    CHECK(stale.status==n::Status::Stale&&stale.claimed&&stale.unknown_effect&&stale_branch.unknown_action());
                    const auto calls=h.actions;CHECK(stale_branch.begin(epoch(h))==n::Status::Stale&&stale_branch.claimed()&&h.actions==calls);
                    h.now=r;sim=saved;memory=bytes;
                }
                if(i==2&&r==7) {
                    auto branch=session;auto local=controller;auto dash=word;dash.steps[0]={n::PrimitiveKind::Dash,{14,10},-1,0};
                    const auto dash_guard=sim.guard(o.frame,proposal.intent,Cast::Dash);const auto calls=h.actions;
                    CHECK(branch.admit(local,dash_guard,dash).status==Status::NeedModel&&h.actions==calls&&!branch.claimed());
                }
                auto issued=session.admit(controller,guard,word);CHECK(issued.status==Status::Ready);
                if(i==0&&r==7) {
                    auto forged=issued.intent;++forged.serial;const auto calls=h.actions;
                    CHECK(session.execute(controller,forged,word,{&h,facts},{&h,permit}).status==n::Status::NeedModel&&h.actions==calls);
                    auto changed=word;++changed.steps[1].target;
                    CHECK(session.execute(controller,issued.intent,changed,{&h,facts},{&h,permit}).status==n::Status::NeedModel&&h.actions==calls);
                }
                auto result=session.execute(controller,issued.intent,word,{&h,facts},{&h,permit});
                if(result.status!=n::Status::Complete||!result.reconciled){std::printf("native r%d i%u stop%u reconciled%d phase%u failure%u\n",r,i,static_cast<unsigned>(result.status),result.reconciled,static_cast<unsigned>(controller.phase()),static_cast<unsigned>(controller.result().failure));CHECK(false);return;}
                CHECK(result.claimed&&result.after_known);
                if(i==0&&r==7){const auto calls=h.actions;const auto replay=session.execute(controller,issued.intent,word,{&h,facts},{&h,permit});
                    CHECK(replay.status==n::Status::NeedModel&&replay.claimed&&h.actions==calls);}
                for(unsigned t=0;t<result.tape.count;++t){const auto& effect=result.tape.effects[t];
                    hp_receipts+=effect.kind==EffectKind::BasicDamage;shield_receipts+=effect.kind==EffectKind::ShieldCast;
                    heal_receipts+=effect.kind==EffectKind::HealCast;submit_receipts+=effect.kind==EffectKind::SubmitStarted;recall_receipts+=effect.kind==EffectKind::RecallStarted;}
                o=result.after;
                if(issued.intent.kind==Kind::Commit)sim.committed[i]=true;
                else CHECK(session.publish(controller,o,{&h,funding}).status==n::Status::Complete);
            } else {
                CHECK(proposal.status==Status::AlreadyIssued||proposal.status==Status::AttestedComplete);
                CHECK(session.publish(controller,o,{&h,funding}).status==n::Status::Complete);
            }
        }
        sim.end_round();bool done=true;for(const auto& controller:controllers)done&=controller.progress().attested_complete;if(done)break;
    }
    CHECK(hp_receipts==4&&shield_receipts==1&&heal_receipts==1&&submit_receipts==3&&recall_receipts==3);
    CHECK(controllers[0].result().members[0].camp_work==60&&controllers[1].result().members[1].useful_heal==25&&controllers[2].result().members[2].camp_work==40);
    for(unsigned i=0;i<3;++i){CHECK(controllers[i].progress().attested_complete&&!controllers[i].result().complete_observed);
        for(unsigned j=0;j<3;++j)CHECK(controllers[i].result().quantity_history_known[j]==(i==j));}
}
void no_offer_and_wire_fences() {
    const auto c=distributed_contract();Simulation sim(c);std::array<std::uint16_t,4> memory{};FakeHost h(sim,memory,0);
    n::Session<FakeHost> disabled(h,c,0,{});CHECK(disabled.begin(epoch(h))==n::Status::NoOffer&&h.reads==0&&h.writes==0);
    n::Session<FakeHost> session(h,c,0,admitted());CHECK(session.begin(epoch(h))==n::Status::Ready);
    h.deny_metadata=true;CHECK(session.propose(c.group,true,{&h,funding}).status==n::Status::NoOffer&&h.writes==0&&!session.claimed());h.deny_metadata=false;
    CHECK(session.propose(c.group+1,true,{&h,funding}).status==n::Status::NoOffer&&h.writes==0);
    h.suspend_write=true;CHECK(session.propose(c.group,true,{&h,funding}).status==n::Status::Pending);
    CHECK(session.claimed()&&h.writes==1);CHECK(session.begin(epoch(h))==n::Status::Stale&&session.claimed()&&h.writes==1);
    Controller controller(c,0);const auto metadata=session.reconcile(controller,{&h,facts});
    CHECK(metadata.status==n::Status::Complete&&metadata.metadata_closed&&metadata.actual_known&&metadata.physical_released);
    CHECK(!metadata.prefix_retained&&!metadata.controller_reconciled&&!session.claimed()&&!session.needs_reconciliation()&&h.actions==0&&h.writes==1);
    h.now=2;CHECK(session.begin(epoch(h))==n::Status::Ready);
    n::Observation o;CHECK(session.observe(controller,o,{&h,facts})==n::Status::Ready); // actual resumed readback was still strictly future
    const auto old=controller.tick(o.frame);auto old_word=compile_word(old.intent,o);const auto old_guard=sim.guard(o.frame,old.intent);
    h.now=3;CHECK(session.begin(epoch(h))==n::Status::Ready);old_word.round=3;old_word.input=epoch(h).input;
    CHECK(session.admit(controller,old_guard,old_word).status==Status::NeedModel&&h.actions==0); // no old-frame private ticket poisoning
    memory[0]=wire::encode_header(6,c.group).word;h.now=3;CHECK(session.begin(epoch(h))==n::Status::Ready);
    CHECK(session.observe(controller,o,{&h,facts})==n::Status::NoOffer); // different contract may not arm
    n::Session<FakeHost> wrong(h,c,1,admitted());CHECK(wrong.begin(epoch(h))==n::Status::Stale);
    h.suspend_read=true;CHECK(session.observe(controller,o,{&h,facts})==n::Status::Stale&&!session.claimed());
    std::array<std::uint16_t,4> late_memory{};FakeHost late(sim,late_memory,0);late.write_advance=2;
    n::Session<FakeHost> late_session(late,c,0,admitted());CHECK(late_session.begin(epoch(late))==n::Status::Ready);
    late.suspend_write=true;CHECK(late_session.propose(c.group,true,{&late,funding}).status==n::Status::Pending);
    CHECK(late_session.begin(epoch(late))==n::Status::Stale&&late_session.claimed());
    const auto late_closed=late_session.reconcile(controller,{&late,facts});
    CHECK(late_closed.status==n::Status::Complete&&late_closed.actual_known&&late_closed.physical_released&&!late_session.claimed());
    ++late.now;CHECK(late_session.begin(epoch(late))==n::Status::Ready);
    CHECK(late_session.observe(controller,o,{&late,facts})==n::Status::NoOffer); // first actual readback was already too late to arm
}
int main(){full_bridge();no_offer_and_wire_fences();std::printf("party_native_reconcile: %u checks, %u failures\n",checks,failures);return failures?1:0;}
