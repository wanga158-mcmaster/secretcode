#pragma once
#ifndef P10_RUNTIME_RECEIPT_ENABLE
#define P10_RUNTIME_RECEIPT_ENABLE 0
#endif
#ifndef P10_RUNTIME_RECEIPT_FROM
#define P10_RUNTIME_RECEIPT_FROM 20
#endif
#ifndef P10_RUNTIME_RECEIPT_TO
#define P10_RUNTIME_RECEIPT_TO 40
#endif
#ifndef P10_RUNTIME_RECEIPT_ACTORS
#define P10_RUNTIME_RECEIPT_ACTORS 33
#endif
#if P10_RUNTIME_RECEIPT_ENABLE
#include "contract.h"
#include <bit>
#include <unistd.h>
namespace p10 {
// Measurement only. Fixed POD storage; no SDK queries, source ranking or I/O
// during the collection window. Zero presence bits mean unavailable, not false.
struct RuntimeReceipt {
 enum Field { version,round,actor,entry_fuel,reserve,exit_code,presence,
  world_round,type,x,y,hp_low,hp_high,gold,bag_low,bag_high,channel,capture,
  readiness,cooldown1,world_flags,world_fuel,runtime_flags,successor,
  policy_admission,policy_before,policy_checkpoint,policy_task,policy_tag,policy_altar,policy_group,policy_monster,policy_dx,policy_dy,
  final_task,final_tag,final_altar,final_group,final_monster,final_dx,final_dy,
  execute_floor,floor_fuel,handoff_result,handoff_fuel,before_execute,execute_calls,execute_guard,execute_guard_fuel,execute_returned,
  bank_status,requested_altar,selected_id,selected_x,selected_y,selected_ax,selected_ay,selected_travel,selected_flags,bank_distance,submit_range,bank_branch,selection_fuel,
  altar_total,altar_written,policy_reason_length,policy_reason_complete,final_reason_length,final_reason_complete,count };
 enum Present { world_present=1,self_complete=2,observation_end_fuel=4,policy_before_present=8,
  policy_checkpoint_present=16,policy_returned=32,final_directive=64,handoff_present=128,
  before_execute_present=256,bank_selected=512,execute_return_present=1024 };
 enum Exit { incomplete=0,pre_channel=1,pre_dead=2,prepare_unavailable=3,envelope_unavailable=4,
  frame_warmup=5,pre_observe_low=6,observe_round_changed=7,post_channel=8,post_dead=9,
  no_actions=10,actors_incomplete=11,hold_unknown=12,capture_before_policy=13,
  policy_round_changed=14,recovery_veto=15,execution_floor_veto=16,observation_veto=17,
  fallback_shop=18,capture_after_policy=19,handoff_veto=20,execution_complete=21,execution_round_changed=22 };
 struct Record {std::array<std::uint32_t,count> f;std::array<std::int32_t,40> altars;std::array<char,96> policy_reason,final_reason;};
 std::array<Record,24> records;int stored=0,overflow=0,attempts=0,emitted=0,cursor=0;
 Record* active=nullptr;
 static bool selected(int id){return id>=0&&id<16&&(std::uint32_t(P10_RUNTIME_RECEIPT_ACTORS)&(std::uint32_t{1}<<id));}
 static void put(Record&r,Field f,int value){r.f[f]=std::uint32_t(value);}
 void set(Field f,int value){if(active)put(*active,f,value);}
 void bit(Present value){if(active)active->f[presence]|=value;}
 void begin(int r,int id,int fuel,int floor){
  active=nullptr;if(!selected(id)||r<P10_RUNTIME_RECEIPT_FROM||r>P10_RUNTIME_RECEIPT_TO)return;
  if(stored==int(records.size())){++overflow;return;}
  active=&records[stored++];*active=Record{};
  set(version,1);set(round,r);set(actor,id);set(entry_fuel,fuel);set(reserve,floor);
  for(Field f:{world_fuel,policy_before,policy_checkpoint,floor_fuel,handoff_fuel,before_execute,execute_guard_fuel,selection_fuel,selected_id,requested_altar})set(f,-1);
 }
 void finish(Exit code){set(exit_code,code);active=nullptr;}
 void observe_fuel(int fuel){set(world_fuel,fuel);bit(observation_end_fuel);}
 void world(const World&w,int flags){if(!active)return;auto&r=*active;bit(world_present);if(w.actor_observation_complete)bit(self_complete);
  set(world_round,w.round);set(type,int(w.self.type));set(x,w.self.position.x);set(y,w.self.position.y);
  const auto hp=std::bit_cast<std::uint64_t>(w.self.hp),bag=std::uint64_t(w.self.bag);
  r.f[hp_low]=std::uint32_t(hp);r.f[hp_high]=std::uint32_t(hp>>32);r.f[bag_low]=std::uint32_t(bag);r.f[bag_high]=std::uint32_t(bag>>32);
  set(gold,w.own_gold);set(channel,int(w.self.channel));set(capture,w.capture_active);
  set(readiness,int(w.self.can_move)|(int(w.self.can_attack)<<1)|(int(w.self.can_cast)<<2));set(cooldown1,w.self.cooldown[1]);
  set(world_flags,int(w.safe_now)|(int(w.geometry_complete)<<1)|(int(w.observation_complete)<<2)|(int(w.actor_observation_complete)<<3));set(runtime_flags,flags);
  set(altar_total,int(w.altars.size()));const auto n=std::min<std::size_t>(4,w.altars.size());set(altar_written,int(n));
  for(std::size_t i=0;i<n;++i){const auto&a=w.altars[i];const std::array<int,10> row{a.id,a.position.x,a.position.y,a.approach.x,a.approach.y,int(a.route_known),a.travel,int(a.visible),int(a.enemy_lock),int(a.friendly_lock)};std::copy(row.begin(),row.end(),r.altars.begin()+i*10);}
 }
 void directive(const Directive&d,bool final){if(!active)return;auto&r=*active;const Field start=final?final_task:policy_task;
  const std::array<int,7> values{int(d.task),d.template_id,d.altar,d.group,d.monster,d.destination.x,d.destination.y};
  for(std::size_t i=0;i<values.size();++i)r.f[start+i]=std::uint32_t(values[i]);
  auto&text=final?r.final_reason:r.policy_reason;int length=0;if(d.reason)while(length<int(text.size())&&d.reason[length]){text[length]=d.reason[length];++length;}
  set(final?final_reason_length:policy_reason_length,length);
  set(final?final_reason_complete:policy_reason_complete,d.reason&&length<int(text.size()));bit(final?final_directive:policy_returned);
 }
 void bank(const Altar*best,int requested){set(bank_status,best?3:2);set(requested_altar,requested);if(!best)return;bit(bank_selected);
  set(selected_id,best->id);set(selected_x,best->position.x);set(selected_y,best->position.y);set(selected_ax,best->approach.x);set(selected_ay,best->approach.y);set(selected_travel,best->travel);
  set(selected_flags,int(best->route_known)|(int(best->visible)<<1)|(int(best->enemy_lock)<<2)|(int(best->friendly_lock)<<3));
 }
 // An intentional later I/O turn is diagnostic-exposed. ATTEMPTS are consumed
 // before write, including failures; no failed write can cause an endless retry.
 bool ready(int r,int id,int fuel)const{return selected(id)&&r>P10_RUNTIME_RECEIPT_TO&&fuel>=90000&&cursor<stored&&attempts<24;}
 bool emit(int r,int id,int fuel,int pending,int held,std::int64_t previous,int old_last_round){
  if(!ready(r,id,fuel))return false;
  const auto&record=records[cursor++];++attempts;
  std::array<char,2048> bytes;std::size_t n=0;for(char c:std::array<char,12>{{'p','1','0','_','r','u','n','t','i','m','e','1'}})bytes[n++]=c;
  const char digits[]="0123456789abcdef";auto word=[&](std::uint32_t value){bytes[n++]=' ';for(int shift=28;shift>=0;shift-=4)bytes[n++]=digits[(value>>shift)&15];};
  for(int value:{1,r,id,attempts,cursor-1,stored,overflow,fuel,pending,held,old_last_round})word(std::uint32_t(value));
  word(std::uint32_t(previous));word(std::uint32_t(std::uint64_t(previous)>>32));word(count);
  for(auto value:record.f)word(value);
  for(int i=0;i<int(record.f[altar_written])*10;++i)word(std::uint32_t(record.altars[i]));
  auto reason=[&](const auto&text,int length){for(int i=0;i<length;i+=4){std::uint32_t packed=0;for(int j=0;j<4&&i+j<length;++j)packed|=std::uint32_t(static_cast<unsigned char>(text[i+j]))<<(j*8);word(packed);}};
  reason(record.policy_reason,int(record.f[policy_reason_length]));reason(record.final_reason,int(record.f[final_reason_length]));
  bytes[n++]='\n';if(::write(1,bytes.data(),n)==static_cast<ssize_t>(n))++emitted;return true;
 }
 static_assert(12+9*(14+count+40+48)+1<=2048);
};
}
#endif
