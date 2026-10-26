#pragma once
// Diagnostic only: compare command-local PRIORITY refresh with the full SDK reference.
// Neither branch advances the engine. Serialization/parity work is outside refresh meters.
#ifdef ZERO_TEACHER_WASM
#if defined(ZERO_STUDENT_PROBE)
#include <type_traits>
#include <stdexcept>
namespace zero_priority_probe {
struct Packed {
    std::vector<std::uint32_t> words;
    template<class T> void add(T value) {
        static_assert(sizeof(T)<=4);
        std::uint32_t bits=0;
        if constexpr(std::is_same_v<T,float>)std::memcpy(&bits,&value,4);
        else bits=static_cast<std::uint32_t>(value);
        words.push_back(bits);
    }
    void tile(zero::Tile v){add(v.x);add(v.y);}
    template<class T> void array(const T& values){add(std::size(values));for(const auto value:values)add(value);}
    void tiles(const std::vector<zero::Tile>& values){add(values.size());for(const auto value:values)tile(value);}
    void unit(const zero::UnitView& v){add(v.id);add(v.roster);add(v.type);tile(v.at);add(v.hp);add(v.max_hp);add(v.strength);add(v.focus);add(v.gems);add(v.gold);array(v.cooldown);add(v.recalling);add(v.tributing);add(v.stunned);add(v.shield);}
    void cand(const zero::Cand& v){add(v.kind);add(v.target);add(v.count);tile(v.at);for(int i=0;i<24;++i)add(v.phi[i]);}
};
void facts(Packed& p,const zero::Obs& o){
    // Revision is a cache epoch rather than a world fact; both monotonic updates are reported separately.
    p.add(o.round);p.add(o.team);p.add(o.command_index);p.unit(o.self);
    p.add(o.alive);p.add(o.can_move);p.add(o.can_attack);p.add(o.can_cast);p.add(o.on_spawn);p.add(o.recall_started);
    p.add(o.command_facts);p.add(o.teleporting);p.add(o.can_recall);p.add(o.can_tribute);p.add(o.can_cancel);p.add(o.can_buy);p.add(o.can_priority);p.array(o.can_cast_slot);
    p.add(o.state_bits);p.add(o.utility_known_mask);p.add(o.utility_known_consumables);p.add(o.utility_capability_limits);p.add(o.utility_affected_limits);
    p.add(o.ward_arguments_complete);p.add(o.consumable_arguments_complete);p.add(o.restriction_bits);p.add(o.channel_remaining);p.add(o.ward_charges);p.add(o.ward_cooldown);p.add(o.priority);
    p.array(o.consumables);p.array(o.item_counts);p.array(o.buyable_items);p.tiles(o.wards);p.add(o.ally_ward_facts);p.tiles(o.ally_ward_tiles);
    p.add(o.utility_actions.size());for(const auto& v:o.utility_actions){p.add(v.type);p.add(v.target);p.tile(v.at);for(float x:v.phi)p.add(x);}
    p.add(o.respawn_in);p.array(o.items);p.add(o.ward_ready);p.tiles(o.step_ok);p.tiles(o.dash_ok);p.add(o.dash_destinations_known);p.add(o.active_dash_range);p.add(o.reservation_facts);p.tiles(o.reserved_tiles);
    p.add(o.allies.size());for(const auto& v:o.allies)p.unit(v);p.add(o.enemies.size());for(const auto& v:o.enemies)p.unit(v);
    p.add(o.camps.size());for(const auto& v:o.camps){p.add(v.id);p.tile(v.at);p.add(v.type);p.add(v.hp);p.add(v.awake);p.add(v.group);}
    p.array(o.group_status);p.array(o.altar_lock);p.array(o.altar_lock_hits);p.add(o.banked_us);p.add(o.banked_them);p.add(o.allies_alive);p.add(o.enemies_alive);p.array(o.claim_kind);p.array(o.claim_target);
}
void history(Packed& p,const zero::Memory& m){
    p.add(m.enemies.size());for(const auto& v:m.enemies){p.add(v.id);p.add(v.type);p.tile(v.at);p.add(v.hp);p.add(v.max_hp);p.add(v.strength);p.add(v.focus);p.add(v.gems);p.add(v.round);}
    p.array(m.respawn_round);p.array(m.seen_round);p.array(m.enemy_near_round);p.array(m.banked_us_hist);p.array(m.banked_them_hist);p.array(m.hist_round);p.add(m.hist_head);p.add(m.deaths);p.add(m.was_alive);
    p.add(m.goal_kind);p.add(m.goal_target);p.add(m.goal_since);p.add(m.goal_recall_p);p.add(m.goal_decided);p.tile(m.prev_at);p.add(m.prev_valid);p.add(m.dir_x);p.add(m.dir_y);
}
void rows(Packed& p,const zero::Context& context){
    using namespace zero;
    float state[student::kState];student::state_vector(context,state);for(float value:state)p.add(value);
    const auto mask=student::rule_mask(context,kAllActions&~action_bit(a_replan));p.add(mask);
    for(int kind=1;kind<a_count;++kind){const auto type=static_cast<ActionType>(kind);if(!(mask&action_bit(type)))continue;
        std::vector<Cand> candidates;const auto built=student::argument_candidates(context,type,candidates);
        p.add(kind);p.add(built.status);p.add(built.total);p.add(candidates.size());for(const auto& candidate:candidates)p.cand(candidate);
        if(type==a_buy)for(const auto& item:candidates){Cand quantity;student::quantity_candidate(context,item,quantity);p.cand(quantity);}
    }
}
std::uint64_t refresh(const zero::Map& map,zero::Obs& local,zero::Memory& memory,const zero::Cand& argument){
    using namespace zero;
    const auto previous_revision=local.revision;
    Obs reference=local;Memory reference_memory=memory;
    auto before=cambc::fuel_remaining();sdk::refresh_after_primitive(map,local,a_priority,argument);observe(local,map,memory);const auto local_fuel=before-cambc::fuel_remaining();
    before=cambc::fuel_remaining();sdk::refresh_after_primitive(map,reference);observe(reference,map,reference_memory);const auto full_fuel=before-cambc::fuel_remaining();
    Packed a,b;facts(a,local);facts(b,reference);const auto fact_words=a.words.size();history(a,memory);history(b,reference_memory);const auto history_words=a.words.size()-fact_words;
    Context lc{&map,nullptr,&local,&memory},rc{&map,nullptr,&reference,&reference_memory};rows(a,lc);rows(b,rc);
    zero_lifecycle::PriorityParity wire{};wire.local=reinterpret_cast<std::uintptr_t>(a.words.data());wire.reference=reinterpret_cast<std::uintptr_t>(b.words.data());wire.local_words=a.words.size();wire.reference_words=b.words.size();
    wire.fact_words=fact_words;wire.history_words=history_words;wire.channel=local.self.recalling?1:local.self.tributing?2:local.teleporting?3:0;wire.remaining=local.channel_remaining;wire.priority=local.priority;wire.command_index=local.command_index;
    wire.previous_revision=previous_revision;wire.local_revision=local.revision;wire.full_revision=reference.revision;wire.local_fuel=local_fuel;wire.full_fuel=full_fuel;
    zero_student_probe(10,&wire,sizeof wire);
    if(a.words!=b.words||local.revision<=previous_revision||reference.revision<=local.revision)throw std::runtime_error("priority refresh parity mismatch");
    return local_fuel;
}
}
#endif
#endif
