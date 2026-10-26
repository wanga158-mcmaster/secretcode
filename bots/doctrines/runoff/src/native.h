#pragma once
#include "preparation.h"
#include "receipts.h"
#include "../../reference/src/runtime.h"

namespace p5::runoff::native_binding {
namespace n=p5::native; namespace e=p5::execution; namespace rr=p5::runoff::receipts;
struct Scratch {
    Project project{}; n::Snapshot snapshot{}; Observation fresh{}; Decision choice{};
    std::array<Group,3> shape{};Preparation preparation{};
};
inline constinit Scratch scratch{};
class Bot {
    n::Runtime runtime_{};
    Project& project_=scratch.project; n::Snapshot& snapshot_=scratch.snapshot;
    Observation& fresh_=scratch.fresh; Decision& choice_=scratch.choice;
    std::array<Group,3>& shape_=scratch.shape;Preparation& preparation_=scratch.preparation;
    Profile profile_{}; Ledger ledger_{}; rr::State receipts_{}; rr::Capture capture_{};
    std::array<int,3> ids_{{-1,-1,-1}};
    std::array<cambc::bitboard,2> source_spawns_{};int vacate_source_=-1,map_width_=0;
    int altar_=-1,spawn_=-1,actor_=-1,recovery_round_=-1,previous_spawn_=-1;
    std::uint64_t recovery_=0,cash_attempt_=0;
    bool fixed_=false,running_=false,closed_=false,done_=false,liquidating_=false;
    bool supported_=false,buy_attempt_=false,fallback_pending_=false;
    struct CurrentLeg { n::NodeKind kind=n::NodeKind::current;int id=-1;bool locked=false;RouteLeg value{}; };
    mutable std::array<CurrentLeg,11> current_legs_{};mutable unsigned current_leg_count_=0;
    bool collecting_routes_=false;
    const Observation* geometry_packet_=nullptr;
    std::uint64_t packet_geometry_=0;
    std::array<int,3> packet_ids_{{-1,-1,-1}};
    int packet_actor_=-1,packet_altar_=-1,packet_spawn_=-1,packet_lock_hits_=0;
    e::State liquidation_{};
    int bank_bag_=0,bank_gold_=0,bank_turn_=-1,bank_ticks_=0,bank_altar_=-1;
    std::uint64_t bank_base_=0;
    struct Note { int round=-1,event=0,A=-1,B=-1,R=-1,prefix=0,bag=0,gold=0; Ledger ledger{}; };
    std::array<Note,8> notes_{};unsigned note_count_=0,note_head_=0,note_seen_=0;
    unsigned note_peak_=0;int note_round_=-1;
    // Capped entry-gate samples. Fuel slots are checkpoints, not summed costs:
    // entry, exported packet, route preparation, built menu, chosen job, return.
    struct GateTrace {
        int round=-1,gate=0,flags=0,groups=0;
        std::array<int,6> fuel{{-1,-1,-1,-1,-1,-1}};
        int phase=-1,phase_start=-1,phase_budget=-1,action=-1,reason=-1,moves=-1;
    };
    GateTrace gate_current_{};std::array<GateTrace,24> gate_queue_{};
    unsigned gate_seen_=0,gate_count_=0,gate_head_=0,gate_peak_=0;
    int gate_print_round_=-1;std::array<char,256> gate_buffer_{};
    struct ApplyRecord { n::ApplyTrace trace{}; int round=-1,flags=0; };
    std::array<ApplyRecord,10> apply_queue_{};
    unsigned apply_attempts_=0,apply_seen_=0,apply_count_=0,apply_head_=0,apply_peak_=0;
    int apply_print_round_=-1;
    // Query-only samples of the existing exact worker. Ordinary records leave
    // room for completion and publication; formatting happens after gameplay.
    struct PrepRecord {
        int round=-1,owner=0,outcome=0,calls=0,cursor=0,restarts=0,prefix=-1;
        std::array<PreparationTrace,4> units{};
        std::array<int,4> fuel_before{{-1,-1,-1,-1}},fuel_after{{-1,-1,-1,-1}};
    };
    PrepRecord prep_current_{};std::array<PrepRecord,24> prep_queue_{};
    unsigned prep_count_=0,prep_head_=0,prep_owners_=0,prep_samples_=0,prep_peak_=0;
    std::uint64_t prep_causes_=0,prep_physical_=0;unsigned prep_outcomes_=0;
    int prep_print_round_=-1;
    void record_preparation(int outcome,const n::Snapshot& s,int prefix=-1) {
        if(prep_count_>=prep_queue_.size())return;
        auto& record=prep_current_;record.outcome=outcome;record.prefix=prefix;
        record.cursor=preparation_.cursor;record.restarts=int(preparation_.restarts);
        const unsigned outcome_bit=1u<<unsigned(outcome);
        bool novel=!(prep_outcomes_&outcome_bit);
        auto causes=prep_causes_,physical_fields=prep_physical_;
        for(int i=0;i<record.calls;++i){const auto& x=record.units[i];
            const auto cause=unsigned(x.cause),physical=unsigned(x.physical);
            if(cause<64){const auto bit=std::uint64_t(1)<<cause;novel|=!(causes&bit);causes|=bit;}
            if(physical && physical<64){const auto bit=std::uint64_t(1)<<physical;novel|=!(physical_fields&bit);physical_fields|=bit;}}
        const bool publication=outcome==5,complete=(outcome>=2 && outcome<=4) || outcome==6;
        const unsigned cap=publication?24u:complete?23u:22u;
        if(!publication && !complete && !record.calls)return;
        if(prep_count_>=cap || !(publication || complete || novel || record.owner<=4 || record.owner%64==0))return;
        if(s.round!=record.round)return;
        prep_causes_=causes;prep_physical_=physical_fields;prep_outcomes_|=outcome_bit;
        prep_queue_[prep_count_++]=record;
    }
    bool flush_preparation() {
        if(prep_head_>=prep_count_)return true;
        const int now=int(cambc::round_number());const auto before=cambc::fuel_remaining();
        if(now==prep_print_round_ || now==apply_print_round_ || now==gate_print_round_ ||
            before<(prep_peak_?prep_peak_+8000u:30000u))return true;
        const auto& record=prep_queue_[prep_head_++];prep_print_round_=now;
        std::array<char,768> out{};std::size_t count=0;
        auto text=[&](const char* value){while(*value && count<out.size())out[count++]=*value++;};
        auto number=[&](int value){std::uint64_t n=value<0?std::uint64_t(-std::int64_t(value)):std::uint64_t(value);
            if(value<0 && count<out.size())out[count++]='-';char digits[20];unsigned size=0;
            do{digits[size++]=char('0'+n%10);n/=10;}while(n && size<20);
            while(size && count<out.size())out[count++]=digits[--size];};
        text("P5 runoff-prep a=");number(actor_);text(" r=");number(record.round);text(" owner=");number(record.owner);
        text(" outcome=");number(record.outcome);text(" calls=");number(record.calls);text(" end=");number(record.cursor);
        text("/");number(record.restarts);text(" prefix=");number(record.prefix);
        for(int i=0;i<record.calls;++i){const auto& x=record.units[i];text(" u=");number(i);text(":");
            number(int(x.cause));text("/");number(int(x.physical));text("/");number(x.index);text("/");number(x.detail);
            text(":");number(x.before_cursor);text("/");number(x.after_cursor);text("/");number(x.before_count);text("/");number(x.after_count);
            text(":");number(int(x.began));text("/");number(int(x.quoted));text(":");number(record.fuel_before[i]);text("/");number(record.fuel_after[i]);}
        text("\n");(void)std::fwrite(out.data(),1,count,stdout);const auto after=cambc::fuel_remaining();
        if(now!=int(cambc::round_number()) || before<after){stale_episode();return false;}
        prep_peak_=std::max(prep_peak_,unsigned(before-after));return true;
    }
    void record_apply(const n::ApplyTrace& trace) {
        const auto tag=unsigned(trace.exit);
        if(tag<1 || tag>10 || (apply_seen_&(1u<<tag)) || apply_count_>=apply_queue_.size())return;
        apply_seen_|=1u<<tag;
        apply_queue_[apply_count_++]={trace,snapshot_.round,int(running_) | (int(closed_)<<1) | (int(fixed_)<<2)};
    }
    bool flush_apply() {
        if(apply_head_>=apply_count_)return true;
        const int now=int(cambc::round_number());const auto before=cambc::fuel_remaining();
        if(now==apply_print_round_ || now==gate_print_round_ || now==prep_print_round_ || before<(apply_peak_?apply_peak_+8000u:24000u))return true;
        const auto& record=apply_queue_[apply_head_++];const auto& x=record.trace;apply_print_round_=now;
        std::array<char,384> out{};std::size_t count=0;
        auto text=[&](const char* value){while(*value && count<out.size())out[count++]=*value++;};
        auto natural=[&](std::uint64_t value){char digits[20];unsigned size=0;
            do{digits[size++]=char('0'+value%10);value/=10;}while(value && size<20);
            while(size && count<out.size())out[count++]=digits[--size];};
        auto number=[&](int value){if(value<0 && count<out.size())out[count++]='-';
            natural(value<0?std::uint64_t(-std::int64_t(value)):std::uint64_t(value));};
        text("P5 runoff-apply a=");number(actor_);text(" r=");number(record.round);text(" exit=");number(int(x.exit));
        text(" result=");number(int(x.result));text(" flags=");number(record.flags);text(" rounds=");number(x.entry_round);text("/");number(x.exit_round);
        text(" fuel=");number(x.entry_fuel);text("/");number(x.move_fuel);text("/");number(x.exit_fuel);
        text(" native=");number(int(x.native_called));text(" action=");number(int(x.kind));text("/");number(x.target);text("/");natural(x.sequence);
        text(" feedback=");number(int(x.feedback_kind));text("/");number(x.feedback_target);text("/");natural(x.feedback_sequence);
        text("/");number(int(x.feedback_matches));text("/");number(int(x.feedback_success));text("\n");
        (void)std::fwrite(out.data(),1,count,stdout);
        const auto after=cambc::fuel_remaining();
        if(now!=int(cambc::round_number()) || before<after){stale_episode();return false;}
        apply_peak_=std::max(apply_peak_,unsigned(before-after));return true;
    }
    void gate(int reason,const n::Snapshot& s) {
        if(reason<1 || reason>24 || (gate_seen_&(1u<<reason)) || gate_count_>=gate_queue_.size())return;
        gate_seen_|=1u<<reason;auto& out=gate_queue_[gate_count_++];out=gate_current_;out.gate=reason;
        out.flags=int(running_) | (int(closed_)<<1) | (int(fixed_)<<2) | (int(runtime_.reference_busy())<<3);
        int known=0,available=0;for(int i=0;i<std::min(s.group_count,int(s.groups.size()));++i){known+=s.groups[i].known;available+=usable(s.groups[i]);}
        out.groups=known*16+available;out.fuel[5]=int(cambc::fuel_remaining());
    }
    void dispatch_trace(int reason,const n::Snapshot& s,const e::Decision* action=nullptr,int moves=-1) {
        if(reason<1 || reason>24 || (gate_seen_&(1u<<reason)) || gate_count_>=gate_queue_.size())return;
        gate_current_.fuel[4]=int(cambc::fuel_remaining());
        gate_current_.phase=int(project_.executor.phase);gate_current_.phase_start=project_.executor.phase_start;
        gate_current_.phase_budget=project_.executor.phase_budget;gate_current_.moves=moves;
        gate_current_.action=action?int(action->action.kind):-1;gate_current_.reason=action?int(action->reason):-1;
        gate(reason,s);
    }
    bool flush_gate() {
        if(gate_head_>=gate_count_)return true;
        const int now=int(cambc::round_number());const auto before=cambc::fuel_remaining();
        if(now==gate_print_round_ || now==apply_print_round_ || now==prep_print_round_ || before<(gate_peak_?gate_peak_+8000u:24000u))return true;
        auto& out=gate_buffer_;std::size_t count=0;
        auto text=[&](const char* value){while(*value && count<out.size())out[count++]=*value++;};
        auto number=[&](int value){std::uint64_t n=value<0?std::uint64_t(-std::int64_t(value)):std::uint64_t(value);
            if(value<0 && count<out.size())out[count++]='-';char digits[10];unsigned size=0;
            do{digits[size++]=char('0'+n%10);n/=10;}while(n && size<10);
            while(size && count<out.size())out[count++]=digits[--size];};
        const auto& record=gate_queue_[gate_head_++];gate_print_round_=now;
        text("P5 runoff-gate a=");number(actor_);text(" r=");number(record.round);text(" gate=");number(record.gate);
        text(" flags=");number(record.flags);text(" groups=");number(record.groups);text(" fuel=");
        for(unsigned i=0;i<record.fuel.size();++i){if(i)text("/");number(record.fuel[i]);}
        if(record.phase>=0){text(" phase=");number(record.phase);text("/");number(record.phase_start);text("/");number(record.phase_budget);
            text(" action=");number(record.action);text("/");number(record.reason);text(" moves=");number(record.moves);}
        text("\n");
        (void)std::fwrite(out.data(),1,count,stdout);const auto after=cambc::fuel_remaining();
        if(now!=int(cambc::round_number()) || before<after){stale_episode();return false;}
        gate_peak_=std::max(gate_peak_,unsigned(before-after));return true;
    }
    static const n::GroupFacts* group(const n::Snapshot& s,int id) {
        for(int i=0;i<std::min(s.group_count,int(s.groups.size()));++i)if(s.groups[i].id==id)return &s.groups[i];return nullptr;
    }
    static bool usable(const n::GroupFacts& g) {
        return g.known && !g.respawning && !g.reward && !g.other_holder && g.gems>0 &&
            g.combat.actor_count==1 && g.combat.monster_count>0 && g.combat.monster_count<=8;
    }
    static cambc::item_type item(int family) {
        return family==0?cambc::item_type::monster_shard_of_strength:
            family==1?cambc::item_type::jungle_shard_of_health:cambc::item_type::monster_shard_of_focus;
    }
    static int item_id(int family) { return int(static_cast<std::uint32_t>(item(family))); }
    int equivalents(int family) const {
        if(family<0 || family>2)return -1;
        const auto inventory=cambc::self().item_count();
        const auto emblem=family==0?cambc::item_type::monster_emblem_of_strength:
            family==1?cambc::item_type::jungle_emblem_of_health:cambc::item_type::monster_emblem_of_focus;
        const auto count=std::uint64_t(inventory[item(family)])+5ull*inventory[emblem];
        return count<=std::uint64_t(std::numeric_limits<int>::max())?int(count):-1;
    }
    bool supported_inventory() const {
        const auto inventory=cambc::self().item_count();
        for(std::uint32_t raw=0;raw<inventory.size();++raw) {
            const cambc::item_type v{raw};if(!inventory[v])continue;
            if(v!=cambc::item_type::monster_shard_of_strength && v!=cambc::item_type::monster_emblem_of_strength &&
               v!=cambc::item_type::jungle_shard_of_health && v!=cambc::item_type::jungle_emblem_of_health &&
               v!=cambc::item_type::monster_shard_of_focus && v!=cambc::item_type::monster_emblem_of_focus)return false;
        }return true;
    }
    RouteLeg leg(n::Node from,n::Node to,bool locked=false) const {
        if(collecting_routes_ && from.kind==n::NodeKind::current)
            for(unsigned i=0;i<current_leg_count_;++i){const auto& x=current_legs_[i];if(x.kind==to.kind && x.id==to.id && x.locked==locked)return x.value;}
        auto out=runtime_.route(from,to);
        if(out.distance>=0 && locked)out.distance+=int(cambc::constants::altar_lock_hits);
        out.safety_checked=out.turns()>=0;
        if(collecting_routes_ && from.kind==n::NodeKind::current && current_leg_count_<current_legs_.size())
            current_legs_[current_leg_count_++]={to.kind,to.id,locked,out};
        return out;
    }
    rr::Frame frame(const n::Snapshot& s,bool final=false) const {
        rr::Frame f;f.actor=s.self.id;f.round=s.round;f.bag=s.self.bag.value;f.gold=s.self.gold.value;
        f.current=s.complete && s.settings_valid && s.round==int(cambc::round_number());
        f.alive=s.alive;f.submitting=s.channel==e::Channel::tribute;f.supported=supported_ && !buy_attempt_;
        f.passive_gold=int(cambc::constants::passive_gold);f.capture=s.receipts.capture;f.bank=s.receipts.bank;
        f.purchase=s.receipts.purchase;f.cash_attempt=cash_attempt_;f.capture_round=s.receipts.capture_round;
        f.capture_group=s.receipts.capture_group;f.capture_gems=s.receipts.capture_gems;f.bank_round=s.receipts.bank_round;
        if(final && f.current) {
            auto& me=cambc::self();f.bag=int(me.gem_count());f.gold=int(me.gold_count());
            f.alive=me.is_alive();f.submitting=me.is_submitting_gems();
            f.current=s.round==int(cambc::round_number());
        }return f;
    }
    void note(int event,const n::Snapshot& s,int prefix=0) {
        if(event<1 || event>8 || (note_seen_&(1u<<event)) || note_count_>=notes_.size())return;
        note_seen_|=1u<<event;notes_[note_count_++]={s.round,event,ids_[0],ids_[1],ids_[2],prefix,s.self.bag.value,s.self.gold.value,ledger_};
    }
    bool flush_note() {
        if(note_head_>=note_count_)return true;
        const int now=int(cambc::round_number());const auto before=cambc::fuel_remaining();
        if(now==note_round_ || before<(note_peak_?note_peak_+16000u:80000u))return true;
        const auto& x=notes_[note_head_++];note_round_=now;
        std::printf("P5 runoff a=%d r=%d event=%d arm=%d A=%d B=%d R=%d prefix=%d bag=%d gold=%d ledger=%d/%d/%d/%u\n",
            actor_,x.round,x.event,int(profile_.arm),x.A,x.B,x.R,x.prefix,x.bag,x.gold,x.ledger.captures,x.ledger.banks,x.ledger.purchases,x.ledger.consumed);
        const auto after=cambc::fuel_remaining();
        if(now!=int(cambc::round_number()) || before<after){stale_episode();return false;}
        note_peak_=std::max(note_peak_,unsigned(before-after));return true;
    }
    bool choose_frontier(const n::Snapshot& s) {
        std::array<int,8> order{};int count=0;
        for(int i=0;i<s.group_count;++i)if(usable(s.groups[i]) && leg({}, {n::NodeKind::group_entry,s.groups[i].id}).turns()>=0)order[count++]=i;
        std::sort(order.begin(),order.begin()+count,[&](int a,int b){
            const int x=leg({}, {n::NodeKind::group_entry,s.groups[a].id}).distance;
            const int y=leg({}, {n::NodeKind::group_entry,s.groups[b].id}).distance;
            return x==y?s.groups[a].id<s.groups[b].id:x<y;
        });
        if(count<3)return false;
        int best=-1,distance=1000000;
        for(int i=0;i<s.altar_count;++i){const auto path=leg({}, {n::NodeKind::altar_entry,s.altars[i].tile_id},true);
            if(path.turns()>=0 && path.distance<distance){best=s.altars[i].tile_id;distance=path.distance;}}
        if(best<0)return false;altar_=best;spawn_=leg({}, {n::NodeKind::spawn,-1}).destination;
        if(spawn_<0)return false;
        for(int i=0;i<3;++i)ids_[i]=s.groups[order[i]].id;
        map_width_=cambc::map::width();
        return map_width_>0 && runtime_.group_spawn_mask(ids_[0],source_spawns_[0]) &&
            runtime_.group_spawn_mask(ids_[1],source_spawns_[1]);
    }
    void copy_shape(Group& out,const n::GroupFacts& g) const {
        out={};out.id=g.id;out.gems=g.gems;out.gold=g.gold;out.count=g.combat.monster_count;
        for(std::size_t i=0;i<out.count && i<out.monsters.size();++i)out.monsters[i]=g.combat.monsters[i];
    }
    void stale_episode() noexcept {
        const bool owns_episode=running_ || liquidating_ || ledger_.captures || ledger_.banks || ledger_.purchases || snapshot_.self.bag.value>0;
        rr::invalidate(receipts_);capture_={};preparation_.reset();geometry_packet_=nullptr;fallback_pending_=false;running_=false;closed_=closed_ || owns_episode;done_=false;
        project_.intent=0;project_.stage=Stage::reobserve;
    }
    bool current(const n::Snapshot& s) {
        if(s.round==int(cambc::round_number()))return true;stale_episode();return false;
    }
    void fallback(const n::Snapshot& s) {
        runtime_.reference_turn(&s);
    }
    void liquidate(const n::Snapshot& s) {
        if(!liquidating_){liquidation_={};liquidating_=true;bank_turn_=-1;note(7,s);}
        running_=false;closed_=true;
    }
    n::Goal liquidation_goal(const n::Snapshot& s) const {
        n::Goal g;g.kind=n::GoalKind::altar;g.altar_tile_id=altar_;g.phase=liquidation_.phase;g.bank_requested=true;
        const int distance=leg({}, {n::NodeKind::altar_entry,altar_},true).turns();
        const int channel=s.settings.channel_ticks(s.self.bag.value);
        g.forecast_turns=distance<0 || channel<0?-1:distance+channel;return g;
    }
    void dispatch_liquidation(n::Snapshot& s) {
        if(s.self.bag.value<=0) {
            const bool receipt=bank_turn_>=0 && s.receipts.bank>bank_base_ && s.receipts.bank_round==s.round &&
                std::int64_t(s.self.gold.value)==std::int64_t(bank_gold_)+std::int64_t(bank_bag_)*s.settings.marcella_gold_per_gem && s.round>=bank_turn_+bank_ticks_ && bank_altar_==altar_ && s.alive;
            if(receipt){if(ledger_.banks<2)++ledger_.banks;if(profile_.arm==Arm::finite_harvest)ledger_.liquidated=true;note(4,s);}
            liquidating_=false;bank_turn_=-1;running_=false;const auto* next=fixed_?group(s,ids_[2]):nullptr;done_=!receipt || !next || !usable(*next);if(done_)fallback(s);return;
        }
        // A fresh unpaid capture is target-independent. Ending the owner turn
        // preserves it without handing the bag or episode to another controller.
        if(s.safe_hold)return;
        const bool prepared=runtime_.prepare_selection();if(!current(s))return;
        if(!prepared){fallback(s);return;}
        if(altar_<0){int distance=1000000;for(int i=0;i<s.altar_count;++i){auto p=leg({}, {n::NodeKind::altar_entry,s.altars[i].tile_id},true);if(p.turns()>=0 && p.distance<distance){altar_=s.altars[i].tile_id;distance=p.distance;}}}
        auto goal=liquidation_goal(s);auto o=runtime_.execution_observation(liquidation_,goal);
        if(!current(s))return;
        if(o.channel!=e::Channel::none){(void)runtime_.apply(e::decision(o,liquidation_),o,liquidation_,goal);return;}
        if(!o.alive || !o.safe || o.needs_recovery || goal.forecast_turns<0){fallback(s);return;}
        if(liquidation_.phase!=e::Phase::bank){e::enter(liquidation_,e::Phase::bank,o);return;}
        o.bank_requested=true;runtime_.add_moves(o,liquidation_,goal);
        if(!current(s))return;
        filter_exit_moves(s,o);if(!current(s))return;
        e::consume_feedback(o,liquidation_);
        auto action=on_captured_region(s)?e::move_intent(o,liquidation_,e::ActionKind::vacate):e::decision(o,liquidation_);
        if(action.action.kind==e::ActionKind::start_tribute){bank_bag_=s.self.bag.value;bank_gold_=s.self.gold.value;bank_turn_=s.round;bank_ticks_=action.action.channel_ticks;bank_base_=s.receipts.bank;bank_altar_=altar_;}
        const auto result=runtime_.apply(action,o,liquidation_,goal);
        if(result!=n::ApplyResult::acted && action.action.kind==e::ActionKind::start_tribute)bank_turn_=-1;
    }
public:
    explicit Bot(Arm arm):profile_{arm}{}
    bool build_into(const n::Snapshot& s,Observation& out) {
        geometry_packet_=nullptr;
        if(s.group_count<0 || s.group_count>int(s.groups.size()) || s.altar_count<0 || s.altar_count>int(s.altars.size()) || !s.complete || !s.settings_valid || !s.alive || s.type!=cambc::champion_type::marcella ||
            s.round!=int(cambc::round_number()) || cambc::constants::passive_gold!=0 || !std::isfinite(s.route_stress) || s.route_stress<0)return false;
        // Route point facts are reused only inside this query-only build. A
        // final owner-round check rejects a suspension before publishing them.
        struct EndRoutes {bool& flag;~EndRoutes(){flag=false;}} end_routes{collecting_routes_};
        current_leg_count_=0;collecting_routes_=true;
        if(!fixed_ && !choose_frontier(s))return false;
        out={};out.round=s.round;out.team_score=s.team_score;out.actual_marcella=true;out.resources=s.self;
        out.actor=s.stats;out.actor.shield=0;out.actor.shield_expiry_tick=0;
        out.actor.spell_known_ready=out.actor.legal_first_cast=out.actor.reserved_cast=out.actor.heal_legal_ready=false;
        out.actor.direct_or_reset_damage=out.actor.heal_amount=0;out.actor.visible_enemy_next_phase=s.route_stress;
        out.settings=s.settings;out.windows=s.windows;out.ledger=ledger_;
        out.current_to_altar=leg({}, {n::NodeKind::altar_entry,altar_},true);
        out.current_to_spawn=leg({}, {n::NodeKind::spawn,-1});out.altar_to_spawn=leg({n::NodeKind::altar_entry,altar_},{n::NodeKind::spawn,-1});
        out.bank_safe=out.current_to_altar.turns()>=0;out.spawn_safe=s.route_stress==0 && out.current_to_spawn.turns()>=0;
        const double rate=double(cambc::constants::spawn_healing);
        if(!std::isfinite(rate) || rate<=0 || rate>1)return false;
        const double rest=std::ceil(1/rate);if(!std::isfinite(rest) || rest>32)return false;out.spawn_rest=std::max(1,int(rest));
        out.spawn_heal_per_tick=out.actor.max_health*rate;
        for(int i=0;i<3;++i){
            const auto* actual=group(s,ids_[i]);const bool live=actual && usable(*actual);
            if(!fixed_){if(!live)return false;copy_shape(shape_[i],*actual);}
            auto& g=out.groups[i];g=shape_[i];
            if(live && !(i<2 && (ledger_.consumed&(1u<<i))))copy_shape(g,*actual);
            else for(std::size_t m=0;m<g.count;++m){g.monsters[m].visible=false;g.monsters[m].observed_health=0;}
            g.current=actual && actual->known;g.available=live && !(i<2 && ((ledger_.consumed&(1u<<i)) || closed_));g.no_other_holder=actual && !actual->other_holder;
            g.from_current=leg({}, {n::NodeKind::group_entry,g.id});g.from_altar=leg({n::NodeKind::altar_entry,altar_},{n::NodeKind::group_entry,g.id});
            g.from_spawn=leg({n::NodeKind::spawn,-1},{n::NodeKind::group_entry,g.id});g.to_altar=leg({n::NodeKind::group_entry,g.id},{n::NodeKind::altar_entry,altar_},true);
            g.to_spawn=leg({n::NodeKind::group_entry,g.id},{n::NodeKind::spawn,-1});g.certificate_return=g.from_altar;
        }
        out.A_to_B=leg({n::NodeKind::group_entry,ids_[0]},{n::NodeKind::group_entry,ids_[1]});
        for(const auto& qp:s.purchases)if(qp.purchasable && qp.family>=0 && qp.family<3 && out.purchase_count<purchase_cap){
            auto& p=out.purchases[out.purchase_count++];p.item=qp.item_id;p.family=qp.family;p.cost=qp.cost;p.shards=1;
            p.strength=qp.strength_gain;p.health=qp.health_gain;p.spawn_heal=qp.health_gain;p.basic_ratio=1;p.legal_on_spawn=true;p.no_new_income_passive=true;
        }
        out.execution.owner_turn=s.round;out.execution.remaining_rounds=std::max(0,s.settings.round_limit-s.round);
        out.execution.fuel=s.fuel;out.execution.alive=s.alive;out.execution.safe=s.alive && safe(out.actor);
        out.execution.channel=s.channel;out.execution.channel_valid=s.channel!=e::Channel::none;out.execution.channel_safe=s.channel_safe;
        if(!current(s))return false;
        geometry_packet_=&out;packet_geometry_=runtime_.geometry_generation();packet_ids_=ids_;
        packet_actor_=s.self.id;packet_altar_=altar_;packet_spawn_=spawn_;packet_lock_hits_=int(cambc::constants::altar_lock_hits);
        return true;
    }
    bool geometry_current(const n::Snapshot& s,const Observation& out) const {
        if(geometry_packet_!=&out || packet_geometry_!=runtime_.geometry_generation() || packet_ids_!=ids_ ||
            packet_actor_!=s.self.id || packet_altar_!=altar_ || packet_spawn_!=spawn_ ||
            packet_lock_hits_!=int(cambc::constants::altar_lock_hits))return false;
        for(int i=0;i<3;++i)if(out.groups[i].id!=ids_[i])return false;
        return true;
    }
    bool refresh_running(const n::Snapshot& s,Observation& out) {
        if(!fixed_ || !running_ || project_.pending_replan || !geometry_current(s,out) ||
            s.group_count<0 || s.group_count>int(s.groups.size()) || s.altar_count<0 || s.altar_count>int(s.altars.size()) ||
            !s.complete || !s.settings_valid || !s.alive || s.type!=cambc::champion_type::marcella ||
            s.round!=int(cambc::round_number()) || cambc::constants::passive_gold!=0 || !std::isfinite(s.route_stress) || s.route_stress<0){
            geometry_packet_=nullptr;return false;
        }
        // Only named-node terrain routes survive. Every private/live field and
        // every route from the current actor position is replaced this owner.
        struct EndRoutes {bool& flag;~EndRoutes(){flag=false;}} end_routes{collecting_routes_};
        current_leg_count_=0;collecting_routes_=true;
        out.round=s.round;out.team_score=s.team_score;out.actual_marcella=true;out.resources=s.self;
        out.actor=s.stats;out.actor.shield=0;out.actor.shield_expiry_tick=0;
        out.actor.spell_known_ready=out.actor.legal_first_cast=out.actor.reserved_cast=out.actor.heal_legal_ready=false;
        out.actor.direct_or_reset_damage=out.actor.heal_amount=0;out.actor.visible_enemy_next_phase=s.route_stress;
        out.settings=s.settings;out.windows=s.windows;out.ledger=ledger_;
        out.current_to_altar=leg({}, {n::NodeKind::altar_entry,altar_},true);
        out.current_to_spawn=leg({}, {n::NodeKind::spawn,-1});
        out.bank_safe=out.current_to_altar.turns()>=0;out.spawn_safe=s.route_stress==0 && out.current_to_spawn.turns()>=0;
        const double rate=double(cambc::constants::spawn_healing);
        if(!std::isfinite(rate) || rate<=0 || rate>1){geometry_packet_=nullptr;return false;}
        const double rest=std::ceil(1/rate);
        if(!std::isfinite(rest) || rest>32){geometry_packet_=nullptr;return false;}
        out.spawn_rest=std::max(1,int(rest));out.spawn_heal_per_tick=out.actor.max_health*rate;
        for(int i=0;i<3;++i){
            const auto* actual=group(s,ids_[i]);const bool live=actual && usable(*actual);
            auto& g=out.groups[i];const auto& original=shape_[i];
            if(live && !(i<2 && (ledger_.consumed&(1u<<i)))){
                g.id=actual->id;g.gems=actual->gems;g.gold=actual->gold;g.count=actual->combat.monster_count;
                for(std::size_t m=0;m<g.count;++m)g.monsters[m]=actual->combat.monsters[m];
            }else{
                g.id=original.id;g.gems=original.gems;g.gold=original.gold;g.count=original.count;
                for(std::size_t m=0;m<g.count;++m){g.monsters[m]=original.monsters[m];g.monsters[m].visible=false;g.monsters[m].observed_health=0;}
            }
            g.current=actual && actual->known;g.available=live && !(i<2 && ((ledger_.consumed&(1u<<i)) || closed_));g.no_other_holder=actual && !actual->other_holder;
            g.from_current=leg({}, {n::NodeKind::group_entry,g.id});
        }
        // Running execution uses the selected purchase, never a new catalogue.
        // Pending replans unconditionally take build_into before preparation.
        out.purchase_count=0;out.A_to_B={};out.execution={};
        out.execution.owner_turn=s.round;out.execution.remaining_rounds=std::max(0,s.settings.round_limit-s.round);
        out.execution.fuel=s.fuel;out.execution.alive=s.alive;out.execution.safe=s.alive && safe(out.actor);
        out.execution.channel=s.channel;out.execution.channel_valid=s.channel!=e::Channel::none;out.execution.channel_safe=s.channel_safe;
        if(!current(s))return false;
        return true;
    }
    void start(const Decision& chosen) {
        // Publish into the retained project without constructing and copying a
        // second whole Project. Every non-decision field is reset explicitly.
        project_.selected=chosen;project_.executor={};project_.stage=chosen.feasible?Stage::running:Stage::aborted;project_.ledger=ledger_;
        project_.index=project_.bag=project_.gold=project_.bank_ticks=0;project_.intent_turn=project_.arrival=project_.item_count=-1;
        project_.capture_base=project_.bank_base=project_.purchase_base=project_.recovery_base=project_.intent=0;
        project_.initialized=project_.must_vacate=project_.pending_replan=false;running_=project_.stage==Stage::running;
        if(running_)fixed_=true;
        project_.executor.consumed_payout=capture_.exact?capture_.receipt:0;
    }
    n::Goal goal() const {
        n::Goal out;out.phase=project_.executor.phase;out.altar_tile_id=altar_;out.controller_stage=int(project_.stage);
        const auto& q=project_.selected.selected;if(project_.index<0 || project_.index>=q.count)return out;
        const auto& step=q.steps[project_.index];out.group_id=step.group;out.forecast_turns=std::max(1,step.duration);
        if(project_.initialized)out.forecast_turns=std::max(1,std::min(out.forecast_turns,project_.executor.phase_budget));
        if(step.kind==StepKind::produce){out.production=true;out.kind=project_.executor.phase==e::Phase::hold?n::GoalKind::camp_spawn:project_.executor.phase==e::Phase::clear?n::GoalKind::camp_attack:n::GoalKind::camp_entry;}
        else if(step.kind==StepKind::bank){out.kind=n::GoalKind::altar;out.bank_requested=true;}
        else if(step.kind==StepKind::renew)out.kind=n::GoalKind::productive_entry;
        else {out.kind=n::GoalKind::spawn;
            if(step.kind==StepKind::shop && step.purchase>=0 && step.purchase<project_.selected.input.purchase_count)out.purchase_family=project_.selected.input.purchases[step.purchase].family;}
        return out;
    }
    bool wants_moves(const n::Snapshot& s,const e::Observation& o,const n::Goal& g) const {
        if(!o.alive || !o.safe || !o.can_act || !o.can_move || o.channel!=e::Channel::none || project_.intent)return false;
        // Receipt admission is mandatory before optional route work. Preparing
        // movement never advances the project or authorizes an SDK action.
        if(capture_.exact && capture_.round==s.round && capture_.receipt>project_.executor.consumed_payout)return false;
        if(project_.must_vacate && on_captured_region(s))return true;
        switch(project_.executor.phase){
        case e::Phase::travel:return !o.at_target || o.group_id!=g.group_id;
        case e::Phase::clear:return o.group_id==g.group_id && o.group_live_known && !o.group_respawning && !o.group_cleared && !e::attack_legal(o);
        case e::Phase::hold:return !o.on_group_spawn;
        case e::Phase::bank:return !o.adjacent_altar;
        case e::Phase::shop:case e::Phase::recover:return !o.at_spawn;
        case e::Phase::renew:return true;
        default:return false;
        }
    }
    bool on_captured_region(const n::Snapshot& s) const {
        return vacate_source_>=0 && vacate_source_<2 && map_width_>0 && s.position_tile_id>=0 &&
            source_spawns_[vacate_source_].is_set({s.position_tile_id%map_width_,s.position_tile_id/map_width_});
    }
    void filter_exit_moves(const n::Snapshot& s,e::Observation& observed) const {
        if(!on_captured_region(s))return;std::size_t count=0;
        for(std::size_t i=0;i<observed.move_count;++i){const int tile=observed.moves[i].destination;
            if(tile>=0 && !source_spawns_[vacate_source_].is_set({tile%map_width_,tile/map_width_}))observed.moves[count++]=observed.moves[i];}
        observed.move_count=count;
    }
    Facts facts(const n::Snapshot& s,const e::Observation& observed) {
        Facts f;f.capture_receipt=capture_.exact?capture_.receipt:0;f.bank_receipt=s.receipts.bank;f.purchase_receipt=s.receipts.purchase;
        f.recovery_receipt=recovery_;f.recovery_round=recovery_round_;
        const auto& q=project_.selected.selected;if(project_.index<0 || project_.index>=q.count)return f;
        const auto& step=q.steps[project_.index];f.navigation_round=s.round;f.navigation_goal=step.waypoint;
        f.captured_region_known=vacate_source_>=0;f.on_captured_region=on_captured_region(s);
        f.moves_leave_region=f.on_captured_region && observed.move_count>0;
        if(f.captured_region_known && !f.on_captured_region)vacate_source_=-1;
        if(step.kind==StepKind::bank && project_.intent && s.receipts.bank>project_.bank_base && s.receipts.bank_round==s.round && s.self.bag.value==0 &&
            std::int64_t(s.self.gold.value)==std::int64_t(project_.gold)+std::int64_t(project_.bag)*s.settings.marcella_gold_per_gem){f.bank_confirmed=true;f.banked_gems=project_.bag;f.bank_gold=s.self.gold.value-project_.gold;f.bank_altar=step.waypoint;}
        if(step.kind==StepKind::shop && step.purchase>=0 && step.purchase<project_.selected.input.purchase_count) {
            const auto& p=project_.selected.input.purchases[step.purchase];f.gear_item=p.item;f.shard_equivalent_count=equivalents(p.family);
            f.purchase_confirmed=s.receipts.purchase_round==s.round && s.receipts.purchase_count==1;
            f.purchased_item=s.receipts.purchase_item;f.purchased_family=s.receipts.purchase_family;f.purchased_cost=s.receipts.purchase_cost;f.purchased_shards=s.receipts.purchase_count;
        }
        if(step.kind==StepKind::renew){f.at_productive=runtime_.productive_ready(step.group);f.productive_group=step.group;f.productive_round=s.round;}
        return f;
    }
    e::Decision advance(const n::Snapshot& s,const e::Observation& observed) {
        fresh_.execution=observed;filter_exit_moves(s,fresh_.execution);
        if(capture_.exact && capture_.round==s.round){fresh_.execution.own_payout_observed=true;fresh_.execution.own_payout_group=capture_.group;
            fresh_.execution.own_gems_received=capture_.gems;fresh_.execution.own_gold_received=capture_.gold;fresh_.execution.payout_receipt=capture_.receipt;}
        else {fresh_.execution.own_payout_observed=false;fresh_.execution.payout_receipt=0;}
        const auto f=facts(s,fresh_.execution);if(!current(s))return e::emit(project_.executor,e::ActionKind::refresh);const auto before=ledger_;
        const bool was_pending=project_.pending_replan;const auto out=execute_job(fresh_,f,project_,!project_.initialized,true);ledger_=project_.ledger;
        if(!was_pending && project_.pending_replan)preparation_.reset();
        if(ledger_.captures>before.captures)for(int i=0;i<2;++i)if(ids_[i]==capture_.group)vacate_source_=i;
        if(!current(s))return e::emit(project_.executor,e::ActionKind::refresh);
        if(ledger_.captures>before.captures)note(ledger_.captures==1?2:3,s);
        if(ledger_.banks>before.banks)note(4,s);if(ledger_.purchases>before.purchases)note(5,s);
        if(project_.stage==Stage::complete){done_=true;running_=false;note(6,s);}
        return out;
    }
    void turn() {
        if(cambc::self().champion_name()!=cambc::champion_type::marcella){runtime_.reference_turn();return;}
        if(!flush_note() || !runtime_.begin_turn())return;
        auto& s=snapshot_;buy_attempt_=false;gate_current_={};gate_current_.round=int(cambc::round_number());gate_current_.fuel[0]=int(cambc::fuel_remaining());
        if(!runtime_.snapshot_into(s) || !s.complete || !current(s)){gate(1,s);stale_episode();return;}
        gate_current_.fuel[1]=int(cambc::fuel_remaining());
        actor_=s.self.id;supported_=supported_inventory();capture_=rr::observe(frame(s),receipts_);
        if(s.at_spawn && s.alive && previous_spawn_==s.round-1 && s.stats.health>=s.stats.max_health){++recovery_;recovery_round_=s.round;}
        previous_spawn_=s.at_spawn && s.alive?s.round:-1;
        if(!s.alive || !s.settings_valid){running_=false;closed_=true;rr::invalidate(receipts_);return;}
        if(s.safe_hold){gate(2,s);finish(s);return;}
        if(s.channel!=e::Channel::none && s.channel_safe){finish(s);return;}
        if(s.self.bag.value>0 && (closed_ || std::int64_t(s.team_score)+s.self.bag.value>=s.settings.winning_gems))liquidate(s);
        if(liquidating_){gate(3,s);dispatch_liquidation(s);finish(s);return;}
        if(done_){gate(4,s);fallback(s);finish(s);return;}
        if(fallback_pending_){fallback_pending_=false;preparation_.reset();fallback(s);finish(s);return;}
        if(!running_ && runtime_.reference_busy()){gate(5,s);fallback(s);finish(s);return;}
        const bool prepared=runtime_.prepare_selection();if(!current(s))return;
        if(!prepared){gate(6,s);fallback(s);finish(s);return;}
        gate_current_.fuel[2]=int(cambc::fuel_remaining());
        s.fuel=int(cambc::fuel_remaining());
        const bool built=running_ && !project_.pending_replan && geometry_current(s,fresh_)?refresh_running(s,fresh_):build_into(s,fresh_);if(!current(s))return;
        gate_current_.fuel[3]=int(cambc::fuel_remaining());
        if(!built){gate(7,s);if(s.self.bag.value>0){liquidate(s);dispatch_liquidation(s);}else fallback(s);finish(s);return;}
        if(!running_ || project_.pending_replan){
            if(cambc::fuel_remaining()<30000){gate(8,s);finish(s);return;}
            const PreparationDomain domain{s.position_tile_id,runtime_.geometry_generation()};
            const int before=preparation_.cursor;auto status=PreparationStatus::incomplete;
            // At most48 sampled owners: the first16, then every32nd owner.
            // Once ordinary slots fill, retain only cheap completion summaries.
            const auto owner=++prep_owners_;
            const bool tracing=prep_count_<22 && prep_samples_<48 && (owner<=16 || owner%32==0);
            if(prep_count_<prep_queue_.size()){prep_current_={};prep_current_.round=s.round;prep_current_.owner=int(owner);}
            if(tracing)++prep_samples_;
            // Short exact units can share this current owner packet. Retain
            // the original entry floor before each unit and publish only the
            // fully compared catalogue; never carry a partial winner forward.
            for(unsigned unit=0;unit<4;++unit){
                const auto fuel=cambc::fuel_remaining();if(fuel<30000)break;
                if(tracing)prep_current_.fuel_before[unit]=int(fuel);
                status=preparation_.advance(fresh_,profile_,domain,tracing?&prep_current_.units[unit]:nullptr);
                if(!current(s))return;
                if(tracing){prep_current_.fuel_after[unit]=int(cambc::fuel_remaining());++prep_current_.calls;}
                if(status!=PreparationStatus::incomplete)break;
            }
            gate_current_.fuel[4]=int(cambc::fuel_remaining());if(!current(s))return;
            if(status==PreparationStatus::incomplete){record_preparation(0,s);gate(preparation_.cursor==before?13:14,s);finish(s);return;}
            if(status==PreparationStatus::invalid){record_preparation(1,s);gate(9,s);note(8,s);if(running_)closed_=true;running_=false;
                if(s.self.bag.value>0)liquidate(s);else fallback_pending_=true;finish(s);return;}
            if(cambc::fuel_remaining()<20000){record_preparation(2,s);gate(15,s);finish(s);return;}
            if(preparation_.publish_into(fresh_,choice_,domain)!=PreparationStatus::complete){record_preparation(3,s);finish(s);return;}
            if(!current(s))return;
            if(!choice_.feasible){record_preparation(6,s);gate(9,s);note(8,s);if(running_)closed_=true;running_=false;
                if(s.self.bag.value>0)liquidate(s);else fallback_pending_=true;finish(s);return;}
            if(cambc::fuel_remaining()<12000){record_preparation(4,s);gate(15,s);finish(s);return;}
            if(running_ && project_.pending_replan){install_replan(project_,choice_);running_=project_.stage==Stage::running;}
            else {start(choice_);note(1,s,choice_.selected.prefix);}
            record_preparation(5,s,choice_.selected.prefix);preparation_.reset();gate(10,s);finish(s);return;
        }
        if(!project_.initialized && s.channel==e::Channel::none){
            if(cambc::fuel_remaining()<16000){finish(s);return;}
            (void)advance(s,fresh_.execution);if(!current(s))return;
            if(project_.stage==Stage::reobserve || project_.stage==Stage::aborted){running_=false;closed_=true;
                if(s.self.bag.value>0)liquidate(s);else fallback_pending_=true;}
            gate(16,s);finish(s);return;
        }
        // One typed observation per owner turn. A phase/goal change ends this
        // unit; no old target packet can authorize the next phase's action.
        if(cambc::fuel_remaining()<30000){dispatch_trace(21,s);finish(s);return;}
        const auto before=goal();auto observed=runtime_.execution_observation(project_.executor,before);
        if(!current(s))return;dispatch_trace(17,s,nullptr,int(observed.move_count));
        // Query the current typed navigation goal before its single policy
        // evaluation. Repeating advance after routing duplicated the full tail
        // proof and left Runtime below its unchanged 30k navigation reserve.
        if(wants_moves(s,observed,before)){
            if(cambc::fuel_remaining()>=30000){
                runtime_.add_moves(observed,project_.executor,before);if(!current(s))return;
                dispatch_trace(22,s,nullptr,int(observed.move_count));
            }else dispatch_trace(23,s,nullptr,int(observed.move_count));
        }
        auto action=advance(s,observed);if(!current(s))return;dispatch_trace(18,s,&action,int(observed.move_count));
        if(project_.stage==Stage::reobserve || project_.stage==Stage::aborted){running_=false;closed_=true;
            if(s.self.bag.value>0)liquidate(s);else fallback_pending_=true;dispatch_trace(20,s,&action,int(observed.move_count));finish(s);return;}
        if(!running_ || project_.pending_replan || !(goal()==before)){dispatch_trace(19,s,&action,int(observed.move_count));finish(s);return;}
        if(cambc::fuel_remaining()<16000){
            dispatch_trace(24,s,&action,int(observed.move_count));
            // No SDK call has consumed this intent. Leave receipt baselines and
            // sequence monotonicity intact, but do not wait for an unstarted action.
            if(action.action.kind==e::ActionKind::start_tribute || action.action.kind==e::ActionKind::buy){project_.intent=0;project_.intent_turn=-1;project_.bank_ticks=0;}
            finish(s);return;
        }
        auto physical=action;
        n::ApplyTrace apply_trace;const bool tracing=apply_attempts_<32;
        if(tracing)++apply_attempts_;
        n::ApplyBudgetDeferral budget_deferral;
        n::ApplyResult result;
        if(action.action.kind==e::ActionKind::buy){
            ++cash_attempt_;buy_attempt_=true;physical.action.target=before.purchase_family;
            auto bound=observed;bound.purchase_id=before.purchase_family;
            result=runtime_.apply(physical,bound,project_.executor,before,tracing?&apply_trace:nullptr,&budget_deferral);
        }else{
            // Only purchases rewrite packet fields. Other actions retain this
            // owner-local observation directly instead of cloning the packet.
            result=runtime_.apply(physical,observed,project_.executor,before,tracing?&apply_trace:nullptr,&budget_deferral);
        }
        if(tracing)record_apply(apply_trace);
        if(budget_deferral!=n::ApplyBudgetDeferral::none){
            // No native action began. Retry from a fresh packet next owner,
            // retaining the original deadline, ledger and monotonic sequences.
            if(action.action.kind==e::ActionKind::start_tribute || action.action.kind==e::ActionKind::buy){project_.intent=0;project_.intent_turn=-1;project_.bank_ticks=0;}
            buy_attempt_=false;
            if(!current(s))return;dispatch_trace(11,s,&action,int(observed.move_count));finish(s);return;
        }
        const bool harmless=action.action.kind==e::ActionKind::yield || action.action.kind==e::ActionKind::refresh || action.action.kind==e::ActionKind::inspect || action.action.kind==e::ActionKind::preserve_channel || action.action.kind==e::ActionKind::hold;
        if(result==n::ApplyResult::stale || (result==n::ApplyResult::stopped && !harmless)){
            running_=false;closed_=true;if(s.self.bag.value>0)liquidate(s);else fallback_pending_=true;}
        if(!current(s))return;dispatch_trace(11,s,&action,int(observed.move_count));
        finish(s);
    }
    void finish(const n::Snapshot& s) {
        runtime_.finish_turn();if(!current(s))return;
        (void)rr::finalize(frame(s,true),receipts_);if(!current(s))return;
        if(!flush_apply() || !flush_gate())return;(void)flush_preparation();
    }
    template<typename Function> void guarded(Function&& function) {
        if(!runtime_.guard_turn(static_cast<Function&&>(function)))stale_episode();
    }
};
inline int run(Arm arm) {
    static Bot bot(arm);
    while(true){bot.guarded([&]{if(cambc::is_draft())n::Runtime::draft(Profile::draft_request({}));else bot.turn();});cambc::end_turn();}
}
} // namespace p5::runoff::native_binding
