// Event-sourced source features. No actor, neural model, Paths or world simulation is instantiated.
// student_records INPUT.tokens OUTPUT.zstud COVERAGE.json INTENTS.json
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "source_intent.h"
#include "../src/core/catalog.h"
#include "../src/core/moves.h"
#include "../src/core/student.h"
#include "../src/core/student_context.h"
#include "../src/core/student_history.h"
#include "../src/core/utility.h"

using namespace zero;
namespace {
static_assert(student::kState==48&&student::kPhi==24&&student::kCampContextPhi==12);
std::string quoted(const std::string& s) { std::string o="\"";for(char c:s){if(c=='\\'||c=='\"')o+='\\';o+=c;}return o+'\"'; }
template<class T> std::string list(const std::vector<T>& values){std::ostringstream o;o<<'[';for(std::size_t i=0;i<values.size();++i){if(i)o<<',';o<<values[i];}o<<']';return o.str();}
// Raw parsing reused from exact_records.cpp; no old Producer/features/annotations.
std::vector<std::uint8_t> unhex(const std::string& s)
{
    if (s.size() % 2) throw std::runtime_error("odd raw hex length");
    std::vector<std::uint8_t> out;
    auto digit = [](char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; throw std::runtime_error("bad raw hex"); };
    for (std::size_t i = 0; i < s.size(); i += 2) out.push_back(static_cast<std::uint8_t>(16 * digit(s[i]) + digit(s[i + 1])));
    return out;
}
template<class T> T raw(const std::vector<std::uint8_t>& b, std::size_t at)
{
    if (at + sizeof(T) > b.size()) throw std::runtime_error("raw ABI bounds");
    T value; std::memcpy(&value, b.data() + at, sizeof(T)); return value;
}

void read_unit(std::istringstream& ss, UnitView& u, int& team, std::uint32_t& bits, int& restrictions, int& channel)
{
    int type, x, y, cd0, cd1, n;
    ss >> u.id >> team >> type >> x >> y >> u.hp >> u.max_hp >> u.strength >> u.focus >> u.gold >> u.gems >> bits >> cd0 >> cd1 >> restrictions >> channel >> n;
    if (type < 0 || type >= catalog().n_champions || cd0 < -1 || cd0 > 127 || cd1 < -1 || cd1 > 127 || n < 0 || n > 3) throw std::runtime_error("unit bounds");
    u.type = static_cast<std::uint8_t>(type); u.at = { static_cast<std::int16_t>(x), static_cast<std::int16_t>(y) };
    u.cooldown[0] = static_cast<std::int8_t>(cd0); u.cooldown[1] = static_cast<std::int8_t>(cd1);
    u.recalling = (bits & 2u) != 0; u.tributing = (bits & 4u) != 0;
    for (int i = 0; i < n; ++i)
    {
        std::uint32_t kind, payload; int timer; ss >> kind >> timer >> payload;
        if (kind == 0) u.shield = std::bit_cast<float>(payload);
        if (kind == 1) u.stunned = true;
    }
}



struct Personal
{
    Memory memory{};
    int round{-1},turn{-1},commands{};
    bool ended{true};
    std::vector<int> prefix,owner_prefix;
};

struct Producer
{
    Map map;
    std::map<int,int> group_ids,roster_slots,actor_teams;
    std::set<int> validated_designs;
    std::map<int,Personal> personal;
    std::array<std::uint32_t,2> consumable_cost{},consumable_limit{};
    std::array<bool,2> consumable_known{};
    struct {int settings_checked{},designs_checked{},context_builds{};std::size_t context_rows{};} coverage;
    Obs obs;
    source::SourceEvent src;
    source::InverseFacts facts;
    bool finished_map{},self_seen{},shop_known{},ward_known{},priority_known{},reservations_known{},items_known{},consumables_known{},buffs_known{},counts_known{},origins_known{};
    bool shop{},consumable_shop{},sell{};
    float explicit_shield{};bool explicit_stunned{};
    int before_phase{},after_phase{},turn{},before_substep{},after_substep{};
    int winner{-1},first{},last{},emit_actor{},processed{},frames{},ambiguous23{},fuel26{};
    bool header{},trailer{};
    std::string source_sha;
    std::vector<int> vision;
    std::vector<std::string> raw_events,units,intents;
    std::string failure, failure_stage;
    FILE* output{};

