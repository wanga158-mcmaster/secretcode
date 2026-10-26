#ifndef ACTOR_RUNTIME_HEADER
#error Runtime receipt fixture requires the generated production Runtime access facade
#endif
#include ACTOR_RUNTIME_HEADER
#include <cassert>
#include <cstring>
#include <iostream>
#include <type_traits>

extern "C" {
__attribute__((import_module("fixture"),import_name("reset"))) void fixture_reset(int);
__attribute__((import_module("fixture"),import_name("clock"))) void fixture_clock(int,int);
__attribute__((import_module("fixture"),import_name("write_mode"))) void fixture_write_mode(int);
__attribute__((import_module("fixture"),import_name("metric"))) int fixture_metric(int);
}
using namespace p10;
using RR=RuntimeReceipt;
static_assert(P10_RUNTIME_RECEIPT_FROM==0&&P10_RUNTIME_RECEIPT_TO==6&&P10_RUNTIME_RECEIPT_ACTORS==33);
static_assert(std::is_trivially_copyable_v<RR::Record>&&std::is_standard_layout_v<RR::Record>);
struct Policy {Directive step(const World&,Arm){return wait();}};
static int get(const RR::Record&r,RR::Field f){return std::int32_t(r.f[f]);}
static World frame(){World w;w.round=5;w.self.id=0;w.self.type=Champion::vendetta;w.self.position={1,1};w.self.bag=7;
 w.self.alive=w.self.visible=w.self.can_attack=w.self.can_cast=true;w.self.hp=123.5;return w;}
static Altar altar(int id,Point p,int travel,bool locked=false,bool visible=false){Altar a;a.id=id;a.position=p;a.approach={p.x-1,p.y};a.travel=travel;a.route_known=true;a.enemy_lock=locked;a.visible=visible;return a;}
static void collector(){fixture_reset(0);RR receipt;assert(RR::selected(0)&&RR::selected(5)&&!RR::selected(1)&&!RR::selected(16));
 receipt.begin(0,0,100000,41000);assert(receipt.active);receipt.finish(RR::pre_channel);
 const auto&unknown=receipt.records[0];assert(get(unknown,RR::presence)==0&&get(unknown,RR::exit_code)==RR::pre_channel);
 assert(get(unknown,RR::world_fuel)==-1&&get(unknown,RR::selected_id)==-1); // Zero bag bits have no observation authority.
 receipt.begin(1,5,98000,41000);auto w=frame();w.self.bag=(Gems{1}<<42)+9;w.own_gold=33;w.capture_active=true;w.self.cooldown[1]=3;
 for(int i=0;i<5;++i)w.altars.push_back(altar(i,{i+2,i+3},10-i));
 char reason[]="actual returned policy reason";Directive d;d.task=Task::bank;d.template_id=61;d.altar=2;d.reason=reason;
 receipt.world(w,5);receipt.observe_fuel(72000);receipt.directive(d,false);receipt.directive(d,true);receipt.finish(RR::execution_complete);
 const auto saved=receipt.records[1];reason[0]='X';w.self.position={7,7};w.self.bag=0;w.altars.clear();
 assert(std::memcmp(&saved,&receipt.records[1],sizeof(saved))==0);
 assert(get(saved,RR::bag_low)==9&&get(saved,RR::bag_high)==1024&&get(saved,RR::world_fuel)==72000);
 assert(get(saved,RR::altar_total)==5&&get(saved,RR::altar_written)==4&&saved.altars[30]==3);
 assert(saved.policy_reason[0]=='a'&&saved.final_reason[0]=='a'&&get(saved,RR::policy_reason_complete)==1);
 assert((saved.f[RR::presence]&(RR::world_present|RR::self_complete|RR::observation_end_fuel|RR::policy_returned|RR::final_directive))==(RR::world_present|RR::self_complete|RR::observation_end_fuel|RR::policy_returned|RR::final_directive));
 receipt.begin(2,0,100000,41000);char long_reason[100];std::fill(std::begin(long_reason),std::end(long_reason),'q');long_reason[99]=0;d.reason=long_reason;
 receipt.directive(d,false);receipt.finish(RR::pre_dead);assert(get(receipt.records[2],RR::policy_reason_length)==96&&get(receipt.records[2],RR::policy_reason_complete)==0);
 receipt.begin(7,0,100000,41000);assert(!receipt.active&&receipt.stored==3);receipt.begin(3,1,100000,41000);assert(!receipt.active&&receipt.stored==3);
 for(int i=3;i<26;++i){receipt.begin(i%7,0,100000,41000);receipt.finish(RR::pre_channel);}
 assert(receipt.stored==24&&receipt.overflow==2&&std::memcmp(&saved,&receipt.records[1],sizeof(saved))==0);
 for(int r=0;r<=6;++r)assert(!receipt.emit(r,0,100000,0,-1,0,-1));
 assert(!receipt.emit(7,0,89999,0,-1,0,-1)&&fixture_metric(0)==0&&receipt.attempts==0);
 fixture_clock(7,0);
 for(int i=0;i<24;++i){fixture_write_mode(i%3);assert(receipt.emit(7,0,100000,1,9,7,6));}
 assert(receipt.attempts==24&&receipt.cursor==24&&receipt.emitted==8&&fixture_metric(0)==24&&fixture_metric(1)==8);
 assert(!receipt.emit(8,0,100000,0,-1,0,7)&&fixture_metric(0)==24);
}
static void banks(){for(Arm arm:{Arm::active,Arm::control}){
 fixture_reset(0);Runtime<Policy> runtime(Profile{},arm);auto&w=runtime.frame_;w=frame();Directive d;d.task=Task::bank;
 auto call=[&](){runtime.runtime_receipt_.begin(5,0,100000,41000);runtime.bank_now(w,d);runtime.runtime_receipt_.finish(RR::execution_complete);return runtime.runtime_receipt_.records[runtime.runtime_receipt_.stored-1];};
 // An unlocked eligible altar wins over a closer locked altar; first equal travel wins.
 w.altars={altar(0,{2,1},0,true),altar(1,{1,2},8),altar(2,{2,2},8)};auto r=call();
 assert(get(r,RR::selected_id)==1&&get(r,RR::bank_distance)==1&&get(r,RR::submit_range)==1&&get(r,RR::bank_branch)==3);
 d.altar=0;r=call();assert(get(r,RR::requested_altar)==0&&get(r,RR::selected_id)==0&&(get(r,RR::selected_flags)&4)&&get(r,RR::bank_branch)==3);
 d.altar=-1;w.altars={altar(0,{2,1},4,true),altar(1,{1,2},1,true)};r=call();assert(get(r,RR::selected_id)==1); // Actual constrained locked fallback.
 d.altar=9;r=call();assert(get(r,RR::bank_status)==2&&!(r.f[RR::presence]&RR::bank_selected));
 d.altar=-1;w.altars[0].route_known=w.altars[1].route_known=false;r=call();assert(get(r,RR::bank_status)==2);
 w.self.bag=0;r=call();assert(get(r,RR::bank_status)==1);w.self.bag=7;
 w.altars={altar(0,{2,1},1,false,true)};fixture_reset(0);r=call();
 assert(get(r,RR::bank_branch)==5&&runtime.bank_pending_&&fixture_metric(2)==1); // Mock reports channel start, never completion.
 fixture_reset(1);r=call();assert(get(r,RR::bank_branch)==2&&fixture_metric(2)==1); // Accepted submit with no observed start.
 fixture_reset(2);r=call();assert(get(r,RR::bank_branch)==4&&fixture_metric(2)==0); // Stale owner round.
 fixture_reset(0);w.altars={altar(0,{2,1},1,true,true)};r=call();assert(get(r,RR::bank_branch)==1&&fixture_metric(3)==1);
 fixture_reset(0);w.altars={altar(0,{4,1},3)};runtime.nav_.banks.resize(1);r=call();assert(get(r,RR::bank_distance)==3&&get(r,RR::bank_branch)==6&&fixture_metric(2)==0);
 assert(fixture_metric(0)==0); // Collection never emits; the existing bank prediction is ordinary diagnostic output.
 }}
