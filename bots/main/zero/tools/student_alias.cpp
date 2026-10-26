// Synthetic production-representation audit; no game, policy, Paths or network.
#include "../src/core/student.h"
#include "../src/core/student_context.h"
#include "../src/core/student_move_exposure.h"
#include "../src/core/catalog.h"
#include "../src/core/moves.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace {
using namespace zero;
void require(bool pass, const char* message) { if (!pass) throw std::runtime_error(message); }
std::uint32_t bits(float value) {
    require(std::isfinite(value), "nonfinite feature");
    return std::bit_cast<std::uint32_t>(value);
}
Map fixture_map() {
    Map map; map.w = map.h = 32; map.flags.assign(32 * 32, 0);
    map.spawn[0] = {{0,0}}; map.spawn[1] = {{31,31}};
    map.flags[0] = tf_spawn_a; map.flags.back() = tf_spawn_b;
    Group group; group.members = {{{25,25},3}}; group.gold = 120; group.gems = 10;
    map.groups.push_back(group); map.finish();
    require(map.group_of.size() == map.flags.size() && map.groups[0].total_hp > 0, "unfinished map");
    return map;
}
UnitView champion(int type, int id, Tile at) {
    const auto& design = catalog().champion(type);
    UnitView unit; unit.id = id; unit.roster = 0; unit.type = static_cast<std::uint8_t>(type); unit.at = at;
    unit.hp = unit.max_hp = design.health; unit.strength = design.strength; unit.focus = design.focus;
    unit.cooldown[0] = 1; unit.cooldown[1] = 2;
    return unit;
}
Obs fixture(const Map& map, Tile enemy_at, float strength) {
    Obs o; o.revision = 1; o.round = 100; o.team = 0;
    o.self = champion(0,0,{10,10}); o.enemies = {champion(3,8,enemy_at)};
    o.enemies[0].strength = strength;
    o.allies_alive = o.enemies_alive = 1;
    o.command_facts = true; o.can_move = o.can_attack = o.can_cast = true;
    o.state_bits = (1u<<0) | (1u<<3) | (1u<<4) | (1u<<11);
    o.ward_arguments_complete = o.consumable_arguments_complete = true;
    o.ward_cooldown = 5; o.can_priority = true;
    for (int type = a_buy_consumable; type < a_count; ++type)
        o.utility_known_mask |= action_bit(static_cast<ActionType>(type));
    o.group_status.assign(map.groups.size(), gs_unknown);
    derive_command_state(o); canonical_observation_order(o); legal_moves(map,o,0,false);
    require(o.step_ok.size() == 8, "not the complete eight-neighbour MOVE set");
    for (const Tile t : o.step_ok) {
        require(t != o.self.at && cheb(t,o.self.at) == 1 && map.walkable(t,o.team), "illegal local destination");
        require(t != o.enemies[0].at, "occupied local destination");
    }
    require(cheb(o.self.at,enemy_at) == 4, "enemy distance differs");
    require(cheb(o.self.at,enemy_at) <= catalog().champion(o.self.type).vision, "enemy outside queried vision");
    require(cheb(o.self.at,enemy_at) > catalog().champion(o.self.type).attack_range, "enemy in self attack range");
    for (const auto& group : map.groups) for (const auto& member : group.members)
        require(cheb(o.self.at,member.at) > catalog().champion(o.self.type).vision, "unknown camp in personal vision");
    return o;
}
struct Inputs {
    std::array<float,student::kState> state{};
    std::vector<student::CampContext> camps;
    std::vector<Cand> moves;
    std::uint32_t mask{};
    ActionBuild build;
    Head head{};
};
Inputs input(const Map& map, const Obs& obs) {
    Memory memory; Context ctx{&map,nullptr,&obs,&memory}; Inputs out;
    require(memory.goal_kind == -1 && memory.goal_target == -1 && memory.goal_since == -1 && !ctx.paths, "conditioned context");
    student::state_vector(ctx,out.state.data()); student::camp_context(ctx,out.camps);
    out.mask = student::rule_mask(ctx,kAllActions);
    out.build = student::argument_candidates(ctx,a_move,out.moves); out.head = student::argument_head(a_move);
    require(out.build.status == ActionBuildStatus::ok && out.build.total == 8 && out.moves.size() == 8, "partial MOVE build");
    require(!(out.mask & action_bit(a_attack)), "unexpected attack family");
    require(out.camps.size() == map.groups.size() && !out.camps.empty(), "missing full context");
    for (const auto& camp : out.camps)
        require(camp.phi[6] > 0 && camp.phi[8] == 0 && camp.phi[9] == 0 && camp.phi[10] == 0 && camp.phi[11] == 0, "unknown camp mistaken for empty");
    return out;
}
std::vector<std::uint32_t> raw(const Inputs& in) {
    std::vector<std::uint32_t> words;
    auto number = [&](auto value) { words.push_back(static_cast<std::uint32_t>(value)); };
    number(student::kFeatureSchema); number(a_move); number(in.head); number(in.mask);
    number(in.build.status); number(in.build.total); number(in.camps.size()); number(in.moves.size());
    for (float value : in.state) words.push_back(bits(value));
    for (const auto& camp : in.camps) { number(camp.group); for (float value : camp.phi) words.push_back(bits(value)); }
    for (const auto& move : in.moves) {
        number(move.kind); number(move.target); number(move.count); number(move.at.x); number(move.at.y);
        for (float value : move.phi) words.push_back(bits(value));
    }
    return words;
}
void array(const std::vector<std::uint32_t>& words) {
    std::printf("["); for (std::size_t i=0;i<words.size();++i) std::printf("%s%u",i?",":"",words[i]); std::printf("]");
}
void observation(const char* name, const Obs& obs, const Inputs& in) {
    const auto& enemy=obs.enemies[0];
    std::printf("\"%s\":{\"enemy\":{\"id\":%d,\"type\":%u,\"at\":[%d,%d],\"hp\":%.9g,\"max_hp\":%.9g,\"strength\":%.9g,\"focus\":%.9g},\"full_rule_mask\":%u,\"move_build_status\":%u,\"move_build_total\":%d,\"raw_u32\":",name,enemy.id,unsigned(enemy.type),enemy.at.x,enemy.at.y,double(enemy.hp),double(enemy.max_hp),double(enemy.strength),double(enemy.focus),in.mask,unsigned(in.build.status),in.build.total);
    array(raw(in)); std::printf("}");
}
using Coverage = student::MoveObservedDesignCoverage;
std::vector<std::uint32_t> row_raw(const Cand& move) {
    std::vector<std::uint32_t> out{move.kind,static_cast<std::uint32_t>(move.target),static_cast<std::uint32_t>(move.count),static_cast<std::uint32_t>(move.at.x),static_cast<std::uint32_t>(move.at.y)};
    for (const float value:move.phi) out.push_back(bits(value));
    return out;
}
std::vector<std::uint32_t> coverage_raw(std::span<const Coverage> rows) {
    std::vector<std::uint32_t> out;
    const auto word64=[&](std::uint64_t value){out.push_back(static_cast<std::uint32_t>(value));out.push_back(static_cast<std::uint32_t>(value>>32));};
    for (const auto& row:rows) {
        word64(row.living_visible_enemy_champions_in_design_range);
        word64(std::bit_cast<std::uint64_t>(row.observed_enemy_strength_sum));
        word64(row.living_visible_awake_camps_in_design_range);
        word64(std::bit_cast<std::uint64_t>(row.queried_awake_camp_base_strength_sum));
    }
    return out;
}
void coverage(Context ctx, const std::vector<Cand>& moves, std::vector<Coverage>& out) {
    std::vector<std::vector<std::uint32_t>> before;
    for (const auto& row:moves) before.push_back(row_raw(row));
    student::move_observed_design_coverage(ctx,moves,out);
    require(out.size()==moves.size(),"coverage row cardinality differs");
    for (std::size_t i=0;i<moves.size();++i)
        require(row_raw(moves[i])==before[i]&&row_raw(out[i].move)==before[i],"coverage changed row identity/order");
}
const Coverage& at(std::span<const Coverage> rows, Tile tile) {
    for (const auto& row:rows) if(row.move.at==tile) return row;
    throw std::runtime_error("coverage missing destination");
}
void expected(const Coverage& row, std::uint64_t enemies, double strength, std::uint64_t camps, double base_strength) {
    require(row.living_visible_enemy_champions_in_design_range==enemies&&row.observed_enemy_strength_sum==strength&&
            row.living_visible_awake_camps_in_design_range==camps&&row.queried_awake_camp_base_strength_sum==base_strength,"coverage factual expectation differs");
}
void emit_coverage(std::span<const Coverage> rows) {
    std::printf("{\"row_identity_preserved\":true,\"raw_fact_u32\":");array(coverage_raw(rows));std::printf(",\"rows\":[");
    for(std::size_t i=0;i<rows.size();++i) {
        const auto& row=rows[i];
        std::printf("%s{\"payload\":[%u,%d,%d,%d,%d],\"facts\":[%llu,%.17g,%llu,%.17g]}",i?",":"",unsigned(row.move.kind),row.move.target,row.move.count,row.move.at.x,row.move.at.y,
                    static_cast<unsigned long long>(row.living_visible_enemy_champions_in_design_range),row.observed_enemy_strength_sum,
                    static_cast<unsigned long long>(row.living_visible_awake_camps_in_design_range),row.queried_awake_camp_base_strength_sum);
    }
    std::printf("]}");
}
CampView camp(int type,int id,Tile tile,bool awake=true) {
    CampView view;view.id=id;view.type=static_cast<std::uint8_t>(type);view.at=tile;view.hp=catalog().camp(type).health;view.awake=awake;view.group=1;return view;
}
struct Control {
    const char* name{}; Obs obs; std::uint64_t enemies{},camps{};double strength{},base_strength{};
};
void prototype(const Map& map,const Obs& east,const Obs& west,const Obs& strong,const Inputs& a,const Inputs& b,const Inputs& c) {
    // Memory and Paths are deliberately absent; only current Obs/Map facts enter this module.
    std::vector<Coverage> ae,be,ce;
    coverage({&map,nullptr,&east,nullptr},a.moves,ae);coverage({&map,nullptr,&west,nullptr},b.moves,be);coverage({&map,nullptr,&strong,nullptr},c.moves,ce);
    for(const auto& row:ae)expected(row,row.move.at.x==11?1:0,row.move.at.x==11?30:0,0,0);
    for(const auto& row:be)expected(row,row.move.at.x==9?1:0,row.move.at.x==9?30:0,0,0);
    for(const auto& row:ce)expected(row,row.move.at.x==11?1:0,row.move.at.x==11?60:0,0,0);
    auto augmented=[&](const Inputs& original,const auto& sidecar){auto words=raw(original);const auto extra=coverage_raw(sidecar);words.insert(words.end(),extra.begin(),extra.end());return words;};
    require(raw(a).size()==301&&coverage_raw(ae).size()==64,"original or appended word count changed");
    require(augmented(a,ae)!=augmented(b,be)&&augmented(a,ae)!=augmented(c,ce),"sidecar fails to separate alias");
    std::size_t reflection_rows=0,strength_rows=0;
    for(std::size_t i=0;i<ae.size();++i){reflection_rows+=coverage_raw(std::span(&ae[i],1))!=coverage_raw(std::span(&be[i],1));strength_rows+=coverage_raw(std::span(&ae[i],1))!=coverage_raw(std::span(&ce[i],1));}
    require(reflection_rows==6&&strength_rows==3,"unexpected differing destination count");
    std::printf("\"prototype\":{\"kind\":\"unintegrated_observed_design_range_coverage\",\"scope\":\"Current personally observed subset only. Counts include zero-Strength sources. No attack readiness, occurrence, target selection, damage, future waking or safety inference; unseen sources remain unknown.\",\"facts\":[\"living_visible_enemy_champions_in_design_range\",\"observed_enemy_strength_sum\",\"living_visible_awake_camps_in_design_range\",\"queried_awake_camp_base_strength_sum\"],\"encoding\":\"Per row, four typed facts: uint64 count, float64 sum, uint64 count, float64 sum, each low then high uint32 word; input Cand copied exactly and row order retained.\",\"appended_fact_values\":32,\"appended_u32_words\":64,\"augmented_u32_words\":365,\"comparisons\":{\"reflection_augmented_differs\":true,\"strength_augmented_differs\":true,\"reflection_differing_move_rows\":%zu,\"strength_differing_move_rows\":%zu},\"fixtures\":{\"east\":",reflection_rows,strength_rows);
    emit_coverage(ae);std::printf(",\"west\":");emit_coverage(be);std::printf(",\"east_stronger\":");emit_coverage(ce);std::printf("},\"controls\":[");
    auto empty=east;empty.enemies.clear();empty.camps.clear();empty.enemies_alive=0;
    std::vector<Control> controls;
    controls.push_back({"empty_observed_subset",empty});
    auto sleeping=empty;sleeping.camps={camp(0,100,{14,10},false)};controls.push_back({"sleeping_visible_camp_excluded",sleeping});
    auto dead=east;dead.enemies[0].hp=0;dead.camps={camp(0,100,{14,11})};dead.camps[0].hp=0;dead.enemies_alive=0;controls.push_back({"dead_source_robustness",dead});
    auto zero=east;zero.enemies[0].strength=0;zero.camps={camp(3,100,{14,11})};controls.push_back({"zero_strength_sources_still_count",zero,1,1,0,0});
    auto multiple=east;multiple.enemies.push_back(champion(3,9,{13,12}));multiple.enemies.back().roster=1;multiple.enemies.back().strength=60;multiple.enemies_alive=2;
    multiple.camps={camp(0,100,{14,11}),camp(1,101,{15,10}),camp(4,102,{16,10})};controls.push_back({"multiple_sources_inclusive_range_boundaries",multiple,2,3,90,280});
    auto stunned=east;stunned.enemies[0].stunned=true;controls.push_back({"enemy_readiness_not_inferred",stunned,1,0,30,0});
    auto outside=empty;outside.camps={camp(0,100,{15,10}),camp(1,101,{16,10}),camp(4,102,{17,10})};controls.push_back({"one_tile_outside_each_queried_camp_range",outside});
    auto many=empty;
    for(int dy=-3;dy<=3&&many.camps.size()<30;++dy)for(int dx=-3;dx<=3&&many.camps.size()<30;++dx){const Tile tile{static_cast<std::int16_t>(11+dx),static_cast<std::int16_t>(10+dy)};if(cheb(tile,many.self.at)>1)many.camps.push_back(camp(0,100+static_cast<int>(many.camps.size()),tile));}
    require(many.camps.size()==30,"uncapped control generation differs");controls.push_back({"thirty_visible_camps_no_24_entity_cap",many,0,30,0,600});
    // Reuse the same caller output after a nonzero, multi-source observation.
    controls.push_back({"reused_output_clears_prior_sources",empty});
    std::vector<Coverage> reused(17);
    for(std::size_t i=0;i<controls.size();++i){
        auto& control=controls[i];auto control_map=fixture_map();
        if(!control.obs.camps.empty()){Group group;for(const auto& source:control.obs.camps)group.members.push_back({source.at,source.type});control_map.groups.push_back(group);control_map.finish();}
        control.obs.group_status.assign(control_map.groups.size(),gs_unknown);if(control_map.groups.size()>1)control.obs.group_status[1]=gs_alive;
        canonical_observation_order(control.obs);legal_moves(control_map,control.obs,0,false);require(control.obs.step_ok.size()==8,"control omitted a local MOVE");
        Memory memory;Context ctx{&control_map,nullptr,&control.obs,&memory};std::vector<Cand> rows;const auto build=student::argument_candidates(ctx,a_move,rows);
        require(build.status==ActionBuildStatus::ok&&build.total==8&&rows.size()==8,"control MOVE list partial");
        coverage(ctx,rows,reused);expected(at(reused,{11,10}),control.enemies,control.strength,control.camps,control.base_strength);
        std::printf("%s{\"name\":\"%s\",\"pass\":true,\"visible_enemy_rows\":%zu,\"visible_camp_rows\":%zu,\"east_destination_expected\":[%llu,%.17g,%llu,%.17g],\"coverage\":",i?",":"",control.name,control.obs.enemies.size(),control.obs.camps.size(),static_cast<unsigned long long>(control.enemies),control.strength,static_cast<unsigned long long>(control.camps),control.base_strength);emit_coverage(reused);std::printf("}");
    }
    auto reversed=a.moves;std::reverse(reversed.begin(),reversed.end());coverage({&map,nullptr,&east,nullptr},reversed,reused);
    expected(at(reused,{11,10}),1,30,0,0);
    std::printf("],\"order_and_input_bits_preserved\":true,\"reversed_complete_row_order_preserved\":true,\"invalid_input_controls\":[");
    unsigned rejected=0;
    auto reject=[&](const char* name,Context ctx,const std::vector<Cand>& rows){reused=ae;bool threw=false;try{student::move_observed_design_coverage(ctx,rows,reused);}catch(const std::invalid_argument&){threw=true;}catch(const std::overflow_error&){threw=true;}require(threw&&reused.empty(),"invalid input not rejected cleanly");std::printf("%s\"%s\"",rejected++?",":"",name);};
    auto bad=east;bad.enemies[0].hp=std::numeric_limits<float>::quiet_NaN();reject("nonfinite_hp",{&map,nullptr,&bad,nullptr},a.moves);
    bad=east;bad.enemies[0].strength=std::numeric_limits<float>::infinity();reject("nonfinite_observed_strength",{&map,nullptr,&bad,nullptr},a.moves);
    bad=east;bad.enemies[0].type=255;reject("invalid_enemy_type",{&map,nullptr,&bad,nullptr},a.moves);
    auto bad_rows=a.moves;bad_rows[0].target=0;reject("non_move_payload",{&map,nullptr,&east,nullptr},bad_rows);
    reject("missing_map",{nullptr,nullptr,&east,nullptr},a.moves);
    std::printf("],\"invalid_inputs_rejected\":%u,\"all_controls_pass\":true}",rejected);
}
} // namespace
int main() {
    try {
        const auto map=fixture_map(); const auto& self_design=catalog().champion(0); const auto& enemy_design=catalog().champion(3);
        require(self_design.attack_type == 0 && enemy_design.attack_type == 1, "attack type differs");
        require(self_design.move_speed == 1, "ordinary move radius differs");
        const auto east=fixture(map,{14,10},enemy_design.strength), west=fixture(map,{6,10},enemy_design.strength), strong=fixture(map,{14,10},enemy_design.strength*2);
        const auto a=input(map,east),b=input(map,west),c=input(map,strong);
        const auto ar=raw(a),br=raw(b),cr=raw(c);
        const Tile destination{11,10}; const int east_distance=cheb(destination,east.enemies[0].at),west_distance=cheb(destination,west.enemies[0].at);
        const bool east_in=east_distance<=enemy_design.attack_range,west_in=west_distance<=enemy_design.attack_range;
        const bool included=std::any_of(a.moves.begin(),a.moves.end(),[&](const auto& row){return row.at==destination;});
        const bool reflected=ar==br,strength=ar==cr;
        std::printf("{\"schema\":\"main.zero.student.alias.v1\",\"scope\":\"Synthetic observation consistency and raw production MOVE input equality only; no natural reachability, engine damage, optimal action, learned competence or fuel claim.\",\"map\":{\"width\":32,\"height\":32,\"finished\":true,\"open_local_terrain\":true,\"groups\":1,\"members\":1,\"unknown_dynamic_context\":true},\"self\":{\"type\":0,\"at\":[10,10],\"attack_range\":%.9g,\"vision\":%.9g},\"no_goal\":true,\"paths\":null,\"counts\":{\"state_float_words\":48,\"context_rows\":%zu,\"context_id_words\":%zu,\"context_float_words\":%zu,\"move_rows\":%zu,\"ordered_payload_words\":%zu,\"move_float_words\":%zu,\"header_words\":8,\"raw_words\":%zu,\"raw_bits\":%zu},\"encoding\":{\"header\":[\"feature_schema\",\"command\",\"argument_head\",\"full_rule_mask\",\"build_status\",\"build_total\",\"context_count\",\"move_count\"],\"body\":\"state[48] float32 bits; every context row group ID then all 12 float32 bits; every MOVE row kind,target,count,x,y then all 24 float32 bits. Signed payload integers use uint32 two's-complement. No padding or truncation.\"},\"fixtures\":{",double(self_design.attack_range),double(self_design.vision),a.camps.size(),a.camps.size(),a.camps.size()*student::kCampContextPhi,a.moves.size(),a.moves.size()*5,a.moves.size()*student::kPhi,ar.size(),ar.size()*32);
        observation("east",east,a); std::printf(","); observation("west",west,b); std::printf(","); observation("east_stronger",strong,c);
        std::printf("},\"comparisons\":{\"reflection_complete_raw_equal\":%s,\"strength_complete_raw_equal\":%s,\"strength_observable_differs\":%s,\"all_masks_equal\":%s},\"range_witness\":{\"destination\":[11,10],\"legal_move_present\":%s,\"enemy_type\":3,\"queried_catalog_attack_range\":%.9g,\"east_distance\":%d,\"west_distance\":%d,\"east_in_range\":%s,\"west_in_range\":%s},",reflected?"true":"false",strength?"true":"false",east.enemies[0].strength!=strong.enemies[0].strength?"true":"false",a.mask==b.mask&&a.mask==c.mask?"true":"false",included?"true":"false",double(enemy_design.attack_range),east_distance,west_distance,east_in?"true":"false",west_in?"true":"false");
        prototype(map,east,west,strong,a,b,c);std::printf("}\n");
        return reflected&&strength&&included&&east_in&&!west_in ? 0:1;
    } catch(const std::exception& error) { std::fprintf(stderr,"error: %s\n",error.what()); return 2; }
}