        void settings(const std::string& hex)
    {
        const auto b = unhex(hex); const auto& s = catalog().settings;
        if (b.size() != 120) throw std::runtime_error("settings ABI size");
        const std::pair<int, int> ints[] = { {0,s.capture_time},{4,s.camp_sleep_time},{8,s.respawn_time},{12,s.recall_time},
            {28,s.submit_gem_minimum_duration},{32,s.submit_gems_range},{36,s.max_rounds},{40,s.winning_gems},{44,s.kill_gold},
            {80,s.ward_cooldown},{104,s.altar_lock_hits},{112,s.tp_crystal_time} };
        for (auto [at, value] : ints) if (raw<int>(b, at) != value) throw std::runtime_error("queried settings differ from compiled catalog");
        if (std::abs(raw<double>(b, 16) - s.submit_gem_duration_multiplier) > 1e-7 || raw<float>(b, 88) != s.ward_vision_radius || raw<float>(b, 92) != s.ward_placement_radius)
            throw std::runtime_error("queried settings differ from compiled catalog");
        ++coverage.settings_checked;
    }

    void design(int type, const std::string& hex)
    {
        if(type<0||type>=catalog().n_champions)throw std::runtime_error("queried champion type bounds");
        const auto b = unhex(hex); const auto& d = catalog().champion(type);
        if (b.size() != 128 || raw<int>(b, 0) != type || b[84] != d.attack_type) throw std::runtime_error("queried champion ABI differs from compiled catalog");
        for (int k = 0; k < 2; ++k)
        {
            const int at = 12 + k * 36; const auto& a = d.ability[k];
            if (raw<std::uint32_t>(b, at) != a.kind || raw<int>(b, at + 4) != a.cooldown || raw<std::uint32_t>(b, at + 12) != a.targets)
                throw std::runtime_error("queried ability differs from compiled catalog");
        }
        ++coverage.designs_checked;
        validated_designs.insert(type);
    }

