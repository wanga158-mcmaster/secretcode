// Independent local-word controller derived from SDK mechanics and the foundation.
// Dated camp estimates nominate work; only current native facts permit primitives.
#include <cambc.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>
#include "config.h"
#include "local.h"
#include "word_search.h"
#include "work_memory.h"
#include "return_budget.h"
#include "melee_exit.h"
#include "return_route.h"
#include "funded_dispatch.h"
#include "funded_navigation.h"
#include "service_owner.h"
#include "return_policy.h"
#include "return_funding.h"
#include "joint_world.h"
#include "joint_catalogue.h"
#include "executable_policy.h"
#include "native_service_macro.h"
#include "native_service_execution.h"
#include "native_service_navigation.h"
#include "native_service_route_evidence.h"
#include "native_service_egress.h"
#include "native_service_route_owner.h"
#include "native_service_arrival.h"
#include "native_service_fields.h"
#include "native_service_cycle.h"
#include "native_service_exit_witness.h"
#include "native_service_stationary_index.h"
#include "native_service_group_frontier.h"
#include "support_field.h"
#include "dash_word/dash_lean.h"

namespace mosaic {
namespace {
using namespace cambc;
using Info=detail::champion_info;
vec2 point(const Info& q){return {q.x,q.y};}
int dist(vec2 a,vec2 b){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
champion_state state(const Info& q){return champion_state::from_bits_retain(std::uint32_t(q.state));}
Info body(){Info q{};detail::check(detail::sdk_champion(std::uint32_t(handle_champion_t(self())),&q));return q;}
int expected_epoch=-1;
bool fuel(std::uint64_t reserve){return fuel_remaining()>reserve&&int(round_number())==expected_epoch;}
struct Member {
    camp handle;int group;vec2 pos;float base_hp,base_strength;int range;
    float hp,shield,strength;int seen{-1};bool alive{true},awake{};
    work_memory::Member work;
};
struct GroupMemory {int state{},seen{-1},until{},ban_until{};std::size_t begin{},end{},probe_cursor{};};
struct Local {word::Target target;int member{-1};std::optional<champion> enemy;};
struct Runtime {
    struct FundedState {
        funded_navigation::Cache navigation;
        funded_dispatch::FailureFence failures;
        service_owner::Observation observation_owner{4};
        std::uint64_t observation_receipt{};
        std::uint64_t observation{},effect{},intent{};
        std::uint64_t prepared_frame{},prepared_effect{},prepared_observation{};
        std::uint64_t prepared_support_revision{},hold_support_revision{};
        std::uint64_t probe_frame{},probe_effect{},probe_observation{};
        bool active{},hold_admitted{},refine_basic{},probe_complete{};
        int probe_kind{},probe_slot{-1},probed_member{-1},probed_enemy{-1},prepared_round{-1},key_job{-1};
        std::optional<champion> enemy;
        vec2 probe_center{},hold_origin{};
        std::uint64_t hold_frame{},hold_effect{};
        funded_dispatch::Owner owner{funded_dispatch::Owner::approach};
        unsigned direction{};
        vec2 prepared_origin{},prepared_next{};
        return_route::Route prepared_route;
        bool prepared{};
        // A nomination includes fresh masks, complete success/refusal quotes
        // and the retained command token. Once paid, only the actual command
        // and failure refresh remain. Do not charge that completed prefix twice.
        static constexpr std::uint64_t nomination_floor=32000,dispatch_tail=16000;
    };
    static FundedState& funded_state(){static FundedState value;return value;}
    template<std::size_t N> static void emit_packet(const std::array<std::uint32_t,N>& packet) {
        // stdout may pass through a text transport. Carry the exact little-
        // endian WASM payload as ASCII hex with an explicit bounded frame and
        // checksum; no non-UTF8 preservation or printf conversion is assumed.
        static constexpr char hex[]="0123456789abcdef";
        std::array<char,N*8+14> frame{};std::size_t at=0;
        for(char c:std::array<char,4>{'M','X','9',':'})frame[at++]=c;
        const auto* bytes=reinterpret_cast<const unsigned char*>(packet.data());
        std::uint32_t checksum=2166136261u;
        for(std::size_t i=0;i<N*sizeof(packet[0]);++i) {
            const auto value=bytes[i];checksum=(checksum^value)*16777619u;
            frame[at++]=hex[value>>4];frame[at++]=hex[value&15];
        }
        frame[at++]=':';
        for(int shift=28;shift>=0;shift-=4)frame[at++]=hex[(checksum>>shift)&15];
        frame[at++]='\n';std::fwrite(frame.data(),1,at,stdout);std::fflush(stdout);
    }
    struct Diagnostic {
        std::array<unsigned,21> stages{};
        std::array<unsigned,11> prepare{};
        std::array<unsigned,9> offers{};
        std::array<unsigned,7> micro{};
        std::array<unsigned,12> walks{};
        int stage{},last_prepare{},last_walk{},work_count{},unseen{-1},return_reason{};
    };
    static Diagnostic& diagnostic(){static Diagnostic value;return value;}
    [[gnu::always_inline]] static void diag_stage(int stage){if constexpr(cfg::trace>1)diagnostic().stage=stage;}
    [[gnu::always_inline]] static void diag_prepare(int code){if constexpr(cfg::trace>1){++diagnostic().prepare[std::size_t(code)];diagnostic().last_prepare=code;}}
    [[gnu::always_inline]] static void diag_offer(int code){if constexpr(cfg::trace>1)++diagnostic().offers[std::size_t(code)];}
    [[gnu::always_inline]] static void diag_micro(int code){if constexpr(cfg::trace>1)++diagnostic().micro[std::size_t(code)];}
    [[gnu::always_inline]] static void diag_walk(int code){if constexpr(cfg::trace>1){++diagnostic().walks[std::size_t(code)];diagnostic().last_walk=code;}}
    void diagnostic_end() {
        if constexpr(cfg::trace>1) {
            if constexpr(cfg::executable_policy&&cfg::trace>=7){executable_diagnostic_end();return;}
            auto& d=diagnostic();++d.stages[std::size_t(d.stage)];
            if(epoch!=32&&epoch!=128&&epoch!=512&&epoch!=1024&&epoch!=1536&&epoch!=1999)return;
            // The full fixed-frame tail measured at most14,790 fuel on the
            // healthy R10 A trace;14K admitted four exhausted B emissions.
            // This caller reserves20K, then qualifies its own native health.
            if(!fuel(20000))return;
            const auto remaining=fuel_remaining();const auto me=body();const auto flags=state(me);
            const auto& route=planned_route();
            // Fixed binary evidence avoids sixty formatted integer conversions
            // consuming the action tail. The packet is emitted AFTER dispatch;
            // its magic/length and exact source profile identify its schema.
            std::array<std::uint32_t,80> packet{};
            packet[0]=0x3943444du;packet[1]=(1u<<16)|std::uint32_t(packet.size()*sizeof(packet[0]));
            packet[2]=std::uint32_t(epoch);packet[3]=std::uint32_t(handle_champion_t(self()));
            packet[4]=std::uint32_t(me.x);packet[5]=std::uint32_t(me.y);
            std::memcpy(&packet[6],&me.health,sizeof(me.health));packet[7]=flags.bits();
            packet[8]=std::uint32_t(job);packet[9]=std::uint32_t(idle);packet[10]=std::uint32_t(task);
            packet[11]=returning;packet[12]=recovering;packet[13]=std::uint32_t(d.work_count);
            packet[14]=std::uint32_t(d.unseen);packet[15]=std::uint32_t(local.size());packet[16]=std::uint32_t(route.size());
            packet[17]=std::uint32_t(d.last_prepare);packet[18]=std::uint32_t(d.last_walk);packet[19]=std::uint32_t(remaining);
            std::size_t at=20;
            for(auto n:d.stages)packet[at++]=n;
            for(auto n:d.prepare)packet[at++]=n;
            for(auto n:d.offers)packet[at++]=n;
            for(auto n:d.micro)packet[at++]=n;
            for(auto n:d.walks)packet[at++]=n;
            if constexpr(cfg::funded_dispatch) {
                const auto& f=funded_state();
                packet[52]=unsigned(f.navigation.status());
                packet[53]=unsigned(f.navigation.ready())|unsigned(f.refine_basic)<<1;
                packet[54]=std::uint32_t(f.navigation.roots_frame());
                packet[55]=std::uint32_t(f.navigation.field_frame());
                packet[56]=std::uint32_t(f.navigation.cursor().intent());
                packet[57]=f.navigation.cursor().remaining();
                packet[58]=std::uint32_t(observation_member);
                packet[59]=std::uint32_t(cast_wait_until);
                packet[60]=std::uint32_t(f.prepared_round);
                if(returning) {
                    const auto priced=exit_state(me,false);const auto quote=route.quote(priced);
                    const auto next_damage=exposure(vp(quote.next)),here_damage=exposure(point(me));
                    const auto terminal=route.valid()?exposure(vp(route.node(route.size()-1).point)):return_budget::Damage{0,false};
                    packet[52]=0x80000000u|unsigned(quote.reason)|unsigned(d.return_reason)<<8;
                    packet[53]=unsigned(quote.valid)|unsigned(quote.safe)<<1|unsigned(route.fresh())<<2|
                        unsigned(route.refusal_used())<<3|unsigned(route.pending_refusal_wait())<<4|
                        unsigned(next_damage.valid)<<5|unsigned(here_damage.valid)<<6|
                        unsigned(terminal.valid&&terminal.amount==0)<<7|
                        unsigned(priced.allow_one_refusal&&!route.refusal_used())<<8|
                        unsigned(quote.worst_loss>0xffffffffu)<<9;
                    packet[54]=std::uint32_t(route.frame());
                    packet[55]=unsigned(quote.steps)|unsigned(quote.max_phases)<<8|unsigned(route.size())<<16;
                    packet[56]=std::uint32_t(quote.worst_loss);
                    packet[57]=(std::uint32_t(quote.next.x)&0xffffu)|(std::uint32_t(quote.next.y)&0xffffu)<<16;
                    packet[58]=std::uint32_t(next_damage.amount);packet[59]=std::uint32_t(here_damage.amount);
                    packet[60]=std::uint32_t(terminal.amount);
                }
            }
            if constexpr(cfg::return_policy) {
                const auto& model=model_return_state();const auto& quote=model.last_quote;
                packet[20]=0x52503132u; // RP12 payload inside the fixed MDC9 transport.
                at=21;
                for(auto n:model.offered)packet[at++]=n;
                for(auto n:model.selected)packet[at++]=n;
                for(auto n:model.applied)packet[at++]=n;
                packet[39]=model.source_reads;packet[40]=model.query_cursor;
                packet[41]=std::uint32_t(model.last_reason);packet[42]=std::uint32_t(model.last_kind);
                packet[43]=unsigned(model.frame.source_count);packet[44]=unsigned(model.group_count);
                packet[45]=unsigned(model.candidate_count);packet[46]=unsigned(model.evaluated_count);
                packet[47]=model.model_risk;packet[48]=model.actual_false;
                packet[49]=model.actual_work;packet[50]=model.actual_moves;packet[51]=model.actual_recalls;
                packet[52]=unsigned(model.full_steps);packet[53]=unsigned(model.steps);
                packet[54]=unsigned(model.truncated)|unsigned(model.has_visible_enemy)<<1;
                packet[55]=std::uint32_t(model.owner_epoch);
                packet[56]=unsigned(quote.valid)|unsigned(quote.supported)<<1|unsigned(quote.upper_alive)<<2|
                    unsigned(quote.estimated_alive)<<3|unsigned(quote.upper_complete)<<4|
                    unsigned(quote.estimated_complete)<<5|unsigned(quote.assurance)<<8;
                packet[57]=unsigned(quote.reason);
                std::memcpy(&packet[58],&quote.upper_loss,sizeof(quote.upper_loss));
                std::memcpy(&packet[60],&quote.estimated_loss,sizeof(quote.estimated_loss));
                packet[62]=quote.max_phases;packet[63]=quote.owner_waits;
                packet[64]=unsigned(model.frame.frame);packet[65]=unsigned(model.frame.observation);
                packet[66]=unsigned(model.support_revision);
                packet[67]=unsigned(quote.frame);packet[68]=unsigned(quote.observation);
                packet[69]=quote.successful_primitives;packet[70]=quote.requires_post_effect_refresh;
                packet[71]=std::uint32_t(model.funding_reason);packet[72]=model.funding_overrun;
                packet[73]=unsigned(model.effect);packet[74]=unsigned(model.word_revision);
                packet[75]=std::uint32_t(model.last_member);
                for(std::size_t n=76;n<packet.size();++n)packet[n]=0;
                if constexpr(cfg::trace>=3) {
                    const auto& first=model_resource_failure();
                    packet[20]=0x52504633u; // RPF3: first actual phase failure, never overwritten.
                    for(std::size_t n=21;n<packet.size();++n)packet[n]=0;
                    packet[21]=first.present;packet[22]=unsigned(first.phase);packet[23]=unsigned(first.reason);
                    packet[24]=unsigned(first.before);packet[25]=unsigned(first.after);packet[26]=unsigned(first.bound);
                    packet[27]=unsigned(first.start.revision);packet[28]=unsigned(first.start.frame);
                    packet[29]=unsigned(first.start.effect);packet[30]=unsigned(first.start.observation);
                    packet[31]=unsigned(first.actual.revision);packet[32]=unsigned(first.actual.frame);
                    packet[33]=unsigned(first.actual.effect);packet[34]=unsigned(first.actual.observation);
                    packet[35]=model.funding_overrun;packet[36]=model.actual_work;
                    packet[37]=model.actual_moves;packet[38]=model.actual_recalls;
                    packet[39]=std::uint32_t(model.last_kind);packet[40]=std::uint32_t(model.last_reason);
                    packet[41]=std::uint32_t(model.owner_epoch);packet[42]=unsigned(model.frame.observation);
                    packet[43]=unsigned((first.start.revision|first.start.frame|first.start.effect|first.start.observation|
                        first.actual.revision|first.actual.frame|first.actual.effect|first.actual.observation)>0xffffffffu);
                }
            }
            if constexpr(cfg::joint_policy)joint_diagnostic_packet(packet);
            emit_packet(packet);
        }
    }
    std::vector<map::camp_group> groups;
    std::vector<GroupMemory> memory;
    std::vector<Member> members;
    std::vector<Local> local;
    std::vector<std::size_t> threats;
    std::array<std::optional<camp_stats>,6> designs;
    std::optional<bitboard> walls,home,quiet,camp_danger,dash_walls;
    std::optional<dash_word::lean::Scratch> dash_scratch;
    int dash_slot{-1},spell_reach{};
    int cast_wait_until{-1};
    std::vector<vec2> altars;
    std::vector<std::uint8_t> visible,static_bits;
    // Complete potential CAMP support, independent of the eight target hot set.
    // This first class keeps all camps, even currently dead/asleep, and ignores
    // Jungle reduction. It is an upper bound, not a prediction of actual hits.
    std::vector<std::uint32_t> support,support_count;
    support_field::Field support_detail;
    std::size_t setup_support{};
    bool support_started{},support_coarse{};
    int exit_priced_epoch{-1};
    std::uint64_t exit_support_revision{};
    float exit_priced_multiplier{1};
    return_budget::Trace exit_trace;
    bool returning{};
    unsigned return_refusals{};
    std::size_t setup_group{},setup_member{};
    bool loaded{},ready{},walked{},attacked{},recovering{};
    int epoch{},job{-1},last_bag{},idle{},last_group_refresh{-1},last_action_round{-1},path_slack{8},observation_member{-1};
    float camp_multiplier{1},camp_taken{1};
    passive_type passive{};
    vec2 previous{};
    enum class Task {work,bank,recover};Task task{Task::work};

    static std::vector<vec2>& executable_home_cache() {
        static constinit std::vector<vec2> value{};return value;
    }
    bool executable_cached_home(executable_policy::Point position)const {
        for(const auto candidate:executable_home_cache())
            if(position.x==candidate.x&&position.y==candidate.y)return true;
        return false;
    }

    bool setup() {
        if(ready)return true;
        if(!loaded) {
            if(!fuel(85000))return false;
            groups=map::camp_groups();memory.resize(groups.size());
            dash_walls=map::walls()|map::altars()|map::spawn_region(team::enemy);
            walls=*dash_walls|map::holes();
            home=map::spawn_region(team::ally);altars=map::altars().positions();
            if constexpr(cfg::executable_policy||cfg::native_service)executable_home_cache()=home->positions();
            const auto design=self().base_stats();camp_multiplier=design.camp_damage_multiplier;
            passive=self().passive();local.reserve(16);members.reserve(256);threats.reserve(256);
            const auto abilities=self().abilities();
            for(std::size_t slot=0;slot<abilities.size();++slot)if(abilities[slot].info())
                std::visit([&](const auto& info) {
                    using T=std::decay_t<decltype(info)>;
                    if constexpr(std::is_same_v<T,ability_damage>||std::is_same_v<T,ability_cc>)
                        spell_reach=std::max(spell_reach,int(std::floor(info.range)));
                    else if constexpr(std::is_same_v<T,ability_aoe_damage>)
                        spell_reach=std::max(spell_reach,int(std::floor(info.range))+int(std::floor(info.radius)));
                    else if constexpr(std::is_same_v<T,ability_dash>)
                        if(info.modifier==0&&info.duration==0&&info.valid_targets.contains(ability_target_mask::ground))
                            dash_slot=int(slot);
                },*abilities[slot].info());
            if(dash_slot>=0)dash_scratch.emplace();
            visible.resize(bitboard::linear_buffer_size());static_bits=walls->export_linear_buffer();
            support.resize(std::size_t(walls->width())*std::size_t(walls->height()));
            support_count.resize(support.size());camp_danger=bitboard{};loaded=true;
        }
        while(setup_group<groups.size()&&fuel(45000)) {
            const auto group=setup_group;const auto camps=groups[group].camps();
            if(setup_member==0)memory[group].begin=members.size();
            while(setup_member<camps.size()&&fuel(45000)) {
                const auto& c=camps[setup_member++];const auto kind=std::size_t(c.kind());
                if(!designs[kind])designs[kind]=c.base_stats();
                const auto d=*designs[kind];members.push_back({c,int(group),c.spawn_position(),
                    d.base_health,d.base_strength,d.base_attack_range,d.base_health,0,d.base_strength});
                const auto& m=members.back();
                if constexpr(cfg::native_service)native_stationary_store({m.pos.x,m.pos.y},int(members.size()-1));
                const auto hit=std::uint32_t(std::ceil(std::max(0.0f,d.base_strength)));
                if(hit)camp_danger->draw_chebyshev(m.pos,m.range);
                for(int y=std::max(0,m.pos.y-m.range);y<=std::min(int(walls->height())-1,m.pos.y+m.range);++y)
                    for(int x=std::max(0,m.pos.x-m.range);x<=std::min(int(walls->width())-1,m.pos.x+m.range);++x) {
                        const auto index=std::size_t(y)*walls->width()+std::size_t(x);
                        support[index]+=hit;support_count[index]+=hit>0;
                    }
            }
            if(setup_member<camps.size())return false;
            memory[group].end=members.size();++setup_group;setup_member=0;
        }
        if(setup_group<groups.size())return false;
        if(!quiet)quiet=(~*camp_danger)&~*walls;
        if constexpr(cfg::native_service){ready=true;return false;}
        if(!support_started) {
            if(!fuel(85000))return false;
            std::size_t capacity=0;
            for(const auto& m:members)if(m.base_strength>0) {
                const auto w=std::min(int(walls->width())-1,m.pos.x+m.range)-std::max(0,m.pos.x-m.range)+1;
                const auto h=std::min(int(walls->height())-1,m.pos.y+m.range)-std::max(0,m.pos.y-m.range)+1;
                capacity+=std::size_t(w)*std::size_t(h);
            }
            support_coarse=!support_detail.reset(walls->width(),walls->height(),groups.size(),capacity).applied();
            support_started=true;
        }
        while(!support_coarse&&setup_support<members.size()&&fuel(65000)) {
            const auto& m=members[setup_support];
            const auto result=support_detail.addCamp(std::size_t(m.group),rp(m.pos),m.range,
                std::uint64_t(std::ceil(std::max(0.0f,m.base_strength))),4096);
            // A high-density unsupported setup falls back to COMPLETE static
            // support; no missing entries may appear as free/quiet space.
            if(!result.applied()){support_coarse=true;break;}
            ++setup_support;
        }
        if(!support_coarse&&setup_support<members.size())return false;
        if(!support_coarse)support_coarse=!support_detail.seal(members.size()).applied();
        if constexpr(cfg::indexed_catalogue)if(!joint_catalogue_prepare())return false;
        ready=true;return false;
    }
    static return_budget::Point rp(vec2 p){return {p.x,p.y};}
    static vec2 vp(return_budget::Point p){return {p.x,p.y};}
    return_budget::Damage exposure(vec2 p)const {
        if(p.x<0||p.y<0||p.x>=walls->width()||p.y>=walls->height())return {0,false};
        const auto index=std::size_t(p.y)*walls->width()+std::size_t(p.x);
        auto raw=return_budget::Damage{support[index],true};
        if(support_detail.ready())raw=support_detail.damageAt(rp(p),epoch,36,16).damage();
        if(!raw.valid||raw.amount==0||camp_taken>=1)return raw;
        // Aggregate scaling plus one per POTENTIAL hit bounds the sum of
        // individually rounded native hits; omitted groups only overcount N.
        const double scaled=std::max(double(raw.amount)*camp_taken,double(float(raw.amount)*camp_taken));
        return {std::uint64_t(std::ceil(scaled))+support_count[index],true};
    }
    void align_exit(const Info& me) {
        const auto p=point(me);const auto d=exposure(p);
        if(!exit_trace.at(rp(p)))exit_trace.clear();
        const auto revision=support_detail.revision();
        if(exit_trace.valid()&&(exit_priced_epoch!=epoch||exit_support_revision!=revision||exit_priced_multiplier!=camp_taken)) {
            std::array<return_budget::Damage,return_budget::Trace::capacity> prices{};
            for(std::size_t i=0;i<exit_trace.size();++i)prices[i]=exposure(vp(exit_trace.point(i)));
            exit_trace.refresh_damage({prices.data(),exit_trace.size()});
        }
        exit_priced_epoch=epoch;exit_support_revision=revision;exit_priced_multiplier=camp_taken;
        if(d.valid&&d.amount==0){exit_trace.reset_quiet(rp(p),d);return_refusals=0;}
    }
    return_budget::State exit_state(const Info& me,bool hold) {
        return_budget::State out;out.health=me.health;
        out.can_move_now=state(me).contains(champion_state::can_move);
        // Conservative initial work class: intend to remain for ONE camp
        // phase, even for a melee word that could instead move immediately.
        // An already spent M is that same one wait, never charged twice.
        out.additional_forced_waits=hold&&out.can_move_now?1:0;
        out.allow_one_refusal=return_refusals==0;
        if(exposure(point(me)).amount>0&&fuel(14000)) {
            const auto buffs=self().buffs();
            if(buffs.has_shield()) {
                const auto shield=buffs.get<buff_shield>();out.shield=shield.value;
                out.shield_phases=std::max(0,shield.duration);
            }
        }
        return out;
    }
    bool work_affordable(const Info& me) {
        if constexpr(cfg::route_exit) {
            if(!prepare_route(me,true))return false;
            return planned_route().quote(exit_state(me,true)).safe;
        }
        align_exit(me);
        if(!exit_trace.valid())return false;
        return exit_trace.quote(exit_state(me,true)).safe;
    }
    bool try_return_shield() {
        if(!fuel(18000)||!self().can_cast())return false;
        for(const auto& ability:self().abilities())if(ability.is_shield()&&ability.is_ready()) {
            if(ability.use(self())){last_action_round=epoch;return true;}
            return false;
        }
        return false;
    }
    bool return_step(const Info& original) {
        if constexpr(cfg::joint_policy)return joint_return_step(original);
        if constexpr(cfg::return_policy)return modeled_return_step(original);
        if constexpr(cfg::route_exit)return planned_return_step(original);
        auto me=original;align_exit(me);
        auto quote=exit_trace.quote(exit_state(me,false));
        if((!quote.valid||!quote.safe)&&try_return_shield()) {
            me=body();quote=exit_trace.quote(exit_state(me,false));
        }
        if(!state(me).contains(champion_state::can_move)||!fuel(14000))return false;
        if(quote.valid&&quote.steps>0) {
            const auto next=vp(quote.first_reverse_endpoint);
            auto blocked=*walls|vision::champion_tiles()|vision::camp_tiles()|vision::teleport_tiles();
            blocked.set(point(me),false);
            const auto next_batch=exposure(next);
            const bool emergency_prefix=next_batch.valid&&next_batch.amount<=exposure(point(me)).amount&&
                double(next_batch.amount)<me.health;
            if(!blocked.is_set(next)&&(quote.safe||emergency_prefix)) {
                const bool applied=self().move_absolute(next);
                if(applied) {
                    exit_trace.observe_step(rp(point(me)),rp(next),exposure(next),true);
                    walked=true;last_action_round=epoch;return true;
                }
                ++return_refusals;return false;
            }
            ++return_refusals;
        }
        // A changed/blocked suffix has a finite observed reroute; its risk is
        // reported as a fallback, not silently called the certified old route.
        // Preserve the incumbent if the composed scan+field+dispatch cannot fit.
        if(!fuel(45000))return false;
        // Cached potential quiet space avoids a 289-cell scan that consumed
        // the physical movement tail in the old fallback composition.
        bitboard goals;
        if(exit_trace.valid()&&exposure(vp(exit_trace.point(0))).amount==0)
            goals.set(vp(exit_trace.point(0)));
        else goals=*quiet;
        return walk(std::move(goals),true,true);
    }
    bool leave_before_work(const Info& me) {
        if(work_affordable(me))return false;
        if constexpr(cfg::melee_exit)if(try_basic_exit(me))return true;
        returning=true;return_step(me);return true;
    }
    bool try_basic_exit(const Info& me) {
        if constexpr(cfg::route_exit)return planned_basic_exit(me);
        // This class never licenses a stationary wait or another optional
        // primitive. Melee Basic must preserve actual M, then exit immediately.
        if(job<0||!fuel(36000)||me.attack_range!=1||
           !state(me).contains(champion_state::can_move)||
           !state(me).contains(champion_state::can_attack))return false;
        align_exit(me);
        const auto priced=exit_state(me,false);
        const auto quote=exit_trace.quote(priced);
        if(!quote.valid||!quote.safe||quote.steps==0)return false;
        const auto next=vp(quote.first_reverse_endpoint);
        auto blocked=*walls|vision::champion_tiles()|vision::camp_tiles()|vision::teleport_tiles();
        blocked.set(point(me),false);
        if(blocked.is_set(next))return false;
        const auto sight=vision::visible_tiles();
        int target=-1;float value=-1;
        const auto& group=memory[std::size_t(job)];
        for(std::size_t i=group.begin;i<group.end&&fuel(26000);++i) {
            const auto& m=members[i];
            if(dist(point(me),m.pos)>me.attack_range||!sight.is_set(m.pos)||!m.handle.state().is_alive())continue;
            const float useful=std::min(m.handle.health(),me.strength*camp_multiplier);
            if(useful>value){value=useful;target=int(i);}
        }
        const auto admitted=melee_exit::admit(exit_trace,priced,
            {state(me).contains(champion_state::can_attack),target>=0,true,fuel(24000),me.attack_range});
        if(!admitted.valid||!admitted.safe)return false;
        if constexpr(cfg::trace) {
            const auto held_before=exit_trace.quote(exit_state(me,true));
            self().attack(members[std::size_t(target)].handle);
            attacked=true;last_action_round=epoch;returning=true;
            const auto post=body();const bool applied=return_step(post);const auto actual=body();
            static unsigned emitted=0;
            if(emitted<2&&fuel(6000)) {
                ++emitted;
                std::printf("mosaic-word round=%d actor=%u from=%d,%d target=%d next=%d,%d hp=%.1f hold=%llu immediate=%llu postM=%d moved=%d at=%d,%d\n",
                    epoch,std::uint32_t(handle_champion_t(self())),me.x,me.y,target,next.x,next.y,double(me.health),
                    static_cast<unsigned long long>(held_before.worst_loss),
                    static_cast<unsigned long long>(admitted.worst_loss),
                    int(state(post).contains(champion_state::can_move)),int(applied),actual.x,actual.y);
            }
        } else {
            self().attack(members[std::size_t(target)].handle);
            attacked=true;last_action_round=epoch;returning=true;
            return_step(body());
        }
        return true;
    }
    static return_route::Route& planned_route() {
        // Constructed only in the enabled route class; the OFF runtime's
        // storage and initialization remain unchanged.
        static return_route::Route route;return route;
    }
    static return_route::Route& projected_route() {
        static return_route::Route route;return route;
    }
    bool prepare_route(const Info& me,bool hold=false) {
        auto& route=planned_route();const auto origin=point(me);
        const auto current=exposure(origin);
        if(current.valid&&current.amount==0) {
            diag_prepare(1);
            return route.reset_at_quiet(rp(origin),current,std::uint64_t(epoch));
        }
        if(!fuel(26000)){diag_prepare(4);return false;}
        const auto intended=exit_state(me,hold);
        auto blocked=*walls|vision::champion_tiles()|vision::camp_tiles()|vision::teleport_tiles();
        blocked.set(origin,false);
        bool retained=false;
        if(route.valid()&&!route.at(rp(origin)))route.clear();
        if(route.valid()) {
            std::array<return_budget::Damage,return_route::Route::capacity> damage{};
            std::array<bool,return_route::Route::capacity> open{};
            const auto count=route.size();
            for(std::size_t i=0;i<count;++i) {
                const auto p=vp(route.node(i).point);damage[i]=exposure(p);open[i]=!blocked.is_set(p);
            }
            if(route.refresh_damage({damage.data(),count},{open.data(),count},std::uint64_t(epoch))==return_route::Reason::ok) {
                retained=true;
                if(route.quote(intended).safe){diag_prepare(2);return true;}
                // Geometric validity is not an executable service. Retain the
                // incumbent while nominating a newly observed affordable exit.
                diag_prepare(0);
            }
        }
        // All unoccupied quiet endpoints are roots. An occupied old anchor
        // cannot remove the rest of this operator's legal terminal set.
        auto goals=*quiet&~blocked;
        if(goals.is_empty()){diag_prepare(5);return retained;}
        const auto cells=std::uint64_t(walls->width())*std::uint64_t(walls->height());
        if(!fuel(24000+cells*3)){diag_prepare(6);return retained;}
        bitboard destination;destination.set(origin);bitboard visited;
        distance_field field;field.compute_to(blocked,goals,destination,visited,31);
        if(!fuel(22000)){diag_prepare(7);return retained;}
        auto distance=field.get(origin);
        if(distance==distance_field::unreachable||distance>=return_route::Route::capacity){diag_prepare(8);return retained;}
        std::array<return_route::Node,return_route::Route::capacity> nodes{};
        std::size_t count=0;auto cursor=origin;
        nodes[count++]={rp(cursor),current,true};
        while(distance>0&&count<nodes.size()&&fuel(22000)) {
            const auto step=field.next_move_from(cursor);if(!step){diag_prepare(9);return retained;}
            const auto next=cursor+*step;
            if(blocked.is_set(next)||field.get(next)+1!=distance){diag_prepare(9);return retained;}
            nodes[count++]={rp(next),exposure(next),true};cursor=next;--distance;
        }
        if(distance!=0||!fuel(18000)){diag_prepare(9);return retained;}
        auto candidate=route;
        const bool accepted=candidate.assign({nodes.data(),count},std::uint64_t(epoch))==return_route::Reason::ok;
        if(accepted)return_route::consider(route,candidate,intended,36);
        diag_prepare(accepted?3:10);return route.valid();
    }
    bool project_route(const Info& me,vec2 next,return_route::Route& projected) {
        auto& route=planned_route();
        if(!route.valid()||!route.at(rp(point(me))))return false;
        std::array<return_route::Node,return_route::Route::capacity> nodes{};
        std::size_t start=route.size();
        for(std::size_t i=0;i<route.size();++i)if(route.node(i).point==rp(next)){start=i;break;}
        std::size_t count=0;
        if(start==route.size()) {
            if(route.size()+1>nodes.size())return false;
            nodes[count++]={rp(next),exposure(next),true};start=0;
        }
        for(std::size_t i=start;i<route.size();++i)nodes[count++]=route.node(i);
        projected=route;
        return projected.assign({nodes.data(),count},std::uint64_t(epoch))==return_route::Reason::ok;
    }
    bool planned_return_step(const Info& original,bool reuse_same_input_geometry=false) {
        auto me=original;
        if constexpr(cfg::funded_dispatch&&cfg::trace>1)diagnostic().return_reason=0;
        // Within this actor's input Basic can remove a body but cannot add a
        // blocker or advance another actor. Reuse only that already validated
        // geometry, with actual post-Basic body/M and a fresh state quote.
        bool prepared=reuse_same_input_geometry&&planned_route().fresh()&&
            planned_route().frame()==std::uint64_t(epoch)&&planned_route().at(rp(point(me)));
        if(!prepared)prepared=prepare_route(me);
        auto quote=planned_route().quote(exit_state(me,false));
        if((!prepared||!quote.safe)&&try_return_shield()) {
            me=body();prepared=prepare_route(me);quote=planned_route().quote(exit_state(me,false));
        }
        if(!prepared||!quote.valid||!quote.steps||!state(me).contains(champion_state::can_move)||!fuel(14000)){
            if constexpr(cfg::funded_dispatch&&cfg::trace>1)diagnostic().return_reason=
                !prepared?1:!quote.valid?2:!quote.steps?3:!state(me).contains(champion_state::can_move)?4:5;
            return false;
        }
        const auto next=vp(quote.next);const auto next_batch=exposure(next);const auto here=exposure(point(me));
        const bool emergency=next_batch.valid&&here.valid&&next_batch.amount<=here.amount&&double(next_batch.amount)<me.health;
        if(!quote.safe&&!emergency){
            if constexpr(cfg::funded_dispatch&&cfg::trace>1)diagnostic().return_reason=6;
            return false;
        }
        // Geometry was freshly validated above. A hidden occupancy refusal
        // still preserves actual M; it consumes this episode's finite reserve.
        const bool applied=self().move_absolute(next);
        planned_route().observe_step(rp(point(me)),rp(next),applied);
        if(applied) {
            if constexpr(cfg::funded_dispatch&&cfg::trace>1)diagnostic().return_reason=8;
            if(exit_trace.at(rp(point(me))))exit_trace.observe_step(rp(point(me)),rp(next),next_batch,true);
            else if(next_batch.amount==0)exit_trace.reset_quiet(rp(next),next_batch);
            walked=true;last_action_round=epoch;return true;
        }
        if constexpr(cfg::funded_dispatch&&cfg::trace>1)diagnostic().return_reason=7;
        ++return_refusals;return false;
    }
    bool planned_basic_exit(const Info& me) {
        diag_offer(0);
        if(job<0||!fuel(36000)||me.attack_range!=1||
           !state(me).contains(champion_state::can_move)||!state(me).contains(champion_state::can_attack)){diag_offer(1);return false;}
        if(!prepare_route(me)){diag_offer(2);return false;}
        const auto priced=exit_state(me,false);const auto quote=planned_route().quote(priced);
        const auto held=planned_route().quote(exit_state(me,true));
        if(!quote.valid||!quote.safe||!quote.steps){diag_offer(3);return false;}
        if(held.safe){diag_offer(4);return false;}
        const auto sight=vision::visible_tiles();int target=-1;float value=-1;
        const auto& group=memory[std::size_t(job)];
        for(std::size_t i=group.begin;i<group.end&&fuel(26000);++i) {
            const auto& m=members[i];
            if(dist(point(me),m.pos)>1||!sight.is_set(m.pos)||!m.handle.state().is_alive())continue;
            const float useful=std::min(m.handle.health(),me.strength*camp_multiplier);
            if(useful>value){value=useful;target=int(i);}
        }
        if(target<0){diag_offer(5);return false;}
        if(!fuel(24000)){diag_offer(6);return false;}
        diag_offer(7);
        self().attack(members[std::size_t(target)].handle);attacked=true;last_action_round=epoch;returning=true;
        const auto post=body();const bool applied=planned_return_step(post,true);
        if(applied)diag_offer(8);
        if constexpr(cfg::trace) {
            static unsigned emitted=0;
            if(emitted<2&&fuel(6000)) {
                ++emitted;const auto actual=body();
                if constexpr(cfg::trace>1) {
                    std::array<std::uint32_t,20> packet{};packet[0]=0x39574d44u;packet[1]=(1u<<16)|80u;
                    packet[2]=std::uint32_t(epoch);packet[3]=std::uint32_t(handle_champion_t(self()));
                    packet[4]=std::uint32_t(me.x);packet[5]=std::uint32_t(me.y);packet[6]=std::uint32_t(target);
                    packet[7]=std::uint32_t(quote.next.x);packet[8]=std::uint32_t(quote.next.y);
                    std::memcpy(&packet[9],&me.health,sizeof(me.health));
                    packet[10]=std::uint32_t(held.worst_loss);packet[11]=std::uint32_t(held.worst_loss>>32);
                    packet[12]=std::uint32_t(quote.worst_loss);packet[13]=std::uint32_t(quote.worst_loss>>32);
                    packet[14]=state(post).contains(champion_state::can_move);packet[15]=applied;
                    packet[16]=std::uint32_t(actual.x);packet[17]=std::uint32_t(actual.y);
                    packet[18]=std::uint32_t(quote.steps);packet[19]=std::uint32_t(fuel_remaining());
                    emit_packet(packet);
                } else {
                std::printf("mosaic-route-word round=%d actor=%u from=%d,%d target=%d next=%d,%d hp=%.1f hold=%llu immediate=%llu postM=%d moved=%d at=%d,%d\n",
                    epoch,std::uint32_t(handle_champion_t(self())),me.x,me.y,target,quote.next.x,quote.next.y,double(me.health),
                    static_cast<unsigned long long>(held.worst_loss),static_cast<unsigned long long>(quote.worst_loss),
                    int(state(post).contains(champion_state::can_move)),int(applied),actual.x,actual.y);
                }
            }
        }
        return true;
    }
    bool sees(vec2 p)const {
        if(p.x<0||p.y<0||p.x>=walls->width()||p.y>=walls->height())return false;
        const auto n=std::size_t(p.y)*std::size_t(walls->width())+std::size_t(p.x);
        return visible[n/8]&(std::uint8_t(1u)<<(n%8));
    }
    bool static_blocked(vec2 p)const {
        if(p.x<0||p.y<0||p.x>=walls->width()||p.y>=walls->height())return true;
        const auto n=std::size_t(p.y)*std::size_t(walls->width())+std::size_t(p.x);
        return static_bits[n/8]&(std::uint8_t(1u)<<(n%8));
    }
    void refresh_group(int index) {
        if(index<0||std::size_t(index)>=groups.size()||!fuel(23000))return;
        bool known=false;
        for(const auto& c:groups[std::size_t(index)].camps())known=known||sees(c.spawn_position());
        if(!known)return;
        auto& m=memory[std::size_t(index)];std::uint32_t status{};
        detail::check(detail::sdk_camp_group_state(std::uint32_t(handle_camp_group_t(groups[std::size_t(index)])),&status));
        if(status==0)return;
        if(status==1&&(m.state==2||m.state==3)&&epoch>m.seen)
            for(std::size_t i=m.begin;i<m.end;++i)members[i].work.new_generation(epoch);
        m.state=int(status);m.seen=epoch;
        if(status==3) {
            std::int32_t timer{};detail::check(detail::sdk_camp_group_cooldown(
                std::uint32_t(handle_camp_group_t(groups[std::size_t(index)])),&timer));
            m.until=epoch+timer;
        } else if(status==2)m.until=epoch+std::max(1,groups[std::size_t(index)].max_respawn_timer()-1);
        else m.until=0;
        if(support_detail.ready()) {
            if(status==1)support_detail.restore(std::size_t(index),epoch);
            else support_detail.absence(std::size_t(index),epoch,std::max(epoch,m.until-1));
        }
    }
    bool absent(const Member& m)const {
        const auto& g=memory[std::size_t(m.group)];
        if((g.state==2||g.state==3)&&epoch+1<g.until)return true;
        return !m.alive&&m.seen==epoch&&g.seen==epoch&&g.state==1;
    }
    void read_member(std::size_t i,const Info& me) {
        auto& m=members[i];
        const auto live=m.handle.state();m.seen=epoch;m.alive=live.is_alive();m.awake=live.is_awake();
        m.hp=0;m.shield=0;
        if(m.alive) {
            if(support_detail.ready())support_detail.restore(std::size_t(m.group),epoch);
            m.work.observe_alive(epoch);
            detail::camp_state q{};detail::check(detail::sdk_camp(std::uint32_t(handle_camp_t(m.handle)),&q));
            m.hp=q.health;m.strength=q.strength;
            if(dist(point(me),m.pos)<=5&&local.size()<8) {
                const auto buffs=m.handle.buffs();
                if(buffs.has_shield())m.shield=buffs.get<buff_shield>().value;
            }
        } else {
            int probe_after=epoch+1;const auto& g=memory[std::size_t(m.group)];
            if(g.seen==epoch) {
                if(g.state==1||g.state==2)probe_after=epoch+std::max(1,groups[std::size_t(m.group)].max_respawn_timer()-1);
                else if(g.state==3)probe_after=std::max(epoch+1,g.until-1);
            }
            m.work.observe_dead(epoch,probe_after);
        }
        if(m.alive&&local.size()<8) {
            word::Target t;t.id=int(std::uint32_t(handle_camp_t(m.handle)));t.pos={m.pos.x,m.pos.y};
            t.hp=m.hp;t.shield=m.shield;t.kind=word::TargetKind::camp;t.visible=true;
            t.weight=m.group==job?1.0:0.0;t.basic_hit=me.strength*camp_multiplier;
            local.push_back({t,int(i),{}});
        }
    }
    bool work_candidate(const Member& m)const {
        return m.work.nomination(epoch)==work_memory::Nomination::Work;
    }
    int probe_member(int group) {
        auto& g=memory[std::size_t(group)];const auto count=g.end-g.begin;
        for(std::size_t n=0;n<count;++n) {
            const auto index=g.begin+(g.probe_cursor+n)%count;
            if(members[index].work.due(epoch))return int(index);
        }
        return -1;
    }
    bool reveal_work(int index,const Info& me) {
        if(!fuel(30000)||state(me).is_dead()||self().buffs().has_stun())return false;
        detail::ward_resources resource{};
        detail::check(detail::sdk_champion_wards(std::uint32_t(handle_champion_t(self())),&resource));
        if(!resource.charges)return false;
        const auto target=members[std::size_t(index)].pos;
        auto blocked=*walls|vision::ward_tiles(team::ally);
        vec2 best{};int distance=10000;bool found=false;
        const int radius=int(constants::ward_placement_radius);
        for(int y=-radius;y<=radius;++y)for(int x=-radius;x<=radius;++x) {
            const vec2 p{me.x+x,me.y+y};
            if(p.x<0||p.y<0||p.x>=walls->width()||p.y>=walls->height()||blocked.is_set(p))continue;
            const int d=dist(p,target);
            if(d<distance){distance=d;best=p;found=true;}
        }
        if(!found||!fuel(22000))return false;
        const bool applied=self().place_ward(best);
        // A refused ward may reveal occupancy too. Requery before continuation.
        collect(body(),false,18000);
        if(applied)last_action_round=epoch;
        return applied;
    }
    bool observe_ingress(int index,const Info& me) {
        if(index<0||std::size_t(index)>=members.size())return false;
        observation_member=index;
        // Personal ward vision is a real information operator preserving MAC.
        // A successful relocation is followed by fresh life/vision before work.
        if(reveal_work(index,me)) {
            if(fuel(44000))micro();else cheap_basic();
            return true;
        }
        // This is an observation target, never reconstructed positive work.
        // If a hidden body refuses entry, the next input refreshes the exact
        // target before another command. A visible live body reopens work.
        bitboard goals;goals.set(members[std::size_t(index)].pos);
        const bool moved=walk(std::move(goals));
        // Refresh after success OR refusal before any continued physical word.
        if(fuel(44000))micro();else if(moved)cheap_basic();
        return moved;
    }
    void collect(const Info& me,bool include_groups,std::uint64_t reserve=60000) {
        local.clear();threats.clear();
        for(std::size_t i=0;i<members.size();++i)if(members[i].base_strength>0&&
            dist(point(me),members[i].pos)<=members[i].range+me.move_speed+1)threats.push_back(i);
        const auto board=vision::visible_tiles();board.export_linear_buffer(visible);
        // Foes are inserted first; no hidden foe is turned into a current fact.
        for(const auto& enemy:vision::champions(team::enemy)) {
            if(!fuel(reserve+2000)||local.size()>=8)break;
            Info q{};detail::check(detail::sdk_champion(std::uint32_t(handle_champion_t(enemy)),&q));
            if(state(q).is_dead())continue;
            float shield=0;const auto buffs=enemy.buffs();
            if(buffs.has_shield())shield=buffs.get<buff_shield>().value;
            word::Target t;t.id=int(std::uint32_t(handle_champion_t(enemy)));t.pos={q.x,q.y};
            t.hp=q.health;t.shield=shield;t.kind=word::TargetKind::enemy;t.visible=true;t.weight=2;
            t.basic_hit=me.strength;local.push_back({t,-1,enemy});
        }
        refresh_group(job);
        const int priority=observation_member;
        if(priority>=0&&std::size_t(priority)<members.size()&&sees(members[std::size_t(priority)].pos)&&fuel(reserve)) {
            read_member(std::size_t(priority),me);
            const auto& m=members[std::size_t(priority)];auto& g=memory[std::size_t(m.group)];
            if(g.end>g.begin)g.probe_cursor=(std::size_t(priority)-g.begin+1)%(g.end-g.begin);
            observation_member=-1;
        }
        // Prioritize the retained service, then local collateral. Collection is
        // bounded; omitted/hidden bodies remain in the catalog risk model.
        for(int pass=0;pass<2;++pass)for(std::size_t i=0;i<members.size();++i) {
            auto& m=members[i];
            if(int(i)==priority||(pass==0)!=(m.group==job)||!sees(m.pos)||dist(point(me),m.pos)>6)continue;
            if(!fuel(reserve))break;
            read_member(i,me);
        }
        if(include_groups&&fuel(45000)) {
            refresh_group(job);
            for(std::size_t n=0;n<std::min<std::size_t>(groups.size(),3)&&fuel(43000);++n)
                refresh_group(int((std::size_t(epoch)*3+n)%groups.size()));
        }
    }
    float camp_risk(vec2 position,int wake=-1,bool potential=false)const {
        float total=0;
        for(const auto index:threats) {
            const auto& m=members[index];
            if(dist(position,m.pos)>m.range||absent(m))continue;
            const float strength=m.seen==epoch&&m.alive?m.strength:m.base_strength;
            if(potential||m.group==wake||(m.seen==epoch?m.awake:true))total+=strength*camp_taken;
        }
        return total;
    }
    float enemy_risk(vec2 position)const {
        float total=0;
        for(const auto& t:local)if(t.enemy) {
            Info q{};detail::check(detail::sdk_champion(std::uint32_t(handle_champion_t(*t.enemy)),&q));
            if(dist(position,{q.x,q.y})<=q.move_speed+std::max(q.attack_range,4))
                total+=q.strength*1.125f+q.focus/4;
        }
        return total;
    }
    int choose_job(const Info& me) {
        int best=-1;double value=-1;
        const float rate=std::max(1.0f,me.strength*1.125f+me.focus/8);
        for(std::size_t g=0;g<groups.size();++g) {
            const auto& memo=memory[g];if(memo.ban_until>epoch||(memo.state==3&&epoch<memo.until))continue;
            float hp=0;int distance=10000,count=0,work_count=0;bool probe_due=false;
            for(std::size_t i=memo.begin;i<memo.end;++i) {
                const auto& m=members[i];distance=std::min(distance,dist(point(me),m.pos));++count;
                if(work_candidate(m)){hp+=m.seen>=0&&m.alive?m.hp:m.base_hp;++work_count;}
                probe_due=probe_due||m.work.due(epoch);
            }
            if(memo.state==2&&epoch<memo.until)hp=0;
            else if(!work_count) {
                if(!probe_due)continue;
                // Probe utility estimates unseen future work. It does not
                // relabel the remembered corpses or authorize an attack.
                for(std::size_t i=memo.begin;i<memo.end;++i)hp+=members[i].base_hp;
            }
            const float splash=passive==passive_type::splash_attack?std::min(3.0f,float(count)):1.0f;
            const double work=hp/(rate*splash);
            const double travel=std::max(0,distance-me.attack_range);
            const double utility=(double(groups[g].gem_value())+0.12*groups[g].gold_value())/
                (8.0+travel+work+0.1*groups[g].gem_value());
            if(utility>value){value=utility;best=int(g);}
        }
        return best;
    }
    void shop() {
        if(!fuel(22000))return;
        const auto items=self().item_count();
        const auto jungle=items[item_type::jungle_emblem_of_strength];
        const auto item=jungle?item_type::power_shard_of_strength:item_type::jungle_shard_of_strength;
        unsigned count=self().gold_count()/item.cost();
        if(!jungle)count=std::min<unsigned>(count,5-std::min<unsigned>(4,items[item_type::jungle_shard_of_strength]));
        if(count&&self().can_buy(item,count))self().buy(item,count);
        const auto after=self().item_count();camp_taken=1.0f;
        if(after[item_type::jungle_emblem_of_strength]) {
            const auto item=item_type::jungle_emblem_of_strength;
            if(item.passive()==item_passive::jungle)camp_taken=std::clamp(1.0f-item.passive_magnitude(),0.0f,1.0f);
        }
        if(after[item_type::jungle_emblem_of_strength]&&fuel(20000)) {
            const auto power=item_type::power_shard_of_strength;
            const auto more=self().gold_count()/power.cost();
            if(more&&self().can_buy(power,more))self().buy(power,more);
        }
    }
    std::uint64_t funded_key(funded_dispatch::Owner owner)const {
        return (std::uint64_t(owner)+1u)<<56 |
            (std::uint64_t(std::uint32_t(job+1))<<24) |
            (std::uint64_t(std::uint32_t(observation_member+1))<<1) |
            std::uint64_t(funded_state().refine_basic);
    }
    bitboard fresh_blocked(vec2 origin) {
        auto blocked=*walls|vision::champion_tiles()|vision::camp_tiles()|vision::teleport_tiles();
        blocked.set(origin,false);return blocked;
    }
    bool funded_refresh_route(const Info& me,const bitboard& blocked) {
        auto& route=planned_route();const auto p=point(me);const auto current=exposure(p);
        if(current.valid&&current.amount==0)return route.reset_at_quiet(rp(p),current,std::uint64_t(epoch));
        if(!route.valid()||!route.at(rp(p)))return false;
        const auto count=route.size();
        if(!fuel(14000+count*160))return false;
        std::array<return_budget::Damage,return_route::Route::capacity> damage{};
        std::array<bool,return_route::Route::capacity> open{};
        for(std::size_t i=0;i<count;++i){const auto q=vp(route.node(i).point);damage[i]=exposure(q);open[i]=!blocked.is_set(q);}
        return route.refresh_damage({damage.data(),count},{open.data(),count},std::uint64_t(epoch))==return_route::Reason::ok;
    }
    funded_navigation::Preparation funded_prepare(const Info& me) {
        auto& f=funded_state();
        if(!fuel(42000))return {};
        const bool hold=work_affordable(me);
        if(!hold)return {};
        auto blocked=fresh_blocked(point(me));
        const auto floor=FundedState::nomination_floor+6000+
            std::uint64_t(walls->width())*std::uint64_t(walls->height())*3;
        const auto result=f.navigation.prepare(point(me),blocked,std::uint64_t(epoch),hold,
            [&](std::uint64_t reserve){return fuel(reserve);},floor);
        if(result.progressed){f.prepared_round=epoch;diag_stage(18);}
        return result;
    }
    bool funded_nominate(const bitboard& goals,bool emergency=false) {
        auto& f=funded_state();f.prepared=false;
        if(!fuel(FundedState::nomination_floor))return false;
        const auto me=body();if(!state(me).contains(champion_state::can_move))return false;
        auto blocked=fresh_blocked(point(me));
        if(!funded_refresh_route(me,blocked))return false;
        const auto reachable=blocked.reachable_tiles(point(me),1);
        ++f.observation;f.failures.enter(std::uint64_t(epoch),f.effect,f.observation);
        const auto nomination=f.navigation.nominate(point(me),reachable);
        std::array<funded_navigation::Step,8> candidates{};std::size_t count=0;
        // An observed adjacent terminal is its own finite operator; it does not
        // wait for a far-goal field after a clear or a bank/observation change.
        static constexpr std::array<vec2,8> directions{{{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}}};
        if(!goals.is_set(point(me)))for(unsigned i=0;i<directions.size();++i) {
            const auto p=point(me)+directions[i];
            if(p.x<0||p.y<0||p.x>=walls->width()||p.y>=walls->height())continue;
            if(reachable.is_set(p)&&goals.is_set(p))candidates[count++]={p,0,i};
        }
        if(!count&&nomination.status==funded_navigation::Status::Prepared) {
            candidates=nomination.candidates;count=nomination.count;
        }
        if(!count)f.navigation.repair(nomination,blocked);
        const auto priced=exit_state(me,false);auto post=priced;post.can_move_now=false;
        for(std::size_t i=0;i<count&&fuel(22000);++i) {
            const auto& candidate=candidates[i];
            if(!f.failures.permits(std::uint64_t(epoch),f.effect,f.observation,candidate.direction))continue;
            return_route::Route projected;
            if(!project_route(me,candidate.point,projected))continue;
            const auto success=projected.quote(post);
            auto refused=priced;++refused.additional_forced_waits;refused.allow_one_refusal=false;
            const auto failure=planned_route().quote(refused);
            const bool certified=success.valid&&success.safe&&failure.valid&&failure.safe;
            const bool explicit_risk=emergency&&exposure(candidate.point).valid&&
                exposure(candidate.point).amount<=exposure(point(me)).amount&&
                double(exposure(candidate.point).amount)<me.health;
            if(!certified&&!explicit_risk)continue;
            f.prepared=true;f.prepared_origin=point(me);f.prepared_next=candidate.point;
            f.prepared_route=projected;f.direction=candidate.direction;
            f.prepared_frame=std::uint64_t(epoch);f.prepared_effect=f.effect;f.prepared_observation=f.observation;
            f.prepared_support_revision=support_detail.revision();
            return true;
        }
        return false;
    }
    bool funded_dispatch_move(const bitboard& goals,bool emergency=false) {
        auto& f=funded_state();
        // A late dispatcher never computes a field or enumerates global roots.
        // Fresh validation is paid before it makes one actual SDK attempt.
        if((!f.prepared||f.prepared_frame!=std::uint64_t(epoch)||f.prepared_effect!=f.effect||
            f.prepared_observation!=f.observation||f.prepared_support_revision!=support_detail.revision())&&
            !funded_nominate(goals,emergency)){diag_walk(7);return false;}
        if(!fuel(14000))return false;
        const auto me=body();
        if(point(me)!=f.prepared_origin||!state(me).contains(champion_state::can_move)||
           !f.failures.permits(std::uint64_t(epoch),f.effect,f.observation,f.direction)){f.prepared=false;return false;}
        const auto origin=f.prepared_origin,next=f.prepared_next;
        const bool applied=self().move_absolute(next);
        if(applied) {
            planned_route()=f.prepared_route;
            if(exit_trace.at(rp(origin)))exit_trace.observe_step(rp(origin),rp(next),exposure(next),true);
            else if(exposure(next).amount==0)exit_trace.reset_quiet(rp(next),exposure(next));
            walked=true;last_action_round=epoch;++f.effect;f.hold_admitted=false;
            diag_walk(9);return true;
        }
        planned_route().observe_refusal(rp(origin));++return_refusals;
        f.failures.refused(f.direction);f.navigation.invalidate();f.prepared=false;
        // Close the actual false with fresh body/vision. No stale-mask retry or
        // fabricated cursor/physical progress is credited in this owner input.
        const auto actual=body();if(fuel(8000)) {
            auto fresh=vision::visible_tiles();fresh.export_linear_buffer(visible);
            ++f.observation;f.failures.refreshed(std::uint64_t(epoch),f.effect,f.observation);
        }
        if(point(actual)!=origin)planned_route().clear();
        diag_walk(10);return false;
    }
    bool funded_walk(bitboard goals,bool flee=false,bool emergency=false) {
        auto& f=funded_state();const auto me=body();
        if(!state(me).contains(champion_state::can_move))return false;
        const auto owner=flee?funded_dispatch::Owner::recover:task==Task::bank?funded_dispatch::Owner::bank:
            job>=0&&memory[std::size_t(job)].state==2&&memory[std::size_t(job)].seen==epoch?
                funded_dispatch::Owner::capture:funded_dispatch::Owner::approach;
        f.owner=owner;
        const bool changed=f.navigation.request(funded_key(owner),goals,std::uint64_t(epoch),!flee);
        if(changed){f.prepared_round=epoch;diag_stage(18);}
        if(funded_nominate(goals,emergency))return funded_dispatch_move(goals,emergency);
        if(!f.navigation.ready())funded_prepare(me);
        if(f.navigation.ready())return funded_dispatch_move(goals,emergency);
        diag_stage(f.prepared_round==epoch?18:20);return false;
    }
    funded_dispatch::Knowledge funded_probe(const Info& me,std::uint64_t tail,std::uint64_t budget=8000,bool refresh=false) {
        auto& f=funded_state();f.probe_kind=0;f.probe_slot=-1;f.probed_member=-1;f.enemy.reset();
        struct KnowledgeFence {
            Runtime& runtime;std::uint64_t revision;
            ~KnowledgeFence(){auto& state=runtime.funded_state();if(revision!=runtime.support_detail.revision()){
                ++state.observation;state.prepared=false;state.hold_admitted=false;
            }}
        } knowledge_fence{*this,support_detail.revision()};
        const auto start=fuel_remaining();const auto stop=std::max(tail,start>budget?start-budget:tail);
        if(!fuel(stop+2000))return funded_dispatch::Knowledge::unknown;
        if(refresh){auto sight=vision::visible_tiles();sight.export_linear_buffer(visible);++f.observation;}
        const auto flags=state(me);const auto abilities=self().abilities();
        struct Slot{bool ready{},reset{},damage{},ground{},camps{},enemies{},exclusive{};int range{},radius{};};
        std::array<Slot,2> slots{};int reach=me.attack_range;
        for(std::size_t i=0;i<slots.size();++i)if(abilities[i].info()) {
            auto& slot=slots[i];
            slot.ready=flags.contains(champion_state::can_cast)&&abilities[i].cooldown()==0;
            std::visit([&](const auto& info){
                using T=std::decay_t<decltype(info)>;
                slot.exclusive=info.exclusive_with_movement;
                slot.camps=info.valid_targets.contains(ability_target_mask::camps);
                slot.enemies=info.valid_targets.contains(ability_target_mask::enemies);
                slot.ground=info.valid_targets.contains(ability_target_mask::ground);
                if constexpr(std::is_same_v<T,ability_attack_reset>)slot.reset=true;
                else if constexpr(std::is_same_v<T,ability_damage>||std::is_same_v<T,ability_cc>||std::is_same_v<T,ability_aoe_damage>){
                    slot.damage=true;slot.range=int(info.range);
                    if constexpr(std::is_same_v<T,ability_aoe_damage>)slot.radius=int(info.radius);
                }
            },*abilities[i].info());
            slot.ready=slot.ready&&!(slot.exclusive&&walked);
            if(slot.ready&&slot.damage)reach=std::max(reach,slot.range+slot.radius);
        }
        auto accept=[&](int kind,int slot,int member,std::optional<champion> enemy,vec2 center){
            f.probe_kind=kind;f.probe_slot=slot;f.probed_member=member;f.enemy=std::move(enemy);f.probe_center=center;
            f.probe_frame=std::uint64_t(epoch);f.probe_effect=f.effect;f.probe_observation=f.observation;
            return funded_dispatch::Knowledge::executable;
        };
        auto operator_for=[&](vec2 p,int member,std::optional<champion> enemy)->bool {
            const auto distance=dist(point(me),p);
            if(flags.contains(champion_state::can_attack)&&distance<=me.attack_range){accept(1,-1,member,enemy,p);return true;}
            for(int i=0;i<2;++i){const auto& slot=slots[std::size_t(i)];if(!slot.ready)continue;
                if(slot.reset&&!flags.contains(champion_state::can_attack)&&
                   (attacked||(walked&&me.attack_range>1))&&distance<=me.attack_range){accept(4,i,member,enemy,p);return true;}
                const bool unit_target=enemy?slot.enemies:slot.camps;
                if(!slot.damage||(!slot.ground&&!unit_target)||!fuel(stop+1500))continue;
                if(abilities[std::size_t(i)].damage_amount()<=0&&!abilities[std::size_t(i)].is_cc())continue;
                if(unit_target&&distance<=slot.range){accept(2,i,member,enemy,p);return true;}
                if(!slot.ground||distance>slot.range+slot.radius)continue;
                for(int y=std::max(p.y-slot.radius,me.y-slot.range);y<=std::min(p.y+slot.radius,me.y+slot.range);++y)
                    for(int x=std::max(p.x-slot.radius,me.x-slot.range);x<=std::min(p.x+slot.radius,me.x+slot.range);++x){
                        if(!fuel(stop+500))return false;
                        const vec2 center{x,y};
                        if(sees(center)&&!static_blocked(center)){accept(3,i,member,enemy,center);return true;}
                    }
            }
            return false;
        };
        // The bounded hot set is positive evidence, never a completeness claim.
        for(const auto& entry:local)if(entry.enemy&&entry.target.hp>0&&sees({entry.target.pos.x,entry.target.pos.y})&&
            operator_for({entry.target.pos.x,entry.target.pos.y},-1,entry.enemy))return funded_dispatch::Knowledge::executable;
        for(std::size_t i=0;i<members.size();++i) {
            if((i&15u)==0&&!fuel(stop+1500))return funded_dispatch::Knowledge::unknown;
            auto& member=members[i];
            if(dist(point(me),member.pos)>reach||!sees(member.pos))continue;
            if(member.seen!=epoch){if(!fuel(stop+3000))return funded_dispatch::Knowledge::unknown;read_member(i,me);}
            if(!member.alive)continue;
            bool useful=member.group==job;
            if(!useful&&passive==passive_type::splash_attack&&dist(point(me),member.pos)<=me.attack_range&&job>=0){
                const auto& g=memory[std::size_t(job)];
                for(std::size_t k=g.begin;k<g.end;++k)if(members[k].seen==epoch&&members[k].alive&&
                    dist(member.pos,members[k].pos)<=int(constants::passive_abilities::varg_splash_radius)){useful=true;break;}
            }
            if(useful&&operator_for(member.pos,int(i),{}))return funded_dispatch::Knowledge::executable;
        }
        f.probe_complete=true;
        // Ready support/heal/ground and omitted foe/anchor scope can still
        // contain a useful word. No bounded eight-target menu proves absence.
        return !flags.contains(champion_state::can_attack)&&!flags.contains(champion_state::can_cast)?
            funded_dispatch::Knowledge::absent:funded_dispatch::Knowledge::unknown;
    }
    bool funded_execute_work() {
        auto& f=funded_state();
        if(!f.probe_kind||!f.hold_admitted||f.probe_frame!=std::uint64_t(epoch)||
           f.probe_effect!=f.effect||f.probe_observation!=f.observation||
           f.hold_support_revision!=support_detail.revision()||!fuel(8000))return false;
        auto me=body();if(point(me)!=f.hold_origin||f.hold_frame!=std::uint64_t(epoch)||f.hold_effect!=f.effect)return false;
        const auto alive=[&]{return f.enemy?!f.enemy->state().is_dead():f.probed_member>=0&&members[std::size_t(f.probed_member)].handle.state().is_alive();};
        if(!alive())return false;
        auto abilities=self().abilities();bool applied=false;
        if(f.probe_kind==1||f.probe_kind==4) {
            if(f.probe_kind==4){
                auto& reset=abilities[std::size_t(f.probe_slot)];
                if(!state(me).contains(champion_state::can_cast)||reset.cooldown()!=0||!reset.use())return false;
                last_action_round=epoch;++f.effect;diag_micro(5);me=body();
            }
            if(!state(me).contains(champion_state::can_attack))return false;
            if(f.enemy)self().attack(*f.enemy);else self().attack(members[std::size_t(f.probed_member)].handle);
            attacked=true;diag_micro(4);applied=true;
        } else {
            auto& ability=abilities[std::size_t(f.probe_slot)];
            if(!state(me).contains(champion_state::can_cast)||ability.cooldown()!=0)return false;
            if(f.probe_kind==3)applied=ability.use(f.probe_center);
            else if(f.enemy)applied=ability.use(*f.enemy);
            else applied=ability.use(members[std::size_t(f.probed_member)].handle);
            if(applied)diag_micro(5);
        }
        if(applied){last_action_round=epoch;++f.effect;f.prepared=false;f.hold_admitted=false;return true;}
        const auto actual=body();auto sight=vision::visible_tiles();sight.export_linear_buffer(visible);
        ++f.observation;f.prepared=false;if(point(actual)!=point(me))planned_route().clear();
        return false;
    }
    bool funded_cached_basic(const Info& me) {
        auto& f=funded_state();
        if(!state(me).contains(champion_state::can_attack))return false;
        for(const auto& entry:local) {
            const vec2 p{entry.target.pos.x,entry.target.pos.y};
            if(entry.target.hp<=0||!sees(p)||dist(point(me),p)>me.attack_range)continue;
            bool useful=entry.enemy.has_value()||entry.member>=0&&members[std::size_t(entry.member)].group==job;
            if(!useful&&passive==passive_type::splash_attack)
                for(const auto& target:local)if(target.member>=0&&members[std::size_t(target.member)].group==job&&
                    target.target.hp>0&&dist(p,{target.target.pos.x,target.target.pos.y})<=int(constants::passive_abilities::varg_splash_radius)){
                    useful=true;break;
                }
            if(!useful)continue;
            f.probe_kind=1;f.probe_slot=-1;f.probed_member=entry.member;f.enemy=entry.enemy;f.probe_center=p;
            f.probe_frame=std::uint64_t(epoch);f.probe_effect=f.effect;f.probe_observation=f.observation;
            return true;
        }
        return false;
    }
    void funded_stamp_hold(const Info& me,bool admitted) {
        auto& f=funded_state();f.hold_admitted=admitted;f.hold_origin=point(me);
        f.hold_frame=std::uint64_t(epoch);f.hold_effect=f.effect;
        f.hold_support_revision=support_detail.revision();
    }
    void funded_observe(int index,const Info& input) {
        auto& f=funded_state();auto& owner=f.observation_owner;
        owner.request(job,index,std::uint64_t(epoch));owner.enter(std::uint64_t(epoch));
        // A matching actual read can resolve a closed key too. Clock passage
        // cannot reopen it, while newly available life information can.
        if(sees(members[std::size_t(index)].pos)&&fuel(18000)) {
            if(members[std::size_t(index)].seen!=epoch)read_member(std::size_t(index),input);
            owner.observed(job,index,std::uint64_t(epoch),members[std::size_t(index)].alive?
                service_owner::Life::alive:service_owner::Life::dead);
            observation_member=-1;diag_stage(17);return;
        }
        if(owner.completed()&&!sees(members[std::size_t(index)].pos))owner.renew_after_observed(std::uint64_t(epoch));
        if(!owner.active()) {
            if(owner.status()==service_owner::Status::blocked) {
                memory[std::size_t(job)].ban_until=epoch+12;job=-1;observation_member=-1;
                f.navigation.invalidate();diag_stage(20);
            }
            return;
        }
        observation_member=index;auto me=input;
        f.owner=funded_dispatch::Owner::observe;
        bitboard goals;goals.set(members[std::size_t(index)].pos);
        const bool changed=f.navigation.request(funded_key(f.owner),goals,std::uint64_t(epoch));
        bool progressed=changed;
        if(!f.navigation.ready())progressed=funded_prepare(me).progressed||progressed;
        const bool moved=funded_nominate(goals)&&funded_dispatch_move(goals);
        if(moved) {
            owner.progress(std::uint64_t(epoch),++f.observation_receipt);
            idle=0;diag_stage(17);return;
        }
        // A real information operator is another way to enable this owner.
        // Its existing 30K admission includes its mandatory fresh collection.
        // Failure does not renew the lease or masquerade as observed life.
        if(fuel(38000)&&reveal_work(index,me)) {
            ++f.effect;
            if(members[std::size_t(index)].seen==epoch) {
                owner.observed(job,index,std::uint64_t(epoch),members[std::size_t(index)].alive?
                    service_owner::Life::alive:service_owner::Life::dead);
                observation_member=-1;
            } else {
                // An accepted ward is an actual effect, but it did not supply
                // this owner's enabling fact. It cannot renew this lease.
                owner.blocked(std::uint64_t(epoch));
                if(!owner.active()) {
                    memory[std::size_t(job)].ban_until=epoch+12;job=-1;
                    observation_member=-1;f.navigation.invalidate();
                }
            }
            diag_stage(17);return;
        }
        if(progressed){diag_stage(18);return;}
        owner.blocked(std::uint64_t(epoch));
        if(!owner.active()) {
            memory[std::size_t(job)].ban_until=epoch+12;job=-1;
            observation_member=-1;f.navigation.invalidate();
        }
        diag_stage(20);
    }
    void funded_service(const Info& input) {
        auto& f=funded_state();f.active=true;
        auto finish=[&]{f.active=false;};
        auto me=input;
        if(f.key_job!=job){f.refine_basic=false;f.key_job=job;}
        auto& observation=f.observation_owner;
        if(observation.active()&&observation.job()!=job)observation.request(job,-1,std::uint64_t(epoch));
        if(observation.active()) {
            funded_observe(observation.member(),me);
            // Resolving life earns a fresh work nomination next input; it does
            // not republish a competing work halo over the retained owner now.
            finish();return;
        }
        bitboard goals;int work_count=0,unseen=-1,nearest=10000;
        for(std::size_t i=memory[std::size_t(job)].begin;i<memory[std::size_t(job)].end;++i)if(work_candidate(members[i])) {
            const auto& member=members[i];
            goals.draw_chebyshev(member.pos,f.refine_basic?me.attack_range:std::max(me.attack_range,spell_reach));++work_count;
            if(!sees(member.pos)&&dist(point(me),member.pos)<nearest){nearest=dist(point(me),member.pos);unseen=int(i);}
        }
        if constexpr(cfg::trace>1){diagnostic().work_count=work_count;diagnostic().unseen=unseen;}
        if(!work_count) {
            const auto probe=probe_member(job);diag_stage(17);
            if(probe>=0)funded_observe(probe,me);
            else {memory[std::size_t(job)].ban_until=epoch+12;job=-1;diag_stage(20);}
            finish();return;
        }
        f.owner=funded_dispatch::Owner::approach;
        const bool changed=f.navigation.request(funded_key(f.owner),goals,std::uint64_t(epoch));
        if(changed){f.prepared_round=epoch;diag_stage(18);}
        if(!f.navigation.ready())funded_prepare(me);
        me=body();funded_stamp_hold(me,work_affordable(me));
        bool physical=funded_nominate(goals);
        auto knowledge=funded_cached_basic(me)?funded_dispatch::Knowledge::executable:funded_dispatch::Knowledge::unknown;
        auto make_input=[&](bool probe) {
            funded_dispatch::Input in;in.frame=std::uint64_t(epoch);in.observation=f.observation;in.fuel=fuel_remaining();
            in.owner=funded_dispatch::Owner::work;in.work=knowledge;
            in.work_action={f.probe_kind==1?funded_dispatch::Action::basic:funded_dispatch::Action::cast,
                knowledge==funded_dispatch::Knowledge::executable,f.hold_admitted,true,in.frame,in.observation,8000};
            in.physical={funded_dispatch::Action::move,physical,physical,true,in.frame,in.observation,
                physical?FundedState::dispatch_tail:FundedState::nomination_floor};
            in.work_probe_cost=probe?8000:0;
            in.preparation={!f.navigation.ready(),f.hold_admitted,f.navigation.cursor().remaining(),
                6000+std::uint64_t(walls->width())*std::uint64_t(walls->height())*3};
            in.nominal_goal=goals.is_set(point(me));return in;
        };
        auto decision=funded_dispatch::decide(make_input(true));
        if(decision.kind==funded_dispatch::Kind::probe_work) {
            knowledge=funded_probe(me,physical?FundedState::dispatch_tail:8000,8000);
            physical=f.prepared&&f.prepared_support_revision==support_detail.revision();
            decision=funded_dispatch::decide(make_input(false));
        }
        diag_stage(16);
        if(decision.kind==funded_dispatch::Kind::work) {
            // An executable work owner has its own guaranteed primitive tail.
            // Only the surplus may improve that incumbent through the word menu.
            bool acted=funded_execute_work();
            // The actual incumbent receives its tail first. Any improving
            // reset/cast menu runs only after that physical effect exists.
            if(acted&&fuel(44000))acted=micro(true)||acted;
            if(last_action_round==epoch){++f.effect;f.prepared=false;f.hold_admitted=false;cast_wait_until=-1;}
            me=body();refresh_group(job);
            const auto& group=memory[std::size_t(job)];
            if(group.state==2&&group.seen==epoch){
                bitboard capture;for(const auto& camp:groups[std::size_t(job)].camps())capture.set(camp.spawn_position());
                if(!capture.is_set(point(me)))funded_walk(std::move(capture));
            } else if(!goals.is_set(point(me))&&state(me).contains(champion_state::can_move))funded_dispatch_move(goals);
            if(acted||last_action_round==epoch){idle=0;previous=point(body());finish();return;}
        } else if(decision.kind==funded_dispatch::Kind::physical) {
            if(funded_dispatch_move(goals)) {
                // Preserve the supported ranged Move -> Reset -> Basic word.
                // It is a fresh post-move offer, not a basic with spent A.
                me=body();
                if(fuel(36000)&&work_affordable(me)) {
                    funded_stamp_hold(me,true);
                    if(funded_probe(me,14000,8000,true)==funded_dispatch::Knowledge::executable)funded_execute_work();
                }
                idle=0;previous=point(body());finish();return;
            }
            if(f.failures.refresh_required()){finish();return;}
        } else if(decision.kind==funded_dispatch::Kind::prepare) {
            const auto prepared=funded_prepare(me);
            if(prepared.progressed){diag_stage(18);finish();return;}
        }
        // A nominal perch is not completion. Use a real visibility operation,
        // finite actual cooldown, or a stricter ordinary attack-access intent.
        me=body();
        if(last_action_round==epoch){idle=0;finish();return;}
        if(unseen>=0&&goals.is_set(point(me))) {
            funded_observe(unseen,me);finish();return;
        }
        if(goals.is_set(point(me))&&exposure(point(me)).amount==0&&spell_reach>me.attack_range) {
            int remaining=10000;
            for(const auto& ability:self().abilities())if((ability.is_damage()||ability.is_aoe_damage()||ability.is_cc())&&ability.cooldown()>0)
                remaining=std::min(remaining,int(ability.cooldown()));
            if(remaining<10000){if(cast_wait_until<0)cast_wait_until=epoch+remaining;
                if(epoch<cast_wait_until){diag_stage(18);finish();return;}}
        }
        if(goals.is_set(point(me))&&!f.refine_basic){f.refine_basic=true;f.navigation.invalidate();diag_stage(18);finish();return;}
        if(f.prepared_round==epoch){diag_stage(18);finish();return;}
        // Failed/unsupported nomination has an explicit finite resolver. It
        // does not renew an idle lease merely by creating another epoch nonce.
        if(++idle>=4){memory[std::size_t(job)].ban_until=epoch+12;job=-1;idle=0;f.navigation.invalidate();}
        diag_stage(20);finish();
    }
    bool walk(bitboard goals,bool flee=false,bool emergency=false) {
        if constexpr(cfg::funded_dispatch)return funded_walk(std::move(goals),flee,emergency);
        const auto me=body();
        const bool can_walk=state(me).contains(champion_state::can_move);
        if((!can_walk&&dash_slot<0)||!fuel(36000)){diag_walk(1);return false;}
        align_exit(me);
        if constexpr(cfg::route_exit)if(!prepare_route(me)&&!emergency){diag_walk(2);return false;}
        const auto priced=exit_state(me,false);
        auto occupied=vision::champion_tiles()|vision::camp_tiles()|vision::teleport_tiles();
        occupied.set(point(me),false);
        auto blocked=*walls|occupied;
        blocked.set(point(me),false);goals&=~blocked;
        if(goals.is_empty()||goals.is_set(point(me))){
            if constexpr(cfg::trace>1)diag_walk(goals.is_empty()?3:4);
            return false;
        }
        const auto chunks=std::uint64_t((walls->width()+7)/8)*std::uint64_t((walls->height()+7)/8);
        int nearest=emergency?1:10000;
        if(!emergency)for(const auto tile:goals.positions())nearest=std::min(nearest,dist(point(me),tile));
        const auto limit=std::uint16_t(std::min(120,nearest+path_slack));
        if(!fuel(chunks*(16+limit)+22000)){diag_walk(5);return false;}
        bitboard destination;destination.set(point(me));bitboard visited;
        distance_field field;field.compute_to(blocked,goals,destination,visited,limit);
        if(!fuel(18000)){diag_walk(6);return false;}
        const auto origin_distance=field.get(point(me));
        if(origin_distance==distance_field::unreachable)path_slack=std::min(120,path_slack*2);
        const auto reachable=blocked.reachable_tiles(point(me),me.move_speed).positions();
        std::array<vec2,81> options{};std::array<double,81> values{};std::size_t count=0;bool priced_out=false;
        for(const auto tile:reachable) {
            if(!can_walk||tile==point(me)||dist(tile,point(me))>1||!fuel(16000)||count==options.size())continue;
            const auto d=field.get(tile);if(d>=origin_distance)continue;
            options[count]=tile;values[count]=double(d)+(flee?0.1:0.015)*camp_risk(tile,job,true);++count;
        }
        // Dash is its own native transition, even when ordinary M is spent.
        // Keep the cheap ordinary incumbent when it can already progress;
        // this first class uses a complete quiet landing across an access gap.
        if(dash_slot>=0&&dash_scratch&&(!can_walk||count==0)&&fuel(42000)) {
            auto transit=*dash_walls|occupied;transit.set(point(me),false);
            const dash_word::lean::MaskStamp stamp{epoch,std::uint32_t(handle_champion_t(self())),0};
            const auto result=dash_word::lean::try_quiet_dash(dash_slot,walked,stamp,transit,*dash_scratch,
                [&]{return fuel(22000);},[]{return std::uint64_t(0);},
                [&](dash_word::Point p){return !static_blocked({p.x,p.y});},
                [&](dash_word::Point p){
                    const auto d=exposure({p.x,p.y});
                    return dash_word::lean::CampSupport{std::uint32_t(std::min<std::uint64_t>(d.amount,0xffffffffu)),
                        d.valid&&d.amount<=0xffffffffu};},
                [&](dash_word::Point p){return int(field.get({p.x,p.y}));},
                [](const dash_word::lean::Choice&){return true;});
            if(result.attempted) {
                // Lexically query-only nomination scope ends at this attempt;
                // no retained menu/metadata survives success OR refusal.
                const auto actual=body();
                if constexpr(cfg::melee_exit)melee_exit::observe_nonordinary(exit_trace,rp(point(actual)));
                else exit_trace.clear();
                align_exit(actual);
                if(result.applied){last_action_round=epoch;path_slack=8;return true;}
                return false;
            }
        }
        for(std::size_t checked=0;checked<count&&fuel(21000);++checked) {
            const auto at=std::size_t(std::min_element(values.begin(),values.begin()+count)-values.begin());
            const auto tile=options[at];values[at]=1e100;
            auto post=priced;post.can_move_now=false;
            if constexpr(!cfg::route_exit) {
                const auto admitted=exit_trace.quote_after_step(rp(point(me)),rp(tile),exposure(tile),post);
                if(!admitted.valid||!admitted.safe) {
                    priced_out=true;
                    if(!emergency||exposure(tile).amount>exposure(point(me)).amount||
                        double(exposure(tile).amount)>=me.health)continue;
                }
                if(!fuel(14000))break;
                if(self().move_absolute(tile)){
                    if(exit_trace.at(rp(point(me))))exit_trace.observe_step(rp(point(me)),rp(tile),exposure(tile),true);
                    else if(exposure(tile).amount==0)exit_trace.reset_quiet(rp(tile),exposure(tile));
                    walked=true;last_action_round=epoch;path_slack=8;diag_walk(9);return true;
                }
                diag_walk(10);return false;
            } else {
                auto& projected=projected_route();
                bool admitted_valid=project_route(me,tile,projected);
                const auto quote=projected.quote(post);
                // The outward attempt can fail at its ACTUAL origin without
                // spending M. Union that paid wait+return with the successful
                // candidate's complete post-move suffix; neither is progress
                // until the native result says the actor actually moved.
                auto refused=priced;++refused.additional_forced_waits;refused.allow_one_refusal=false;
                const auto false_quote=planned_route().quote(refused);
                admitted_valid=admitted_valid&&quote.valid&&false_quote.valid;
                const bool admitted_safe=quote.safe&&false_quote.safe;
            if(!admitted_valid||!admitted_safe) {
                priced_out=true;
                // Explicit emergency class, not a certified complete exit:
                // a strictly progressing observed route with no greater next
                // camp batch and positive post-batch HP. No phase may silently
                // get this exception merely by being called "flee".
                if(!emergency||exposure(tile).amount>exposure(point(me)).amount||
                    double(exposure(tile).amount)>=me.health)continue;
            }
            if(!fuel(14000))break;
            if(self().move_absolute(tile)){
                if constexpr(cfg::route_exit) {
                    if(admitted_valid)planned_route()=projected_route();
                    else planned_route().clear();
                }
                if(exit_trace.at(rp(point(me))))exit_trace.observe_step(rp(point(me)),rp(tile),exposure(tile),true);
                else if(exposure(tile).amount==0)exit_trace.reset_quiet(rp(tile),exposure(tile));
                walked=true;last_action_round=epoch;path_slack=8;diag_walk(9);return true;
            }
            if constexpr(cfg::route_exit) {
                planned_route().observe_refusal(rp(point(me)));++return_refusals;
            }
            diag_walk(10);return false; // One actual SDK attempt; refresh after a refusal.
            }
        }
        if(priced_out){recovering=true;task=Task::recover;}
        diag_walk(priced_out?8:7);
        return false;
    }
    void retreat(const Info& me) {
        if(state(me).contains(champion_state::submitting_gems))self().cancel_submit_gems();
        if(state(me).contains(champion_state::recalling))self().cancel_recall();
        auto danger=*walls;
        for(const auto& m:members)if(m.base_strength>0&&!absent(m)&&dist(point(me),m.pos)<=m.range+9)
            danger.draw_chebyshev(m.pos,m.range);
        for(const auto& t:local)if(t.enemy)danger.draw_chebyshev({t.target.pos.x,t.target.pos.y},5);
        const auto sight=vision::visible_tiles();auto goals=(~danger)&sight;
        if(goals.is_set(point(me))&&exposure(point(me)).amount==0&&enemy_risk(point(me))==0) {
            self().recall();last_action_round=epoch;return;
        }
        goals.set(point(me),false); // A rejected recall condition must permit real egress.
        if(goals.is_empty())goals=*home;
        walk(std::move(goals),true);
    }
    bool micro(bool reuse=false) {
        diag_micro(0);
        bool applied_any=false;
        for(unsigned primitive=0;primitive<4&&fuel(44000);++primitive) {
            const auto me=body();if(primitive>0||!reuse)collect(me,false,35000);
            if(leave_before_work(me))return applied_any;
            if(!state(me).contains(champion_state::can_attack)&&!state(me).contains(champion_state::can_cast)){diag_micro(1);break;}
            word::Input input;input.body.pos={me.x,me.y};input.body.hp=me.health;
            const auto buffs=self().buffs();
            if(buffs.has_shield())input.body.shield=buffs.get<buff_shield>().value;
            input.body.range=me.attack_range;input.body.splash=passive==passive_type::splash_attack?int(constants::passive_abilities::varg_splash_radius):0;
            input.body.can_attack=state(me).contains(champion_state::can_attack);
            input.body.can_cast=state(me).contains(champion_state::can_cast);input.body.walked=walked;input.body.attack_spent=attacked||(walked&&me.attack_range>1);
            auto abilities=self().abilities();std::array<float,2> damage{};
            for(std::size_t slot=0;slot<abilities.size();++slot) {
                const auto& a=abilities[slot];if(!a.info())continue;
                auto& out=input.abilities[slot];out.ready=input.body.can_cast&&a.cooldown()==0;
                std::visit([&](const auto& info) {
                    using T=std::decay_t<decltype(info)>;out.exclusive=info.exclusive_with_movement;
                    if(info.valid_targets.contains(ability_target_mask::camps))out.target_bits|=word::camps;
                    if(info.valid_targets.contains(ability_target_mask::enemies))out.target_bits|=word::enemies;
                    if(info.valid_targets.contains(ability_target_mask::ground))out.target_bits|=word::ground;
                    if(info.valid_targets.contains(ability_target_mask::self))out.target_bits|=word::self;
                    if constexpr(std::is_same_v<T,ability_attack_reset>)out.kind=word::AbilityKind::reset;
                    else if constexpr(std::is_same_v<T,ability_shield>){out.kind=word::AbilityKind::shield;out.range=int(info.range);out.shield_amount=a.shield_amount();}
                    else if constexpr(std::is_same_v<T,ability_damage>||std::is_same_v<T,ability_cc>||std::is_same_v<T,ability_aoe_damage>) {
                        out.kind=word::AbilityKind::damage;out.range=int(info.range);damage[slot]=a.damage_amount();
                        if constexpr(std::is_same_v<T,ability_aoe_damage>)out.radius=int(info.radius);
                    }
                },*a.info());
            }
            const auto threat=camp_risk(point(me),job,true)+enemy_risk(point(me));
            input.self_shield_value=threat>0?std::min(1.0f,threat*5/std::max(1.0f,me.focus*2.5f)):0;
            bool useful=false,need_ground=false;int ground_range=0,ground_radius=0;
            for(const auto& a:input.abilities)if(a.ready&&!(a.exclusive&&walked)) {
                if(a.kind==word::AbilityKind::shield&&a.shield_amount>input.body.shield&&input.self_shield_value>0)useful=true;
                if(a.kind==word::AbilityKind::damage&&(a.target_bits&word::ground)) {
                    need_ground=true;ground_range=std::max(ground_range,a.range);ground_radius=std::max(ground_radius,a.radius);
                }
            }
            for(const auto& entry:local) {
                if(entry.member>=0&&members[std::size_t(entry.member)].group!=job)continue;
                const auto d=word::distance(input.body.pos,entry.target.pos);
                if(input.body.can_attack&&d<=input.body.range)useful=true;
                for(const auto& a:input.abilities)if(a.ready&&!(a.exclusive&&walked)) {
                    if(a.kind==word::AbilityKind::reset&&input.body.attack_spent&&!input.body.can_attack&&d<=input.body.range)useful=true;
                    if(a.kind==word::AbilityKind::damage&&d<=a.range+a.radius&&
                        ((a.target_bits&word::ground)||(a.target_bits&(entry.enemy?word::enemies:word::camps))))useful=true;
                }
            }
            // Mandatory movement keeps its fuel when no current word can help.
            // A stationary solver is optional work, not a reason to wait.
            if(!useful){diag_micro(2);return applied_any;}
            std::array<word::Target,8> targets{};std::size_t count=0;
            std::array<word::Point,24> centers{};std::size_t centers_count=0;
            for(auto& entry:local) {
                if(count==targets.size())break;auto t=entry.target;
                t.basic_hit=me.strength*(t.kind==word::TargetKind::camp?camp_multiplier:1);
                if(entry.member>=0)t.weight=members[std::size_t(entry.member)].group==job?1.0:0.0;
                for(std::size_t slot=0;slot<2;++slot)t.spell_hit[slot]=damage[slot]*(t.kind==word::TargetKind::camp?camp_multiplier:1);
                targets[count++]=t;
                if(need_ground&&t.weight>0&&word::distance(input.body.pos,t.pos)<=ground_range+ground_radius)
                for(int y=-ground_radius;y<=ground_radius&&centers_count<centers.size();++y)
                    for(int x=-ground_radius;x<=ground_radius&&centers_count<centers.size();++x) {
                    const vec2 p{t.pos.x+x,t.pos.y+y};
                    if(!sees(p)||static_blocked(p)||dist(point(me),p)>ground_range)continue;
                    const word::Point c{p.x,p.y};
                    if(std::find(centers.begin(),centers.begin()+centers_count,c)==centers.begin()+centers_count)centers[centers_count++]=c;
                }
            }
            input.targets={targets.data(),count};input.ground_centers={centers.data(),centers_count};
            input.death_bonus=me.strength*0.25;input.depth=3;input.beam_width=3;input.node_quota=8;
            if(!fuel(40000))break;
            const auto result=word::search(input);
            if(!result.valid||result.first.kind==word::Kind::none||!fuel(14000)){diag_micro(3);break;}
            const auto command=result.first;bool applied=true;
            if(command.kind==word::Kind::basic) {
                if(command.target_index<0||std::size_t(command.target_index)>=local.size())break;
                const auto& target=local[std::size_t(command.target_index)];
                if(target.enemy)self().attack(*target.enemy);else self().attack(members[std::size_t(target.member)].handle);
                attacked=true;
                diag_micro(4);
            } else if(command.kind==word::Kind::reset)applied=abilities[std::size_t(command.slot)].use();
            else if(command.kind==word::Kind::shield)applied=abilities[std::size_t(command.slot)].use(self());
            else if(command.target_index<0)applied=abilities[std::size_t(command.slot)].use(vec2{command.center.x,command.center.y});
            else {
                const auto& target=local[std::size_t(command.target_index)];
                if(target.enemy)applied=abilities[std::size_t(command.slot)].use(*target.enemy);
                else applied=abilities[std::size_t(command.slot)].use(members[std::size_t(target.member)].handle);
            }
            if(!applied){diag_micro(6);break;}
            if(command.kind!=word::Kind::basic)diag_micro(5);
            applied_any=true;last_action_round=epoch;
        }
        // Keep an executable fresh basic incumbent when optional search runs
        // out of its admitted tail, including after an actual reset.
        if(fuel(18000)&&self().can_attack())applied_any=cheap_basic()||applied_any;
        return applied_any;
    }
    bool cheap_basic() {
        if constexpr(cfg::funded_dispatch)if(funded_state().active&&last_action_round!=epoch&&funded_state().probe_kind)
            return funded_execute_work();
        if(!fuel(18000)||!self().can_attack())return false;
        const auto me=body();if(!work_affordable(me))return false;
        const auto sight=vision::visible_tiles();
        int target=-1;float value=-1;
        for(std::size_t i=0;i<members.size()&&fuel(15000);++i) {
            const auto& m=members[i];
            if(dist(point(me),m.pos)>me.attack_range||!sight.is_set(m.pos)||!m.handle.state().is_alive())continue;
            const float useful=std::min(m.handle.health(),me.strength*camp_multiplier)+(m.group==job?1.0f:0.0f);
            if(useful>value){value=useful;target=int(i);}
        }
        if(target<0||!fuel(13000))return false;
        self().attack(members[std::size_t(target)].handle);attacked=true;last_action_round=epoch;return true;
    }
    void bank(const Info& me) {
        const auto locks=vision::altar_locks(team::enemy);bitboard goals;
        for(const auto altar:altars) {
            bool locked=false;
            for(const auto& lock:locks)if(lock.position()==altar)locked=true;
            if(dist(point(me),altar)<=constants::submit_gems_range) {
                if(!locked){self().submit_gems(altar);last_action_round=epoch;return;}
                if(state(me).contains(champion_state::can_attack)) {
                    for(const auto& lock:locks)if(lock.position()==altar){self().attack(lock);attacked=true;last_action_round=epoch;return;}
                }
            }
            goals.draw_chebyshev(altar,constants::submit_gems_range);
        }
        walk(std::move(goals));
    }
    void funded_turn() {
        auto& f=funded_state();f.active=false;f.prepared=false;f.probe_kind=0;
        auto me=body();auto flags=state(me);
        f.failures.enter(std::uint64_t(epoch),f.effect,++f.observation);
        if(flags.is_dead()) {
            diag_stage(1);planned_route()=return_route::Route{};exit_trace.clear();returning=false;return_refusals=0;
            f.navigation.invalidate();shop();recovering=true;task=Task::recover;return;
        }
        if(flags.contains(champion_state::teleporting)){diag_stage(2);exit_trace.clear();return;}
        if(self().buffs().has_stun()){diag_stage(4);return;}
        const auto bag=int(self().gem_count());
        if(bag<last_bag&&bag==0)task=Task::work;last_bag=bag;
        // A retained return is already the mandatory complete-word suffix.
        // Its actual body, current occupancy and horizon-priced damage are
        // refreshed by return_step before optional target collection can spend
        // this input's executable tail. Potential camp support remains the
        // conservative model until a fresh lifecycle receipt refutes it.
        align_exit(me);
        if(returning&&!flags.contains(champion_state::recalling)&&!flags.contains(champion_state::submitting_gems)&&
           exposure(point(me)).amount>0) {
            diag_stage(8);return_step(me);return;
        }
        // Only a pending semantic field is prepared early. An already committed
        // field remains a dated nomination, and no epoch nonce restarts its work.
        if(!returning&&!flags.contains(champion_state::recalling)&&!flags.contains(champion_state::submitting_gems)&&
           state(me).contains(champion_state::can_move)&&!f.navigation.ready()&&
           f.navigation.status()!=funded_navigation::Status::NotRequested)funded_prepare(me);
        collect(me,true);
        if(!fuel(18000)){diag_stage(3);return;}
        me=body();flags=state(me);align_exit(me);
        const bool at_home=home->is_set(point(me));
        if(at_home){shop();me=body();if(me.health<me.max_health){diag_stage(5);return;}
            recovering=false;returning=false;if(task==Task::recover)task=bag?Task::bank:Task::work;}
        const float risk=camp_risk(point(me),job)+enemy_risk(point(me));
        if(flags.contains(champion_state::recalling)){
            if(risk==0){diag_stage(6);return;}self().cancel_recall();me=body();++f.effect;
        }
        if(flags.contains(champion_state::submitting_gems)){
            if(risk*2<me.health){diag_stage(7);return;}self().cancel_submit_gems();me=body();++f.effect;recovering=true;task=Task::recover;
        }
        if(exposure(point(me)).amount==0)returning=false;
        if(returning){diag_stage(8);return_step(me);return;}
        if(!at_home&&leave_before_work(me)){diag_stage(9);return;}
        const bool funded=bag==0&&self().gold_count()>=5*item_type::jungle_shard_of_strength.cost();
        if(!at_home&&(recovering||funded||me.health<me.max_health*0.18f)){
            diag_stage(10);recovering=true;task=Task::recover;retreat(me);return;
        }
        if(bag>0&&(task==Task::bank||bag>=30||me.health<0.6f*me.max_health||epoch>int(constants::max_rounds)-150)){
            diag_stage(11);task=Task::bank;bank(me);return;
        }
        if(job<0||memory[std::size_t(job)].state==3&&epoch<memory[std::size_t(job)].until)job=choose_job(me);
        if(job<0){diag_stage(12);if(bag){task=Task::bank;bank(me);}return;}
        if(memory[std::size_t(job)].state==2&&memory[std::size_t(job)].seen==epoch){
            diag_stage(14);bitboard goals;for(const auto& camp:groups[std::size_t(job)].camps())goals.set(camp.spawn_position());
            if(!goals.is_set(point(me)))walk(std::move(goals));return;
        }
        funded_service(me);
    }
    #include "modeled_return.h"
    #include "joint_contact.h"
    #include "joint_observation.h"
    #include "joint_catalogue_runtime.h"
    #include "joint_execution.h"
    #include "joint_controller.h"
    #include "executable_observation.h"
    #include "executable_execution.h"
    #include "executable_controller.h"
    #include "native_service_facts.h"
    #include "native_service_runtime.h"
    #include "native_service_shop.h"
    #include "native_service_controller.h"
    void turn() {
        const auto native_entry=[] {if constexpr(cfg::native_service)return fuel_remaining();else return std::uint64_t(0);}();
        epoch=int(round_number());expected_epoch=epoch;walked=attacked=false;
        if constexpr(cfg::trace>1){diagnostic().stage=0;diagnostic().work_count=0;diagnostic().unseen=-1;}
        if(!setup())return;
        if constexpr(cfg::native_service){native_service_step(native_entry);native_diagnostic_end();return;}
        if constexpr(cfg::executable_policy){
            if(executable_service_step())return;
            // A partially prepared owner cannot spend the rest of this
            // input in the legacy dispatcher. Its typed handoff starts with
            // a fresh input, preserving actual prefix/refusal obligations.
            if(executable_controller_state().input_started){
                executable_controller_state().legacy_latched=true;
                ++executable_controller_state().deferred_handoffs;return;}
        }
        ExecutableLegacyGuard legacy_guard{this,last_bag,recovering};
        if constexpr(cfg::joint_policy)if(joint_owns_input())return;
        if constexpr(cfg::return_policy){
            if constexpr(cfg::executable_policy){if(executable_legacy_return_owns_input())return;}
            else if(modeled_return_owns_input())return;
        }
        if constexpr(cfg::funded_dispatch){funded_turn();return;}
        auto me=[&]{if constexpr(cfg::executable_policy)return executable_legacy_body_or_query();else return body();}();
        const auto flags=state(me);
        if(flags.is_dead()){
            diag_stage(1);
            if constexpr(cfg::route_exit){planned_route()=return_route::Route{};return_refusals=0;}
            exit_trace.clear();returning=false;shop();recovering=true;task=Task::recover;return;
        }
        if(flags.contains(champion_state::teleporting)){diag_stage(2);exit_trace.clear();return;}
        const auto bag=int(self().gem_count());
        if(bag<last_bag) {if(bag==0)task=Task::work;}
        last_bag=bag;
        collect(me,true);
        if(!fuel(18000)){diag_stage(3);return;}
        if(self().buffs().has_stun()){diag_stage(4);return;}
        align_exit(me);
        const bool at_home=home->is_set(point(me));
        if(at_home) {
            shop();me=body();
            if(me.health<me.max_health){diag_stage(5);return;}
            recovering=false;returning=false;
            if(task==Task::recover)task=bag?Task::bank:Task::work;
        }
        const float current_risk=camp_risk(point(me),job)+enemy_risk(point(me));
        if(flags.contains(champion_state::recalling)) {
            if(current_risk==0){diag_stage(6);return;}
            self().cancel_recall();me=body();
        }
        if(flags.contains(champion_state::submitting_gems)) {
            if(current_risk*2<me.health){diag_stage(7);return;}
            self().cancel_submit_gems();me=body();recovering=true;task=Task::recover;
        }
        // Mandatory exit is priced before optional damage search. A depleted
        // work+exit prefix continues physically rather than waiting on search.
        if(exposure(point(me)).amount==0)returning=false;
        if(returning){diag_stage(8);return_step(me);return;}
        if(!at_home&&leave_before_work(me)){diag_stage(9);return;}
        // Earned capital needs an executable return even at full health.
        // Five affordable shards complete the first family investment.
        const bool funded=bag==0&&self().gold_count()>=5*item_type::jungle_shard_of_strength.cost();
        if(!at_home&&(recovering||funded||me.health<me.max_health*0.18f)) {
            recovering=true;task=Task::recover;
            diag_stage(10);
            // The recovery macro remains executable and does not enter a
            // terminal unsupported state if pressure interrupts it.
            retreat(me);return;
        }
        if(bag>0&&(task==Task::bank||bag>=30||me.health<0.6f*me.max_health||epoch>int(constants::max_rounds)-150)) {
            diag_stage(11);task=Task::bank;bank(me);return;
        }
        if(job<0||memory[std::size_t(job)].state==3&&epoch<memory[std::size_t(job)].until)job=choose_job(me);
        if(job<0){diag_stage(12);if(bag){task=Task::bank;bank(me);}return;}
        if(!fuel(18000)){diag_stage(13);return;}
        if(memory[std::size_t(job)].state==2&&memory[std::size_t(job)].seen==epoch) {
            diag_stage(14);
            bitboard capture;for(const auto& c:groups[std::size_t(job)].camps())capture.set(c.spawn_position());
            if(!capture.is_set(point(me)))walk(std::move(capture));
            return;
        }
        diag_stage(15);const bool acted=micro(true);
        if(!fuel(18000))return;
        me=body();refresh_group(job);
        const auto& memo=memory[std::size_t(job)];
        if(memo.state==2&&memo.seen==epoch) {
            bitboard goals;for(const auto& c:groups[std::size_t(job)].camps())goals.set(c.spawn_position());
            if(goals.is_set(point(me)))return;
            walk(std::move(goals));
        } else {
            bitboard goals;int work_count=0,unseen=-1,nearest=10000;
            for(const auto& m:members)if(m.group==job&&work_candidate(m)) {
                goals.draw_chebyshev(m.pos,std::max(me.attack_range,spell_reach));++work_count;
                if(!sees(m.pos)&&dist(point(me),m.pos)<nearest){nearest=dist(point(me),m.pos);unseen=int(&m-members.data());}
            }
            if constexpr(cfg::trace>1){diagnostic().work_count=work_count;diagnostic().unseen=unseen;}
            diag_stage(16);
            if(!work_count) {
                const auto probe=probe_member(job);
                if(probe>=0){diag_stage(17);observe_ingress(probe,me);}
                else {job=-1;idle=0;return;}
            } else if(!acted&&unseen>=0&&goals.is_set(point(me))) {
                diag_stage(17);
                observe_ingress(unseen,me);
            } else if(walk(std::move(goals))){if(fuel(44000))micro();else cheap_basic();}
        }
        // A ready local spell perch has a finite actual cooldown resolver.
        // Waiting is supported only on complete quiet support, and the first
        // absolute deadline is never renewed by a planning/observation nonce.
        me=body();
        bool cooldown_wait=false;
        if(!acted&&exposure(point(me)).amount==0&&spell_reach>me.attack_range) {
            int remaining=10000;
            for(const auto& a:self().abilities())if((a.is_damage()||a.is_aoe_damage()||a.is_cc())&&a.cooldown()>0)
                remaining=std::min(remaining,int(a.cooldown()));
            if(remaining<10000) {
                if(cast_wait_until<0)cast_wait_until=epoch+remaining;
                cooldown_wait=epoch<cast_wait_until;
            } else cast_wait_until=-1;
        } else if(acted)cast_wait_until=-1;
        // Physical progress, not observation/nonce churn, renews the lease.
        if(point(me)==previous&&last_action_round!=epoch&&!acted&&!cooldown_wait)++idle;else idle=0;
        previous=point(me);
        if(idle>=8) {
            diag_stage(19);
            memory[std::size_t(job)].ban_until=epoch+12;job=-1;idle=0;path_slack=8;observation_member=-1;
            if(bag)task=Task::bank;
        }
    }
};
}
void local_draft() {
    constexpr std::array order{champion_draft::varg,champion_draft::ryth,champion_draft::marcella,
        champion_draft::vendetta,champion_draft::vesper_vale,champion_draft::tavi};
    const auto chosen=draft::current_draft(team::ally);
    for(const auto pick:order)if(std::find(chosen.begin(),chosen.end(),pick)==chosen.end()){draft::pick(pick);return;}
}
void local_turn(){
    static Runtime runtime;runtime.turn();
    if constexpr(cfg::native_service&&cfg::trace<0)runtime.native_late_diagnostic();
    if constexpr(cfg::trace>1&&!cfg::native_service)runtime.diagnostic_end();
}
}
