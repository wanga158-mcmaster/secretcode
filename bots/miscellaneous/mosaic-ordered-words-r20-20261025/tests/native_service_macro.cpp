#include "native_service_macro.h"
#include <cassert>
#include <cstdio>
using namespace mosaic::native_service;
namespace {
unsigned checks{};
void check(bool value){++checks;assert(value);}
SelfFact observed(Epoch epoch={9,1,0},int gems=0,bool gems_actual=true){
    SelfFact self;self.epoch=epoch;self.id=1;self.alive=true;self.hp=self.max_hp=100;
    self.body_actual=self.buffs_actual=self.channel_actual=true;
    self.gems=gems;self.gems_actual=gems_actual;return self;
}
ServiceJob job(NativeId group=100,NativeId member=10){
    ServiceJob value;value.group_id=group;value.member_id=member;
    value.group_ordinal=2;value.member_ordinal=3;return value;
}
ServiceState banking(int bag=10,bool known=true){
    ServiceState state;state.phase=state.resume_phase=ServicePhase::work;
    state.bag=bag;state.bag_actual=known;state.bank_obligation=known&&bag>0;
    check(ack_service(state,1,PrimitiveKind::bank,ServiceApplied::yes));return state;
}
ServiceSignals completed(Epoch epoch,bool amount_known=false,unsigned amount=0){
    ServiceSignals signals;signals.epoch=epoch;
    signals.bank_completion_actual=signals.bank_completed=true;
    signals.bank_amount_actual=amount_known;signals.banked_gems=amount;return signals;
}
}
int main(){
    // Group evidence persists, but one member's death does not transfer to a
    // different member. Repeating the same nomination preserves that evidence.
    ServiceState state;retain_job(state,job());state.job.active=true;state.job.member_dead=true;
    state.phase=state.resume_phase=ServicePhase::work;state.accepted_prefix=7;
    retain_job(state,job());check(state.job.member_dead);
    auto next=job(100,11);next.member_ordinal=4;retain_job(state,next);
    check(!state.job.member_dead&&state.job.member_id==11&&state.job.member_ordinal==4);
    check(state.job.active&&state.accepted_prefix==7&&state.phase==ServicePhase::work);
    state.job.member_dead=true;state.job.respawn_due_known=true;state.job.respawn_due_round=20;
    next=job(100,12);retain_job(state,next);
    check(!state.job.member_dead&&state.job.respawn_due_known&&state.job.respawn_due_round==20);
    retain_job(state,job(101,12));check(!state.job.member_dead&&!state.job.active&&!state.job.respawn_due_known);
    check(state.accepted_prefix==7);

    // Completion is an explicit observed event. The amount is a separate fact,
    // so lack of a new custody query cannot keep a completed episode pending.
    auto self=observed({},0,false);self.epoch={10,1,0};
    state=banking();auto signals=completed(self.epoch);
    auto update=observe_service(state,self,{},{},{},{},signals);
    check(update.bank_completed&&state.bank_completions==1&&state.bank_amounts_unknown==1);
    check(!state.bank_start_seen&&!state.bank_channel_seen&&!state.bank_obligation&&!state.bag_actual);
    check(state.banked_gems==0&&update.custody_unknown&&state.phase==ServicePhase::work);
    update=observe_service(state,self,{},{},{},{},signals);
    check(!update.bank_completed&&state.bank_completions==1&&state.bank_amounts_unknown==1);

    // A newly observed positive bag remains an obligation even though the old
    // bank episode closed. It need not decrease or become zero to close once.
    self=observed({11,1,0},10);state=banking();signals=completed(self.epoch);
    update=observe_service(state,self,{},{},{},{},signals);
    check(update.bank_completed&&state.bank_completions==1&&state.bank_amounts_unknown==1);
    check(state.bank_obligation&&state.bag_actual&&state.bag==10&&!state.bank_start_seen);
    check(state.phase==ServicePhase::bank&&state.banked_gems==0);
    update=observe_service(state,self,{},{},{},{},signals);
    check(!update.bank_completed&&state.bank_completions==1&&state.bank_obligation);

    // An exact caller amount is credited independently of the net bag change.
    self=observed({12,1,0},2);state=banking(7);signals=completed(self.epoch,true,5);
    update=observe_service(state,self,{},{},{},{},signals);
    check(update.bank_completed&&state.banked_gems==5&&state.bank_amounts_unknown==0);
    check(state.bank_obligation&&state.bag==2&&state.unknown_gems==0);
    self=observed({13,1,0},0);state=banking(10);signals=completed(self.epoch);
    update=observe_service(state,self,{},{},{},{},signals);
    check(update.bank_completed&&state.banked_gems==0&&state.bank_amounts_unknown==1);
    check(state.unknown_gems==10&&!state.bank_obligation);
    self=observed({14,1,0},0);state=banking(10);signals=completed(self.epoch,true,7);
    update=observe_service(state,self,{},{},{},{},signals);
    check(update.bank_completed&&state.banked_gems==7&&state.unknown_gems==3);

    // TEAM deposits and channel disappearance never certify our completion.
    self=observed({15,1,0},10);state=banking();state.team_submitted=20;state.team_submitted_actual=true;
    BankFact bank;bank.epoch=self.epoch;bank.submitted=120;bank.submitted_actual=true;
    update=observe_service(state,self,{},{},bank);
    check(!update.bank_completed&&state.bank_completions==0&&state.observed_team_submitted_increase==100);
    check(state.bag==10&&state.bank_obligation&&state.banked_gems==0);
    self=observed({16,1,0},0);signals=completed({15,1,0},true,10);state=banking();
    update=observe_service(state,self,{},{},{},{},signals);
    check(!update.bank_completed&&state.bank_completions==0&&state.banked_gems==0&&state.unknown_gems==10);
    self=observed({17,1,0},0);signals=completed(self.epoch,true,10);signals.bank_completion_actual=false;
    state=banking();update=observe_service(state,self,{},{},{},{},signals);
    check(!update.bank_completed&&state.banked_gems==0);

    // Contradicting custody observations do not invent a residual bag or an
    // amount, but cannot erase independently certified completion truth.
    self=observed({18,1,0},5);bank={};bank.epoch=self.epoch;bank.gems=7;bank.custody_actual=true;
    state=banking();signals=completed(self.epoch);
    update=observe_service(state,self,{},{},bank,{},signals);
    check(update.bank_completed&&update.custody_unknown&&!state.bag_actual&&state.banked_gems==0);
    check(state.bank_completions==1&&!state.bank_start_seen);

    // Death outranks a delayed accepted START. Preserve actual command history
    // and its serial, without resurrecting the cancelled physical channel.
    state={};state.resume_phase=ServicePhase::work;
    check(!ack_service(state,4,PrimitiveKind::bank,ServiceApplied::unknown));
    check(state.pending_ack&&state.unknown_receipts==1);
    self=observed({19,1,0},0);self.alive=false;self.hp=0;
    update=observe_service(state,self);check(state.was_dead&&state.phase==ServicePhase::recover);
    check(ack_service(state,4,PrimitiveKind::bank,ServiceApplied::yes));
    check(state.accepted_prefix==1&&state.accepted_banks==1&&state.last_ack_serial==4&&!state.pending_ack);
    check(state.phase==ServicePhase::recover&&!state.bank_start_seen&&!state.bank_obligation);
    check(!ack_service(state,4,PrimitiveKind::bank,ServiceApplied::yes));
    check(state.accepted_banks==1&&state.accepted_prefix==1);
    check(ack_service(state,5,PrimitiveKind::recall,ServiceApplied::yes));
    check(state.phase==ServicePhase::recover&&!state.recall_start_seen&&state.accepted_recalls==1);
    self=observed({20,1,0},0);update=observe_service(state,self);
    check(update.recovered&&!state.was_dead&&state.phase==ServicePhase::work);
    check(ack_service(state,6,PrimitiveKind::bank,ServiceApplied::yes));
    check(state.phase==ServicePhase::bank&&state.bank_start_seen&&state.accepted_banks==2);

    // Living recovery also cannot be overwritten merely by the ACK. A later
    // actual channel observation is different authority and can identify it.
    state={};state.phase=ServicePhase::recover;
    check(ack_service(state,1,PrimitiveKind::bank,ServiceApplied::yes));
    check(state.phase==ServicePhase::recover&&state.bank_start_seen);
    self=observed({21,1,0},4);self.hp=50;self.submitting=true;
    update=observe_service(state,self);check(state.phase==ServicePhase::bank);

    // Actual completion and subsequent observed death can coexist; neither
    // the scored amount nor the physical recovery state overwrites the other.
    state=banking(8);self=observed({22,1,0},0);self.alive=false;self.hp=0;
    update=observe_service(state,self,{},{},{},{},completed(self.epoch,true,8));
    check(update.bank_completed&&state.banked_gems==8&&state.bank_completions==1);
    check(state.phase==ServicePhase::recover&&state.was_dead&&!state.bank_start_seen);

    // Refusal/UNKNOWN reconciliation stays exact-once and cannot skip a pending
    // serial to consume a later command or mutate the accepted prefix.
    state={};state.phase=ServicePhase::work;
    check(!ack_service(state,3,PrimitiveKind::bank,ServiceApplied::unknown));
    check(!ack_service(state,4,PrimitiveKind::recall,ServiceApplied::yes));
    check(!ack_service(state,3,PrimitiveKind::bank,ServiceApplied::unknown));
    check(state.unknown_receipts==1&&state.pending_ack&&state.accepted_prefix==0);
    check(ack_service(state,3,PrimitiveKind::bank,ServiceApplied::no));
    check(state.false_receipts==1&&!state.pending_ack&&state.phase==ServicePhase::work&&!state.bank_start_seen);
    check(!ack_service(state,2,PrimitiveKind::bank,ServiceApplied::yes));
    check(state.accepted_banks==0&&state.accepted_prefix==0);

    // Respawn timing is group-scoped and does not disappear on a member change.
    state={};retain_job(state,job());self=observed({30,1,0});
    GroupFact group;group.epoch=self.epoch;group.id=100;group.life=GroupLife::respawning;
    group.actual=group.respawn_actual=true;group.respawn_time=5;
    observe_service(state,self,group);check(!state.job.active&&state.job.respawn_due_known&&state.job.respawn_due_round==35);
    retain_job(state,job(100,11));check(state.job.respawn_due_known&&state.job.respawn_due_round==35);
    check(!respawn_probe_due(state,{34,1,0})&&respawn_probe_due(state,{35,1,0}));
    const auto before=state.last_observation;self.epoch={29,1,0};
    check(!observe_service(state,self).fresh&&state.last_observation==before);
    std::printf("native service macro: %u checks passed\n",checks);
}