    std::string origin(int phase) const
    {
        std::ostringstream o;o<<"{\"sequence\":"<<src.sequence<<",\"input\":"<<src.input<<",\"actor\":"<<src.actor
          <<",\"team\":"<<obs.team<<",\"round\":"<<src.round<<",\"phase\":"<<phase<<",\"native_kind\":"<<src.kind<<'}';return o.str();
    }
    std::string history() const
    {
        const auto& a=personal.at(src.actor);std::ostringstream o;
        o<<"{\"initial_goal\":\"NONE\",\"memory_init\":\"Memory{}\",\"owner_turn\":"<<turn
         <<",\"command_index_before\":"<<obs.command_index<<",\"personal_prefix_inputs\":"<<list(a.prefix)
         <<",\"owner_prefix_inputs\":"<<list(a.owner_prefix)
         <<",\"goal_kind\":-1,\"goal_target\":-1,\"goal_since\":-1,\"goal_decided\":-1}";return o.str();
    }
    std::string proof(const source::InverseResult& result,int type) const
    {
        std::ostringstream o;o<<"{\"oracle\":\"ReplayNext\",\"inverse_status\":"<<quoted(source::status_name(result.status))
          <<",\"inverse_reason\":"<<quoted(source::reason_name(result.reason))<<",\"source_sha256\":"<<quoted(source_sha)
          <<",\"action_type\":"<<type<<'}';return o.str();
    }
    std::string event_role(const source::InverseResult& result) const
    {
        if(result.status==source::Status::exogenous)return "fuel_boundary";
        if(result.status==source::Status::ambiguous)return "inverse_ambiguous_attempt";
        if(result.status==source::Status::phase_event)return "phase_event_requires_pair";
        if(result.resolved())return result.intent->type==a_end?"observed_owner_end":"observed_attempt";
        return "unresolved_source_event";
    }
    std::string intent_json(const source::InverseResult& result) const
    {
        std::ostringstream o;o<<"{\"sequence\":"<<src.sequence<<",\"input\":"<<src.input<<",\"raw_kind\":"<<src.kind
          <<",\"status\":"<<quoted(source::status_name(result.status))<<",\"reason\":"<<quoted(source::reason_name(result.reason))
          <<",\"decision_origin\":"<<origin(before_phase)<<",\"label_origin\":"<<origin(after_phase)
          <<",\"source_event\":{\"slot\":"<<src.slot<<",\"from\":["<<src.from.x<<','<<src.from.y<<"],\"to\":["<<src.to.x<<','<<src.to.y<<"],\"priority\":"<<src.priority
          <<",\"before_substep\":"<<before_substep<<",\"after_substep\":"<<after_substep<<",\"owner_turn\":"<<turn<<'}';
        if(result.resolved())
        {
            const auto& i=*result.intent;const auto& c=i.argument;
            o<<",\"intent\":{\"type\":"<<static_cast<int>(i.type)<<",\"argument\":{\"kind\":"<<static_cast<int>(c.kind)<<",\"target\":"<<c.target
             <<",\"count\":"<<c.count<<",\"x\":"<<c.at.x<<",\"y\":"<<c.at.y<<"},\"target_kind\":"<<static_cast<int>(i.target_kind)
             <<",\"target_id\":"<<i.target_id<<",\"scalar\":"<<i.scalar<<",\"scalar_known\":"<<(i.scalar_known?"true":"false")<<'}';
        }
        o<<'}';return o.str();
    }
    void frame(const source::InverseResult& result,int kind,int head,int label,std::uint32_t mask,const float* state,
               const std::vector<student::CampContext>& camp_rows,const std::vector<Cand>* rows)
    {
        const int n=rows?static_cast<int>(rows->size()):0,type=result.intent->type;
        const int won=winner<0?-1:(winner==obs.team?1:0);
        std::ostringstream o;o<<"{\"kind\":"<<kind<<",\"head\":"<<head<<",\"command\":"<<type<<",\"n\":"<<n<<",\"label\":"<<label
          <<",\"feature_schema\":3,\"context_n\":"<<camp_rows.size()<<",\"context_phi\":"<<student::kCampContextPhi
          <<",\"rule_mask\":"<<mask<<",\"profile_mask\":"<<(kAllActions&~action_bit(a_replan))
          <<",\"training_rule_mask\":"<<(mask&~action_bit(a_replan))<<",\"policy_profile\":\"unconditional_camp_attention_v1\",\"admitted_mask\":null,\"won\":"<<won
          <<",\"decision_origin\":"<<origin(before_phase)<<",\"label_origin\":"<<origin(after_phase)
          <<",\"event_role\":"<<quoted(event_role(result))<<",\"label_kind\":\"observed_engine_action\",\"history_type\":\"sourceprefix+declared_no_goal/v1\""
          <<",\"qualification\":{\"source_semantic_pass\":true,\"representation_pass\":true,\"history_contract_pass\":true,\"deployment_slice_pass\":false,\"optimizer_allowed\":false,\"purpose\":\"contract_test\"}"
          <<",\"history\":"<<history()<<",\"facts\":{\"self_buffs_known\":true,\"camp_context_all_static_groups\":true},\"pairproof\":"<<proof(result,type)<<'}';
        const auto metadata=o.str();const auto size=static_cast<std::uint32_t>(metadata.size());
        std::fwrite(&size,4,1,output);std::fwrite(metadata.data(),1,metadata.size(),output);std::fwrite(state,4,student::kState,output);
        for(const auto& row:camp_rows)
        {
            const std::int32_t group=row.group;
            std::fwrite(&group,4,1,output);std::fwrite(row.phi,4,student::kCampContextPhi,output);
        }
        if(rows)for(const auto& c:*rows)
        {
            const std::int32_t payload[5]={c.kind,c.target,c.count,c.at.x,c.at.y};
            std::fwrite(payload,4,5,output);std::fwrite(c.phi,4,student::kPhi,output);
        }
        ++frames;
    }
    void fail(const char* stage,const std::string& reason)
    {
        failure_stage=stage;failure="input "+std::to_string(src.input)+": "+reason;throw std::runtime_error(failure);
    }
    void finish_input()
    {
        if(src.input!=first+processed||src.sequence!=src.input-1)fail("source_facts","noncontiguous retained source prefix");
        ++processed;
        if(!finished_map){map.finish();finished_map=true;}
        if(!self_seen)fail("source_facts","missing SELF");
        facts.self_known=true;facts.before_gold_known=true;facts.before_items_known=items_known;facts.before_consumables_known=consumables_known;
        facts.visible_units_complete=true;facts.visible_camps_complete=true;facts.consumable_types_known=consumable_known[0]&&consumable_known[1];
        facts.consumable_cost[0]=consumable_cost[0];facts.consumable_cost[2]=consumable_cost[1];
        facts.consumable_cost_known[0]=consumable_known[0];facts.consumable_cost_known[2]=consumable_known[1];
        facts.lock_personally_known.assign(map.altars.size(),0);
        for(std::size_t a=0;a<map.altars.size();++a)for(int tile:vision)if(tile==map.idx(map.altars[a]))facts.lock_personally_known[a]=1;
        const auto decoded=source::inverse(src,map,obs,facts);if(emit_actor<0||src.actor==emit_actor)intents.push_back(intent_json(decoded));
        const bool ambiguous_attempt=src.kind==23&&decoded.status==source::Status::ambiguous;
        const bool fuel_boundary=src.kind==26&&decoded.status==source::Status::exogenous;
        if(ambiguous_attempt)++ambiguous23;if(fuel_boundary)++fuel26;
        if(!decoded.resolved()&&!fuel_boundary&&!ambiguous_attempt)
            fail("semantic_inverse",std::string(source::reason_name(decoded.reason)));
        if(!origins_known||!counts_known||!shop_known||!ward_known||!priority_known||!items_known||!consumables_known||!reservations_known||!obs.ally_ward_facts||!buffs_known||coverage.settings_checked!=1||!validated_designs.contains(obs.self.type))
            fail("required_facts","missing queried student source fact");
        if(std::bit_cast<std::uint32_t>(obs.self.shield)!=std::bit_cast<std::uint32_t>(explicit_shield)||obs.self.stunned!=explicit_stunned)
            fail("required_facts","self buff query disagrees with raw buff list");
        obs.command_facts=true;obs.revision=src.input;
        obs.on_spawn=obs.alive&&map.inside(obs.self.at)&&(map.at(obs.self.at)&(obs.team?tf_spawn_b:tf_spawn_a));
        derive_command_state(obs);
        const auto& design=catalog().champion(obs.self.type);
        for(int slot=0;slot<2;++slot)obs.can_cast_slot[slot]=obs.can_cast&&obs.self.cooldown[slot]==0&&design.ability[slot].kind!=ab_none;
        for(int i=0;i<catalog().n_items&&i<kMaxItems;++i)
        {
            obs.buyable_items[i]=shop&&catalog().item(i).purchasable&&obs.self.gold>=catalog().item(i).cost;
            obs.can_buy|=obs.buyable_items[i];
        }
        obs.utility_known_mask=action_bit(a_sell)|action_bit(a_buy_consumable)|action_bit(a_priority);
        if(sell)for(int i=0;i<catalog().n_items&&i<kMaxItems;++i)if(obs.item_counts[i]&&static_cast<std::uint64_t>(obs.self.gold)+catalog().item(i).cost<=std::numeric_limits<std::uint32_t>::max())append_sell_utility(obs,a_sell,i,catalog().item(i).cost);
        for(int i=0;i<2;++i)if(consumable_known[i]&&consumable_shop&&obs.self.gold>=consumable_cost[i]&&obs.consumables[i]<consumable_limit[i])append_buy_consumable_utility(obs,a_buy_consumable,i,consumable_cost[i]);
        obs.can_priority=(obs.state_bits&32u)==0;obs.reservation_facts=reservations_known;
        const auto utility=append_ground_utilities(map,obs,{static_cast<float>(catalog().settings.ward_placement_radius),2.0f,consumable_cost[1],consumable_known[1]?kConsumeLock:0u});
        obs.utility_known_consumables=utility.known_consume_mask;obs.utility_capability_limits=utility.limitations;obs.utility_affected_limits=utility.affected_limits;
        obs.ward_arguments_complete=utility.ward_complete;obs.consumable_arguments_complete=utility.consume_complete;
        if(utility.affected_limits)fail("representation","source utility capability gap");
        int dash=0;for(int slot=0;slot<2;++slot)if(obs.can_cast_slot[slot]&&design.ability[slot].kind==ab_dash)dash=std::max(dash,static_cast<int>(design.ability[slot].range));
        canonical_observation_order(obs);legal_moves(map,obs,dash,false);
        auto& own=personal[src.actor];
        if(own.ended||own.round!=src.round||own.turn!=turn){student::begin_commands(obs);own.commands=0;own.owner_prefix.clear();own.ended=false;own.round=src.round;own.turn=turn;}
        else obs.command_index=own.commands;
        own.prefix.push_back(src.input);own.owner_prefix.push_back(src.input);observe(obs,map,own.memory);
        if(own.memory.goal_kind!=-1||own.memory.goal_target!=-1||own.memory.goal_since!=-1||own.memory.goal_decided!=-1)fail("history","undeclared goal-conditioning transition");
        Context context{&map,nullptr,&obs,&own.memory};
        std::vector<student::CampContext> camp_rows;student::camp_context(context,camp_rows);
        ++coverage.context_builds;coverage.context_rows+=camp_rows.size();
        if(camp_rows.size()!=map.groups.size())fail("representation","camp context omits a static group");
        for(std::size_t i=0;i<camp_rows.size();++i)
        {
            if(camp_rows[i].group!=static_cast<int>(i))fail("representation","camp context group order differs from canonical map order");
            for(float value:camp_rows[i].phi)if(!std::isfinite(value))fail("representation","nonfinite camp context fact");
        }
        const bool emit=emit_actor<0||src.actor==emit_actor;
        std::ostringstream raw;raw<<"{\"origin\":"<<origin(before_phase)<<",\"label_origin\":"<<origin(after_phase)<<",\"event_role\":"<<quoted(event_role(decoded))
          <<",\"source_status\":"<<quoted(source::status_name(decoded.status))<<",\"supervision_scope\":"<<quoted(emit?"emit_actor":"personal_prefix_only")
          <<",\"command_index_before\":"<<obs.command_index<<",\"owner_turn\":"<<turn<<'}';raw_events.push_back(raw.str());
        if(emit&&decoded.resolved())
        {
            if(!decoded.resolved())fail("semantic_inverse",source::reason_name(decoded.reason));
            const auto& intent=*decoded.intent;
            const auto mask=student::rule_mask(context,kAllActions);
            if(!(mask&action_bit(intent.type)))fail("representation","observed command missing from semantic rule mask");
            float state[student::kState];student::state_vector(context,state);
            for(float value:state)if(!std::isfinite(value))fail("representation","nonfinite student state");
            std::vector<Cand> arguments,quantity;int label=-1;Head head=h_aux;
            if(intent.type!=a_end)
            {
                const auto build=student::argument_candidates(context,intent.type,arguments);head=student::argument_head(intent.type);
                if(build.status!=ActionBuildStatus::ok||arguments.empty())fail("representation","incomplete selected argument builder");
                int matches=0;for(int i=0;i<static_cast<int>(arguments.size());++i)if(source::match_argument(intent,arguments[i])){label=i;++matches;}
                if(matches!=1)fail("representation","source argument absent or duplicated");
                if(intent.type==a_priority)
                {
                    if(arguments.size()!=1||!intent.scalar_known||intent.scalar<std::numeric_limits<int>::min()||intent.scalar>std::numeric_limits<int>::max())fail("representation","priority scalar not encodable");
                    label=static_cast<int>(intent.scalar);
                }
                if(intent.type==a_buy)
                {
                    if(!intent.scalar_known||intent.scalar<1||intent.scalar>std::numeric_limits<int>::max())fail("representation","quantity scalar not encodable");
                    quantity.emplace_back();student::quantity_candidate(context,arguments[label],quantity.back());
                }
                for(const auto& row:arguments)for(float value:row.phi)if(!std::isfinite(value))fail("representation","nonfinite raw argument fact");
            }
            std::vector<std::string> stages{quoted("command")};
            if(intent.type!=a_end)stages.push_back(quoted("family:"+std::to_string(static_cast<int>(head))));
            if(intent.type==a_buy)stages.push_back(quoted("family:12"));
            std::ostringstream unit;unit<<"{\"decision_origin\":"<<origin(before_phase)<<",\"label_origin\":"<<origin(after_phase)
              <<",\"event_role\":"<<quoted(event_role(decoded))<<",\"label_kind\":\"observed_engine_action\",\"history_type\":\"sourceprefix+declared_no_goal/v1\",\"command\":"<<static_cast<int>(intent.type)<<",\"expected_stages\":"<<list(stages)<<'}';units.push_back(unit.str());
            frame(decoded,0,-1,intent.type,mask,state,camp_rows,nullptr);
            if(intent.type!=a_end)frame(decoded,intent.type==a_priority?2:1,head,label,mask,state,camp_rows,&arguments);
            if(intent.type==a_buy)frame(decoded,2,h_quantity,static_cast<int>(intent.scalar),mask,state,camp_rows,&quantity);
        }
        if(src.kind==8||src.kind==26)own.ended=true;
        else if(decoded.resolved()||src.kind==23){student::record_command(obs);own.commands=obs.command_index;}
    }
    void report(const char* path) const
    {
        std::ofstream out(path);out<<"{\"scope\":\"paired_source_units\",\"feature_schema\":3,\"policy_profile\":\"unconditional_camp_attention_v1\",\"source_sha256\":"<<quoted(source_sha)
          <<",\"pass\":"<<(failure.empty()&&trailer?"true":"false")<<",\"failure_stage\":"<<quoted(failure_stage)<<",\"failure\":"<<quoted(failure)
          <<",\"emit_scope\":{\"actor\":"<<emit_actor<<",\"first_input\":"<<(emit_actor==6?26:first)<<",\"last_input\":"<<last<<"},\"raw_events\":"<<list(raw_events)<<",\"units\":"<<list(units)
          <<",\"unrecoverable_intent_inputs\":"<<(ambiguous23?"{\"kind23_inverse_ambiguous_intent\":"+std::to_string(ambiguous23)+"}":"{}")
          <<",\"exogenous_inputs\":"<<(fuel26?"{\"kind26_fuel_boundary\":"+std::to_string(fuel26)+"}":"{}")
          <<",\"semantic_omissions\":"<<(failure_stage=="semantic_inverse"?"{\"first_failure\":1}":"{}")
          <<",\"representation_omissions\":"<<((!failure.empty()&&failure_stage!="semantic_inverse"&&failure_stage!="history")?"{\"first_failure\":1}":"{}")
          <<",\"history_omissions\":"<<(failure_stage=="history"?"{\"first_failure\":1}":"{}")
          <<",\"camp_context\":{\"builder\":\"student::camp_context\",\"calls\":"<<coverage.context_builds<<",\"rows\":"<<coverage.context_rows
          <<",\"all_static_groups\":"<<map.groups.size()<<",\"phi\":"<<student::kCampContextPhi<<",\"order\":\"canonical_map_group\"},\"records\":"<<frames<<'}';
    }
};
} // namespace