static void runtime_flush(){for(Arm arm:{Arm::active,Arm::control}){
 fixture_reset(0);Runtime<Policy> runtime(Profile{},arm);runtime.last_round_=5;runtime.hold_=17;runtime.bank_pending_=true;runtime.previous_bag_=7;runtime.previous_alive_=true;
 runtime.runtime_receipt_.begin(5,0,100000,41000);runtime.runtime_receipt_.finish(RR::pre_channel);
 fixture_clock(7,0);runtime.turn();assert(runtime.last_round_==7&&runtime.hold_==17&&runtime.bank_pending_&&runtime.previous_bag_==7&&runtime.previous_alive_);
 assert(runtime.runtime_receipt_.attempts==1&&runtime.runtime_receipt_.emitted==1&&fixture_metric(0)==1);
 assert(std::strcmp(runtime.stage(),"runtime-receipt-emission")==0);
 fixture_clock(8,1);runtime.turn();assert(runtime.last_round_==8&&runtime.hold_==17&&runtime.bank_pending_&&runtime.previous_bag_==7&&runtime.previous_alive_);
 assert(fixture_metric(0)==1&&fixture_metric(2)==0&&fixture_metric(3)==0);
 // A real collection-window Runtime exit has unknown World/bag, rather than stale frame data.
 fixture_clock(5,1);Runtime<Policy> channel(Profile{},arm);channel.frame_.self.bag=999;channel.turn();
 assert(channel.runtime_receipt_.stored==1&&get(channel.runtime_receipt_.records[0],RR::exit_code)==RR::pre_channel&&get(channel.runtime_receipt_.records[0],RR::presence)==0);
 }}
int main(){collector();banks();runtime_flush();std::cout<<"PASS production Runtime receipt: bounded immutable collection, unknown pre-World exits, 24 attempts including failed/partial writes, actual bank selection/range branches in both arms, post-window flush preserves local gap bookkeeping; post-flush observations are delayed, no engine or native ability claim\n";}