int main(int argc,char** argv)
{
    if(argc!=5){std::fprintf(stderr,"usage: student_records INPUT.tokens OUTPUT.zstud COVERAGE.json INTENTS.json\n");return 2;}
    Producer p;bool active=false;
    try
    {
        std::ifstream input(argv[1]);if(!input)throw std::runtime_error("missing source tokens");
        p.output=std::fopen(argv[2],"wb");if(!p.output)throw std::runtime_error("cannot open source frame output");std::fwrite("ZSTUD003",1,8,p.output);
        std::string line;int line_number=0;
        while(std::getline(input,line))
        {
            ++line_number;std::istringstream ss(line);std::string tag;ss>>tag;
            if(tag=="SOURCE") {int schema;ss>>schema>>p.winner>>p.first>>p.last>>p.emit_actor;if(schema!=1||p.first!=21||!((p.last==30&&p.emit_actor==6)||(p.last==2256&&p.emit_actor==-1)))throw std::runtime_error("unsupported bounded source profile");p.header=true;}
            else if(tag=="SOURCE_SHA") {ss>>p.source_sha;if(p.source_sha.size()!=64)throw std::runtime_error("source hash");}
            else if (tag == "MAP") { ss >> p.map.w >> p.map.h; p.map.flags.assign(static_cast<std::size_t>(p.map.w * p.map.h), 0); }
            else if (tag == "FLAGS")
            {
                std::string flags; ss >> flags; if (flags.size() != p.map.flags.size()) throw std::runtime_error("map flag size");
                for (std::size_t i = 0; i < flags.size(); ++i) p.map.flags[i] = static_cast<std::uint8_t>(flags[i] <= '9' ? flags[i] - '0' : flags[i] - 'a' + 10);
            }
            else if (tag == "SPAWN")
            {
                int team, n; ss >> team >> n;
                for (int i = 0; i < n; ++i) { int x,y; ss >> x >> y; Tile t{static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)}; p.map.spawn[team].push_back(t); p.map.flags[p.map.idx(t)] |= team == 0 ? tf_spawn_a : tf_spawn_b; }
            }
            else if (tag == "ALTARS") { int n; ss >> n; for (int i = 0; i < n; ++i) { int x,y; ss >> x >> y; p.map.altars.push_back({static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)}); } }
            else if (tag == "GROUP")
            {
                int id, n; Group g; ss >> id >> g.gold >> g.gems >> g.respawn >> n; p.group_ids[id] = static_cast<int>(p.map.groups.size());
                for (int i = 0; i < n; ++i) { int info,x,y,type; ss >> info >> x >> y >> type; g.members.push_back({{static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)},static_cast<std::uint8_t>(type)}); }
                p.map.groups.push_back(g);
            }
            else if (tag == "SETTINGS") { std::string hex; ss >> hex; p.settings(hex); }
            else if (tag == "DESIGN") { int type; std::string hex; ss >> type >> hex; p.design(type, hex); }
            else if (tag == "CONSUMABLEINFO") { int id; std::uint32_t cost,limit; ss >> id >> cost >> limit; const int typed = id == 0 ? 0 : id == 2 ? 1 : -1; if (typed < 0) throw std::runtime_error("unsupported consumable identity"); p.consumable_cost[typed]=cost; p.consumable_limit[typed]=limit; p.consumable_known[typed]=true; }
            else if (tag == "ACTOR") { int id,team,type,slot=0; ss >> id >> team >> type; for (const auto& [other,t] : p.actor_teams) slot += t == team; p.roster_slots[id]=slot; p.actor_teams[id]=team; }
            else if (tag == "BEGIN")
            {
                if (active) throw std::runtime_error("nested input"); active = true; p.src = source::SourceEvent{}; p.obs = Obs{}; p.facts=source::InverseFacts{};
                p.self_seen=p.shop_known=p.ward_known=p.priority_known=p.reservations_known=p.items_known=p.consumables_known=p.buffs_known=p.counts_known=p.origins_known=false;
                p.shop=p.consumable_shop=p.sell=false;p.vision.clear();p.explicit_shield=0;p.explicit_stunned=false;
                int fx,fy,tx,ty; ss >> p.src.sequence >> p.src.input >> p.src.kind >> p.src.round >> p.src.actor >> fx >> fy >> tx >> ty >> p.src.slot >> p.src.priority;
                p.src.from={static_cast<std::int16_t>(fx),static_cast<std::int16_t>(fy)}; p.src.to={static_cast<std::int16_t>(tx),static_cast<std::int16_t>(ty)};
                p.src.from_known=p.src.to_known=p.src.slot_known=p.src.priority_known=true;p.obs.round=p.src.round; p.obs.group_status.assign(p.map.groups.size(),gs_unknown); p.obs.altar_lock.assign(p.map.altars.size(),lk_none);p.obs.altar_lock_hits.assign(p.map.altars.size(),-1);
            }
            else if (tag == "SELF" || tag == "UNIT")
            {
                UnitView u; int team,restrictions,channel; std::uint32_t bits; read_unit(ss,u,team,bits,restrictions,channel); u.roster=p.roster_slots.at(u.id);
                if (tag == "SELF")
                {
                    p.self_seen=true;p.obs.self=u; p.obs.team=team; p.obs.alive=(bits&1u)!=0; p.obs.state_bits=bits; p.obs.restriction_bits=static_cast<std::uint8_t>(restrictions); p.obs.channel_remaining=std::max(0,channel);
                    p.obs.can_move=(bits&8u)!=0; p.obs.can_attack=(bits&16u)!=0; p.obs.can_cast=(bits&2048u)!=0; p.obs.recall_started=(bits&64u)!=0;
                }
                else (team == p.obs.team ? p.obs.allies : p.obs.enemies).push_back(u);
            }
            else if (tag == "ITEMS") { int n; ss >> n; if (n<0||n>kMaxItems) throw std::runtime_error("item count");p.items_known=n>=catalog().n_items; for (int i=0;i<n;++i) { ss >> p.obs.item_counts[i];  p.obs.items[i]=static_cast<std::uint8_t>(std::min(255u,p.obs.item_counts[i])); } }
            else if (tag == "CONSUMABLES") { int n; ss >> n; if (n<0||n>4) throw std::runtime_error("consumable count");p.consumables_known=n==4; for (int i=0;i<n;++i) ss >> p.facts.before_consumables[i]; p.obs.consumables[0]=p.facts.before_consumables[0]; p.obs.consumables[1]=p.facts.before_consumables[2]; }
            else if (tag == "CAMP")
            {
                int id,type,x,y,state; CampView c; ss >> id >> type >> x >> y >> c.hp >> state; c.id=id; c.at={static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)}; c.type=static_cast<std::uint8_t>(type); c.awake=state==1;
                // Static group membership is by spawn coordinate; runtime camp handles change on respawn.
                c.group=-1; for (std::size_t g=0;g<p.map.groups.size();++g) for (const auto& m:p.map.groups[g].members) if (m.at==c.at) c.group=static_cast<int>(g);
                if (c.group<0) throw std::runtime_error("visible camp has no static spawn group"); p.obs.camps.push_back(c);
            }
            else if (tag == "GROUPSTATE") { int id,state,cooldown; ss >> id >> state >> cooldown; const auto g=p.group_ids.find(id); if(g==p.group_ids.end()||state<1||state>3)throw std::runtime_error("group state identity"); p.obs.group_status[g->second]=state==1?gs_alive:state==2?gs_reward:gs_respawning; }
            else if (tag == "LOCK") { int team,x,y,hits,kind; std::uint32_t handle; ss >> team >> x >> y >> hits >> kind >> handle; bool found=false; for(std::size_t a=0;a<p.map.altars.size();++a) if(p.map.altars[a]==Tile{static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)}) {const auto lock=team==p.obs.team?lk_ours:lk_theirs;if(p.obs.altar_lock[a]!=lk_theirs||lock==lk_theirs){p.obs.altar_lock[a]=lock;p.obs.altar_lock_hits[a]=hits;}found=true;} if(!found)throw std::runtime_error("lock lacks altar"); }
            else if (tag == "BANKED") { std::uint32_t a,b; ss >> a >> b; p.obs.banked_us=p.obs.team==0?a:b; p.obs.banked_them=p.obs.team==0?b:a; }
            else if (tag == "VISION") { int n,value;ss>>n;for(int i=0;i<n;++i){ss>>value;p.vision.push_back(value);} }
            else if (tag == "RESERVED") { int n,x,y;ss>>n;for(int i=0;i<n;++i){ss>>x>>y;p.obs.reserved_tiles.push_back({static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)});}p.reservations_known=p.obs.reservation_facts=true; }
            else if (tag == "ALLYWARDS") { int n,x,y;ss>>n;for(int i=0;i<n;++i){ss>>x>>y;p.obs.ally_ward_tiles.push_back({static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)});}p.obs.ally_ward_facts=true; }
            else if (tag == "SHOPFLAGS") { ss >> p.shop >> p.consumable_shop >> p.sell; p.shop_known=true; }
            else if (tag == "WARDRESOURCES") { ss >> p.obs.ward_charges >> p.obs.ward_cooldown >> p.obs.priority >> p.priority_known; p.obs.ward_ready=p.obs.ward_charges>0; p.ward_known=true; }
            else if (tag == "COUNTS") { int a,b; ss >> a >> b; p.obs.allies_alive=p.obs.team==0?a:b; p.obs.enemies_alive=p.obs.team==0?b:a; p.counts_known=true; }
            else if (tag == "WARD") { int x,y; ss >> x >> y; p.obs.wards.push_back({static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)}); }
            else if (tag == "AFTER") { int n; ss >> p.src.after_gold >> n; if(n<0||n>kMaxItems)throw std::runtime_error("after item count");for(int i=0;i<n;++i)ss>>p.src.after_items[i];p.src.after_items_known=n>=catalog().n_items;ss>>n;if(n<0||n>4)throw std::runtime_error("after consumable count");for(int i=0;i<n;++i)ss>>p.src.after_consumables[i];p.src.after_consumables_known=n==4;p.src.after_gold_known=p.src.after_gold>=0; }
            else if(tag=="ORIGINS") {ss>>p.before_phase>>p.after_phase>>p.turn>>p.before_substep>>p.after_substep;p.origins_known=true;}
            else if(tag=="SELFBUFF") {ss>>p.explicit_shield>>p.explicit_stunned;p.buffs_known=true;}
            else if(tag=="ENDINPUT") {if(!active)throw std::runtime_error("input end without begin");p.finish_input();active=false;}
            else if(tag=="FINISH") {int count,first,last,actor;ss>>count>>first>>last>>actor;if(active||count!=p.processed||first!=p.first||last!=p.last||actor!=p.emit_actor||count!=last-first+1)throw std::runtime_error("source trailer");p.trailer=true;}
            else throw std::runtime_error("unknown raw source token "+tag);

            if(!ss)throw std::runtime_error("bad token fields line "+std::to_string(line_number));
            std::string extra;if(ss>>extra)throw std::runtime_error("extra token fields line "+std::to_string(line_number));
        }
        if(active||!p.header||!p.trailer)throw std::runtime_error("incomplete source stream");
        if(std::ferror(p.output))throw std::runtime_error("source frame output error");std::fclose(p.output);p.output=nullptr;
        p.report(argv[3]);std::ofstream intents(argv[4]);intents<<"{\"source_sha256\":"<<quoted(p.source_sha)<<",\"history_type\":\"sourceprefix+declared_no_goal/v1\",\"records\":"<<list(p.intents)<<'}';
        std::printf("source events %d units %zu CE frames %d\n",p.processed,p.units.size(),p.frames);return 0;
    }
    catch(const std::exception& error)
    {
        if(p.output)std::fclose(p.output);if(p.failure.empty())p.failure=error.what();p.report(argv[3]);
        std::ofstream intents(argv[4]);intents<<"{\"source_sha256\":"<<quoted(p.source_sha)<<",\"failure\":"<<quoted(error.what())<<",\"records\":"<<list(p.intents)<<'}';
        std::fprintf(stderr,"error source contract: %s\n",error.what());return 1;
    }
}
