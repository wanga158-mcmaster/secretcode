// Exact saved-input supervision. Python transports queried values; only shared C++ builds features/candidates.
// exact_records INPUT.tokens OUTPUT.bin COVERAGE.json
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "../src/core/action.h"
#include "../src/core/catalog.h"
#include "../src/core/draft_facts.h"
#include "../src/core/moves.h"
#include "../src/core/plan.h"
#include "../src/core/rules.h"
#include "../src/core/utility.h"

using namespace zero;
namespace {

struct Source
{
    int sequence{}, input{}, kind{}, round{}, actor{}, priority{};
    std::uint32_t slot{};
    Tile from{}, to{};
    std::int64_t after_gold{ -1 };
    std::array<std::uint32_t, kMaxItems> after_items{};
    std::array<std::uint32_t, 4> raw_consumables{}, after_consumables{};
    bool shop_known{}, shop{}, consumable_shop{}, sell{}, counts_known{}, priority_known{};
    bool explicit_buyable{}, explicit_utilities{};
    bool inventory_representable{ true }, reservations_known{}, ward_resources_known{};
};

struct DraftInput
{
    Source source;
    int team{},team_size{},step_round{};
    std::vector<int> ours,theirs;
};

std::string quote(const std::string& s)
{
    std::string out = "\"";
    for (char c : s) { if (c == '\\' || c == '"') out += '\\'; out += c; }
    return out + "\"";
}

struct Coverage
{
    int inputs{}, gameplay{}, labelled{}, rows{}, draft{}, draft_picks{}, draft_labelled{}, draft_receipts{}, settings_checked{}, designs_checked{}, annotations{}, goal_unknown_turns{};
    bool trailer{};
    std::map<std::string, int> failures, native_kinds, heads, families, truncated, example_counts, annotation_failures, annotation_skipped, annotation_diagnostics, utility_affected, nonpolicy, unrecoverable;
    int ward_complete{}, consume_complete{};
    std::string cold_goal_json;
    std::vector<std::string> examples;
    bool policy_input(const Source& s)
    {
        if (s.kind == 7) { ++nonpolicy["kind7_carrier"]; return false; }
        if (s.kind == 26) { ++nonpolicy["kind26_fuel_boundary"]; return false; }
        if (s.kind == 23) { ++unrecoverable["kind23_inverse_ambiguous_intent"]; return false; }
        // Unknown kinds stay eligible and fail their label build; never broaden the exclusions implicitly.
        return true;
    }
    int excluded_inputs() const
    {
        int count = 0;
        for (const auto& [_, n] : nonpolicy) count += n;
        for (const auto& [_, n] : unrecoverable) count += n;
        return count;
    }
    void fail(const std::string& reason, const Source& s)
    {
        ++failures[reason];
        if (example_counts[reason]++ < 3 && examples.size() < 64) examples.push_back("input " + std::to_string(s.input) + " actor " + std::to_string(s.actor) + " kind " + std::to_string(s.kind) + " slot " + std::to_string(s.slot) + " target " + std::to_string(s.to.x) + "," + std::to_string(s.to.y) + ": " + reason);
    }
    void write(const char* path)
    {
        std::ofstream out(path);
        if (!out) throw std::runtime_error("cannot open coverage output");
        auto dict = [&](const auto& values) {
            out << '{'; bool first = true;
            for (const auto& [key, value] : values) { if (!first) out << ','; first = false; out << quote(key) << ':' << value; }
            out << '}';
        };
        const int eligible = inputs - excluded_inputs(), source_labelled = labelled + draft_labelled;
        const bool facts_complete = failures.empty() && utility_affected.empty() && ward_complete == gameplay && consume_complete == gameplay;
        const bool covered = trailer && gameplay > 0 && facts_complete && annotation_failures.empty() && source_labelled == eligible && draft_labelled == draft_picks;
        out << "{\"schema\":\"zero-exact-records/v2\",\"coverage_pass\":" << (covered ? "true" : "false")
            << ",\"coverage_scope\":\"source_reference_eligible_policy\",\"source_reference_coverage_pass\":" << (covered ? "true" : "false")
            << ",\"source_facts_complete\":" << (facts_complete ? "true" : "false")
            << ",\"raw_retained_inputs\":" << inputs << ",\"raw_source_labelled_inputs\":" << source_labelled
            << ",\"eligible_policy_inputs\":" << eligible << ",\"eligible_policy_labelled_inputs\":" << source_labelled
            << ",\"excluded_inputs\":" << excluded_inputs() << ",\"nonpolicy_inputs\":"; dict(nonpolicy);
        out << ",\"unrecoverable_intent_inputs\":"; dict(unrecoverable);
        out
            << ",\"inputs\":" << inputs << ",\"gameplay_inputs\":" << gameplay << ",\"labelled_inputs\":" << labelled
            << ",\"records\":" << rows << ",\"draft_inputs\":" << draft << ",\"draft_pick_inputs\":" << draft_picks << ",\"draft_labelled_inputs\":" << draft_labelled << ",\"generic_draft_receipts_untrained\":" << draft_receipts << ",\"settings_checked\":" << settings_checked
            << ",\"designs_checked\":" << designs_checked << ",\"replan_annotations\":" << annotations << ",\"goal_unknown_turns\":" << goal_unknown_turns << ",\"failures\":"; dict(failures);
        out << ",\"native_kinds\":"; dict(native_kinds); out << ",\"heads\":"; dict(heads); out << ",\"families\":"; dict(families);
        out << ",\"truncated_argument_sets\":"; dict(truncated);
        out << ",\"annotation_failures\":"; dict(annotation_failures);
        out << ",\"annotation_skipped_unavailable\":";dict(annotation_skipped);
        out << ",\"annotation_diagnostics\":"; dict(annotation_diagnostics);
        out << ",\"cold_goal_example\":" << (cold_goal_json.empty()?"null":cold_goal_json);
        out << ",\"feature_parity_validated\":false,\"runtime_path_fidelity\":\"pending\"";
        out << ",\"utility_affected_limits\":";dict(utility_affected);
        out << ",\"utility_ward_complete_inputs\":"<<ward_complete<<",\"utility_consume_complete_inputs\":"<<consume_complete;
        out << ",\"examples\":[";
        for (std::size_t i = 0; i < examples.size(); ++i) { if (i) out << ','; out << quote(examples[i]); }
        out << "],\"issues\":[\"REPLAN labels are internal teacher annotations from future exact source actions, with a 4-round annotation period; they are not engine inputs\","
               "\"Allied shared-memory claims are unset in this initial exact converter\","
               "\"Native paths are complete BFS fields; cold runtime EnginePaths warming and observer state remain a pending parity contract\","
               "\"Draft labels use the final native table only as labels, keyed by BEFORE team and source stepRound; generic kind7 receipts have no policy label\","
               "\"Kind26 fuel boundaries and kind7 carriers are nonpolicy; kind23 inverse-ambiguous attempts retain observation/history without any supervised record\","
               "\"Eligible-policy coverage requires every remaining source input and every selected argument stage; unknown kinds and source/capability/descriptor gaps are hard failures\","
               "\"Ward and lock arguments use the shared utility contract; reported hole, reservation and TP limits remain explicit\"]}\n";
    }
};

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

struct Teacher
{
    Source source;
    UnitView self;
    int team{}, enemy_target{ -1 }, camp_target{ -1 };
    std::uint32_t state{};
};

// The prepass reads labels only. It never supplies an observation or candidate feature to a decision.
std::vector<Teacher> teacher_inputs(const char* path)
{
    std::ifstream in(path); std::string line; Teacher t; std::vector<Teacher> result;
    while (std::getline(in, line))
    {
        std::istringstream ss(line); std::string tag; ss >> tag;
        if (tag == "BEGIN")
        {
            t = Teacher{}; int fx,fy,tx,ty;
            ss >> t.source.sequence >> t.source.input >> t.source.kind >> t.source.round >> t.source.actor >> fx >> fy >> tx >> ty >> t.source.slot >> t.source.priority;
            t.source.to={static_cast<std::int16_t>(tx),static_cast<std::int16_t>(ty)};
        }
        else if (tag == "SELF") { int restrictions,channel; read_unit(ss,t.self,t.team,t.state,restrictions,channel); }
        else if (tag == "UNIT")
        {
            UnitView u; int team,restrictions,channel; std::uint32_t state; read_unit(ss,u,team,state,restrictions,channel);
            if (team != t.team && u.at == t.source.to) t.enemy_target=u.id;
        }
        else if (tag == "CAMP")
        {
            int id,type,x,y,state;float hp;ss>>id>>type>>x>>y>>hp>>state;
            if(hp>0.0f&&t.source.to==Tile{static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)})t.camp_target=id;
        }
        else if (tag == "ENDINPUT") result.push_back(t);
    }
    return result;
}

struct Producer
{
    Map map;
    std::map<int, int> group_ids, roster_slots, actor_teams;
    std::set<int> validated_designs;
    std::map<int, Memory> memories;
    std::vector<DraftInput> drafts;
    std::map<std::pair<int,int>,int> draft_labels;
    bool drafts_flushed{};
    std::array<std::uint32_t, 2> consumable_cost{}, consumable_limit{};
    std::array<bool, 2> consumable_known{};
    std::unique_ptr<BfsPaths> paths[2];
    bool finished_map{};
    int winner{ -1 };
    Obs obs;
    Source src;
    std::vector<Teacher> teacher;
    std::map<int,std::size_t> teacher_index;
    std::map<int,int> actor_round, last_annotation;
    std::map<int,int> command_counts;
    std::map<int,bool> replan_used;
    Coverage coverage;
    FILE* output{};

    void write(const CandSet& set, int label, const float* state, bool annotation=false,const Source* origin=nullptr,int origin_team=-1)
    {
        if (set.n <= 0 || (set.head != h_priority && set.head != h_quantity && (label < 0 || label >= set.n))) throw std::runtime_error("invalid supervised record");
        const auto& event=origin?*origin:src;const int team=origin_team>=0?origin_team:obs.team;
        const int hdr[9] = { set.head, set.n, label, kPhi[set.head], team, origin?event.round:obs.round, winner < 0 ? -1 : (winner == team ? 1 : 0), annotation ? -event.sequence-1 : event.sequence, event.input };
        for (int i = 0; i < kState; ++i) if (!std::isfinite(state[i])) throw std::runtime_error("nonfinite state");
        std::fwrite(hdr, sizeof(int), 9, output); std::fwrite(state, sizeof(float), kState, output);
        for (int i = 0; i < set.n; ++i)
        {
            for (int j = 0; j < kPhi[set.head]; ++j) if (!std::isfinite(set.c[i].phi[j])) throw std::runtime_error("nonfinite candidate");
            std::fwrite(set.c[i].phi, sizeof(float), static_cast<std::size_t>(kPhi[set.head]), output);
        }
        ++coverage.rows; ++coverage.heads[std::to_string(set.head)];
    }

    void flush_drafts()
    {
        if(drafts_flushed)return;drafts_flushed=true;
        for(const auto& draft:drafts)
        {
            if(draft.source.kind!=19)continue;
            bool designs=true;for(int pick=0;pick<12;++pick)designs&=validated_designs.contains(pick<=3?pick:pick+1);
            if(!designs){coverage.fail("missing_draft_candidate_designs",draft.source);continue;}
            const auto label=draft_labels.find({draft.team,draft.step_round});
            if(label==draft_labels.end()){coverage.fail("missing_completed_native_draft_label",draft.source);continue;}
            float state[kState];CandSet candidates;draft_facts(map,draft.team_size,draft.ours,draft.theirs,state,candidates);
            int selected=-1;for(int i=0;i<candidates.n;++i)if(candidates.c[i].target==label->second)selected=i;
            if(selected<0){coverage.fail("draft_label_not_represented",draft.source);continue;}
            write(candidates,selected,state,false,&draft.source,draft.team);++coverage.draft_labelled;
        }
    }

    bool spawn(Tile t, int team) const
    {
        return map.inside(t) && (map.at(t) & (team == 0 ? tf_spawn_a : tf_spawn_b));
    }

    bool recall_completes(std::size_t at) const
    {
        const auto& start=teacher[at]; bool saw_channel=false;
        for(std::size_t i=at+1;i<teacher.size();++i)
        {
            const auto& next=teacher[i];
            if(next.source.round>start.source.round+catalog().settings.recall_time+1)break;
            if(next.source.actor!=start.source.actor)continue;
            if(!(next.state&1u))return false;
            if(saw_channel && !(next.state&2u) && spawn(next.self.at,next.team))return true;
            if(next.source.kind==16)return false;
            if(next.self.at!=start.self.at)return false;
            saw_channel|=(next.state&2u)!=0;
        }
        return false;
    }

    bool infer_goal(std::size_t at, int& kind, int& target, std::size_t& evidence) const
    {
        const auto& start=teacher[at]; kind=-1;target=-1;
        for(std::size_t i=at;i<teacher.size();++i)
        {
            const auto& t=teacher[i];
            if(t.source.round>start.source.round+80)break;
            if(t.source.actor!=start.source.actor)continue;
            if(t.source.kind==11 && recall_completes(i)){kind=g_recall;evidence=i;return true;}
            if(t.source.kind==12)
                for(std::size_t a=0;a<map.altars.size();++a)if(map.altars[a]==t.source.to){kind=g_bank;target=static_cast<int>(a);evidence=i;return true;}
            if(t.source.kind==5)
            {
                // A spawn coordinate alone does not identify a living camp: an enemy can stand there after
                // the camp dies. Exact personal pre-input occupants identify which entity was attacked.
                if(t.camp_target>=0&&map.inside(t.source.to))
                {
                    const int group=map.group_of[map.idx(t.source.to)];
                    if(group>=0){kind=g_farm;target=group;evidence=i;return true;}
                }
                if(t.enemy_target>=0){kind=g_engage;target=t.enemy_target;evidence=i;return true;}
            }
        }
        if(spawn(start.self.at,start.team))
        {
            int last=start.source.round;
            for(std::size_t i=at;i<teacher.size();++i)
            {
                const auto& t=teacher[i];
                if(t.source.round>start.source.round+20)break;
                if(t.source.actor!=start.source.actor)continue;
                if(!(t.state&1u)||!spawn(t.self.at,t.team))return false;
                last=t.source.round;
            }
            if(last>=start.source.round+20){kind=g_hold;evidence=at;return true;}
        }
        return false;
    }

    void annotate_goal(Context& ctx, bool first)
    {
        if(!first||!obs.alive)return;
        int kind,target;std::size_t evidence=0;
        if(!infer_goal(teacher_index.at(src.input),kind,target,evidence)){++coverage.goal_unknown_turns;return;}
        auto& memory=memories[src.actor];
        const bool changed=kind!=memory.goal_kind||target!=memory.goal_target;
        if(!changed&&last_annotation.contains(src.actor)&&obs.round-last_annotation[src.actor]<4)return;
        CandSet root,goals; action_candidates(ctx,root,kAllActions); goal_candidates(ctx,goals);
        int controller=-1,legacy_label=-1;
        for(int i=0;i<root.n;++i)if(root.c[i].kind==a_replan)controller=i;
        for(int i=0;i<goals.n;++i)if(goals.c[i].kind==kind&&(kind==g_recall||kind==g_hold||goals.c[i].target==target))legacy_label=i;
        if(legacy_label<0)++coverage.annotation_diagnostics["flat_goal_omissions_kind_"+std::to_string(kind)];
        GoalCursor cursor;plan_begin(ctx,cursor);
        std::vector<std::pair<CandSet,int>> decisions;
        bool plan_ok=controller>=0;
        for(int depth=0;plan_ok&&cursor.stage!=GoalStage::done&&depth<16;++depth)
        {
            CandSet node;const auto build=plan_candidates(ctx,cursor,node);
            const int selected=plan_teacher_index(ctx,cursor,static_cast<GoalKind>(kind),target);
            if(!build.usable()||selected<0||selected>=node.n){plan_ok=false;break;}
            decisions.emplace_back(node,selected);
            if(!plan_select(ctx,cursor,node.c[selected]))plan_ok=false;
        }
        plan_ok=plan_ok&&cursor.stage==GoalStage::done&&cursor.kind==kind&&cursor.target==target;
        if(coverage.cold_goal_json.empty())
        {
            const auto& e=teacher[evidence];std::ostringstream d;
            d<<"{\"input\":"<<src.input<<",\"actor\":"<<src.actor<<",\"self\":["<<obs.self.at.x<<','<<obs.self.at.y<<"],\"goal\":["<<kind<<','<<target<<"],\"evidence\":{\"input\":"<<e.source.input<<",\"round\":"<<e.source.round<<",\"kind\":"<<e.source.kind<<",\"at\":["<<e.source.to.x<<','<<e.source.to.y<<"],\"visible_camp_id\":"<<e.camp_target<<",\"visible_enemy_id\":"<<e.enemy_target<<"},\"offered\":[";
            for(int i=0;i<goals.n;++i){if(i)d<<',';d<<'['<<static_cast<int>(goals.c[i].kind)<<','<<goals.c[i].target<<']';}
            d<<"],\"tree_routed\":"<<(plan_ok?"true":"false")<<",\"tree_rows\":"<<decisions.size()<<",\"groups\":[";
            for(std::size_t g=0;g<map.groups.size();++g)
            {
                if(g)d<<',';int native=-1;for(const auto& [id,index]:group_ids)if(index==static_cast<int>(g))native=id;
                d<<"{\"index\":"<<g<<",\"native_id\":"<<native<<",\"center\":["<<map.groups[g].center.x<<','<<map.groups[g].center.y<<"],\"cheb\":"<<cheb(map.groups[g].center,obs.self.at)<<",\"bfs\":"<<ctx.paths->to_group(static_cast<int>(g),obs.self.at)<<",\"status\":"<<static_cast<int>(obs.group_status[g])<<'}';
            }
            d<<"]}";coverage.cold_goal_json=d.str();
        }
        if(!plan_ok)
        {
            const std::string reason=controller<0?"replan_unavailable":"teacher_goal_not_represented_kind_"+std::to_string(kind);
            bool causal_unavailable=kind==g_bank&&obs.self.gems==0;
            causal_unavailable|=kind==g_recall&&obs.on_spawn;
            if(kind==g_engage){bool visible=false;for(const auto& e:obs.enemies)visible|=e.id==target;causal_unavailable|=!visible;}
            if(controller>=0&&causal_unavailable)++coverage.annotation_skipped[reason];
            else ++coverage.annotation_failures[reason];
            if(coverage.example_counts[reason]++<3)coverage.examples.push_back("annotation before input "+std::to_string(src.input)+" goal "+std::to_string(kind)+"/"+std::to_string(target)+": "+reason);
            if(kind==g_farm&&target>=0&&target<static_cast<int>(map.groups.size()))
            {
                const int dist=ctx.paths->to_group(target,obs.self.at),straight=cheb(map.groups[target].center,obs.self.at);int rank=1,near_rank=1;
                for(std::size_t g=0;g<map.groups.size();++g){rank+=ctx.paths->to_group(static_cast<int>(g),obs.self.at)<dist;near_rank+=cheb(map.groups[g].center,obs.self.at)<straight;}
                ++coverage.annotation_diagnostics["farm_bfs_rank_"+std::to_string(rank)];++coverage.annotation_diagnostics["farm_cheb_rank_"+std::to_string(near_rank)];
                ++coverage.annotation_diagnostics[obs.group_status[target]==gs_unknown?"farm_currently_unseen":"farm_currently_seen"];
                if(dist>=kUnreachable)
                {
                    ++coverage.annotation_diagnostics["farm_unreachable"];
                    if(map.inside(obs.self.at)&&map.group_of[map.idx(obs.self.at)]>=0)++coverage.annotation_diagnostics["farm_unreachable_from_camp_spawn_tile"];
                }
            }
            if(kind==g_engage)
            {
                int rank=1,distance=-1;for(const auto& e:obs.enemies)if(e.id==target)distance=cheb(e.at,obs.self.at);
                if(distance<0)++coverage.annotation_diagnostics["engage_currently_unseen"];
                else
                {
                    int equal=0;for(const auto& e:obs.enemies){rank+=cheb(e.at,obs.self.at)<distance;equal+=cheb(e.at,obs.self.at)==distance;}
                    ++coverage.annotation_diagnostics["engage_visible_rank_"+std::to_string(rank)];
                    ++coverage.annotation_diagnostics["engage_visible_rank_max_"+std::to_string(rank+equal-1)];
                }
            }
            if(kind==g_bank)
            {
                ++coverage.annotation_diagnostics[obs.self.gems==0?"bank_current_gems_zero":"bank_current_gems_positive"];
                if(obs.self.gems>0)
                {
                    const int d=ctx.paths->to_altar(target,obs.self.at);
                    if(d>=kUnreachable)++coverage.annotation_diagnostics["bank_gems_positive_unreachable"];
                    if(map.inside(obs.self.at)&&map.group_of[map.idx(obs.self.at)]>=0)++coverage.annotation_diagnostics["bank_gems_positive_from_camp_spawn_tile"];
                }
            }
            if(kind==g_recall)++coverage.annotation_diagnostics[obs.on_spawn?"recall_current_on_spawn":"recall_current_away_from_spawn"];
            return;
        }
        float state[kState];state_vector(ctx,state);write(root,controller,state,true);
        for(const auto& [node,selected]:decisions)write(node,selected,state,true);
        if(changed)memory.goal_since=obs.round;
        memory.goal_kind=kind;memory.goal_target=target;memory.goal_decided=obs.round;memory.goal_recall_p=kind==g_recall?1.0f:0.0f;
        last_annotation[src.actor]=obs.round;replan_used[src.actor]=true;++coverage.annotations;
        ++obs.command_index;  // one completed internal command, regardless of the number of plan nodes
    }

    int purchase_target(bool sell, bool consumable, int* quantity=nullptr) const
    {
        if (src.after_gold < 0) return -1;
        int result = -1, matches = 0, bought = 0;
        if (consumable)
        {
            for (int item = 0; item < 2; ++item)
            {
                const int raw_id = item == 0 ? 0 : 2;
                if (!consumable_known[item] || obs.self.gold < consumable_cost[item]) continue;
                auto after = src.raw_consumables; ++after[raw_id];
                if (after == src.after_consumables && obs.item_counts == src.after_items && src.after_gold == static_cast<std::int64_t>(obs.self.gold - consumable_cost[item])) { result = item; bought=1; ++matches; }
            }
        }
        else for (int item = 0; item < catalog().n_items && item < kMaxItems; ++item)
        {
            const auto& info = catalog().item(item);
            auto after = obs.item_counts;
            std::int64_t gold = obs.self.gold;
            int count=1;
            if (sell) { if (!after[item]) continue; --after[item]; gold += info.cost; }
            else
            {
                if (!info.purchasable || info.cost==0 || src.after_gold>=gold) continue;
                const std::int64_t spent=gold-src.after_gold;
                if(spent%info.cost!=0||spent/info.cost>std::numeric_limits<int>::max())continue;
                count=static_cast<int>(spent/info.cost);
                if(count<=0||static_cast<std::uint64_t>(after[item])+static_cast<std::uint32_t>(count)>std::numeric_limits<std::uint32_t>::max())continue;
                after[item]+=static_cast<std::uint32_t>(count); gold-=static_cast<std::int64_t>(count)*info.cost;
                if (info.combine_count > 0 && after[item] >= info.combine_count && info.result >= 0)
                {
                    const auto combined=after[item]/info.combine_count;
                    after[item]%=info.combine_count;after[info.result]+=combined;
                }
            }
            if (after == src.after_items && src.raw_consumables == src.after_consumables && gold == src.after_gold) { result = item; bought=count; ++matches; }
        }
        if(quantity)*quantity=matches==1?bought:0;
        return matches == 1 ? result : -1;
    }

    int family()
    {
        switch (src.kind)
        {
        case 4: return a_move;
        case 5: return a_attack;
        case 6: return src.slot == 0 ? a_cast0 : (src.slot == 1 ? a_cast1 : -1);
        case 8: return a_end;
        case 9: return a_priority;
        case 11: return a_recall;
        case 12: return a_tribute;
        case 13:
            if (purchase_target(false, false) >= 0) return a_buy;
            if (purchase_target(false, true) >= 0) return a_buy_consumable;
            return -1;
        case 14: return a_ward;
        case 16: return a_cancel;
        case 20: case 21: return a_use_consumable;
        case 25: return a_sell;
        default: return -1;
        }
    }

    int argument_label(ActionType type, const CandSet& set)
    {
        if (type == a_priority) return src.priority;  // signed scalar target; never copied into candidate facts
        int label = -1, matches = 0;
        const int item = type == a_buy ? purchase_target(false, false) : type == a_buy_consumable ? purchase_target(false, true) : type == a_sell ? purchase_target(true, false) : -1;
        for (int i = 0; i < set.n; ++i)
        {
            const auto& c = set.c[i]; bool match = false;
            switch (type)
            {
            case a_end: case a_recall: case a_cancel: match = set.n == 1; break;
            case a_buy: case a_buy_consumable: case a_sell: match = item >= 0 && c.target == item; break;
            case a_priority: match = c.target == src.priority; break;
            case a_cast0: case a_cast1: {
                const auto kind = catalog().champion(obs.self.type).ability[type == a_cast0 ? 0 : 1].kind;
                // Reset/shield are typed self commands; the replay's unused endpoint need not be the actor tile.
                match = kind == ab_reset || kind == ab_shield ? set.n == 1 : c.at == src.to;
                break;
            }
            default: match = c.at == src.to; break;
            }
            if (match) { label = i; ++matches; }
        }
        return matches == 1 ? label : -1;
    }

    void finish_input()
    {
        ++coverage.inputs; ++coverage.gameplay; ++coverage.native_kinds[std::to_string(src.kind)];
        const bool eligible = coverage.policy_input(src);
        const bool first=!actor_round.contains(src.actor)||actor_round[src.actor]!=obs.round;
        if(first){actor_round[src.actor]=obs.round;replan_used[src.actor]=false;command_counts[src.actor]=0;}
        obs.command_index=command_counts[src.actor];
        struct AdvanceCommand
        {
            Producer& producer;
            ~AdvanceCommand()
            {
                const int actor=producer.src.actor,kind=producer.src.kind;
                if(kind==8||kind==26||kind==27)
                {
                    producer.command_counts[actor]=0;
                    producer.actor_round.erase(actor);
                    producer.replan_used.erase(actor);
                }
                else if(kind!=7) producer.command_counts[actor]=producer.obs.command_index+1;
            }
        } advance{*this};
        if (!finished_map)
        {
            map.finish(); paths[0] = std::make_unique<BfsPaths>(map, 0); paths[1] = std::make_unique<BfsPaths>(map, 1); finished_map = true;
            flush_drafts();
        }
        if (!src.counts_known) { coverage.fail("missing_exact_alive_counts", src); return; }
        if (!src.shop_known) { coverage.fail("missing_exact_shop_permissions", src); return; }
        if (!src.ward_resources_known) { coverage.fail("missing_exact_ward_resources", src); return; }
        if (!src.priority_known) { coverage.fail("missing_exact_next_priority", src); return; }
        if (!src.inventory_representable) { coverage.fail("item_count_exceeds_shared_feature_width", src); return; }
        if (!validated_designs.contains(obs.self.type)) { coverage.fail("missing_queried_champion_design", src); return; }
        if (!src.reservations_known) coverage.fail("missing_exact_teleport_reservations", src);
        if (!obs.ally_ward_facts) coverage.fail("missing_exact_allied_ward_tiles",src);
        if (coverage.settings_checked == 0) { coverage.fail("missing_runtime_settings_validation", src); return; }
        obs.command_facts = true;
        obs.revision = static_cast<std::uint64_t>(src.input);
        obs.on_spawn = obs.alive && map.inside(obs.self.at) && (map.at(obs.self.at) & (obs.team == 0 ? tf_spawn_a : tf_spawn_b));
        derive_command_state(obs);
        const auto& design = catalog().champion(obs.self.type);
        for (int k = 0; k < 2; ++k) obs.can_cast_slot[k] = obs.can_cast && obs.self.cooldown[k] == 0 && design.ability[k].kind != ab_none;
        if (!src.explicit_buyable)
            for (int i = 0; i < catalog().n_items && i < kMaxItems; ++i)
                obs.buyable_items[i] = src.shop && catalog().item(i).purchasable && obs.self.gold >= catalog().item(i).cost;
        for (bool item : obs.buyable_items) obs.can_buy |= item;
        if (!src.explicit_utilities)
        {
            obs.utility_known_mask = action_bit(a_sell);
            if (src.sell)
                for (int i = 0; i < catalog().n_items && i < kMaxItems; ++i)
                    if (obs.item_counts[i] && static_cast<std::uint64_t>(obs.self.gold) + catalog().item(i).cost <= std::numeric_limits<std::uint32_t>::max())
                        append_sell_utility(obs, a_sell, i, catalog().item(i).cost);
            if (consumable_known[0] && consumable_known[1])
            {
                obs.utility_known_mask |= action_bit(a_buy_consumable);
                for (int i = 0; i < 2; ++i)
                    if (src.consumable_shop && obs.self.gold >= consumable_cost[i] && obs.consumables[i] < consumable_limit[i])
                        append_buy_consumable_utility(obs, a_buy_consumable, i, consumable_cost[i]);
            }
            if (src.priority_known) { obs.utility_known_mask |= action_bit(a_priority); obs.can_priority = (obs.state_bits&32u)==0; }
        }
        obs.reservation_facts=src.reservations_known;
        const UtilityRules utility_rules{static_cast<float>(catalog().settings.ward_placement_radius),2.0f,consumable_cost[1],consumable_known[1]?kConsumeLock:0u};
        const auto utility=append_ground_utilities(map,obs,utility_rules);
        obs.utility_known_consumables=utility.known_consume_mask;obs.utility_capability_limits=utility.limitations;obs.utility_affected_limits=utility.affected_limits;
        obs.ward_arguments_complete=utility.ward_complete;obs.consumable_arguments_complete=utility.consume_complete;
        coverage.ward_complete+=utility.ward_complete?1:0;coverage.consume_complete+=utility.consume_complete?1:0;
        for(unsigned bit=0;bit<7;++bit)if(utility.affected_limits&(1u<<bit))++coverage.utility_affected[std::to_string(1u<<bit)];
        int dash = 0;
        for (int k = 0; k < 2; ++k) if (obs.can_cast_slot[k] && design.ability[k].kind == ab_dash) dash = std::max(dash, static_cast<int>(design.ability[k].range));
        canonical_observation_order(obs);
        legal_moves(map, obs, dash, false);  // match the SDK: enumerate a selected dash, never an unchosen family
        auto& memory = memories[src.actor]; observe(obs, map, memory);
        Context ctx{ &map, paths[obs.team].get(), &obs, &memory };
        if (!eligible) return;  // exact observation/history above remains causal; no source or internal CE here
        annotate_goal(ctx,first);
        const int action = family();
        if (action < 0) { coverage.fail(src.kind == 13 ? "ambiguous_or_unsupported_purchase" : "unsupported_native_input_" + std::to_string(src.kind), src); return; }
        const auto type = static_cast<ActionType>(action);
        ++coverage.families[action_name(type)];
        CandSet root, arguments;
        action_candidates(ctx, root, replan_used[src.actor]?kAllActions&~action_bit(a_replan):kAllActions);
        int controller_label = -1;
        for (int i = 0; i < root.n; ++i) if (root.c[i].kind == action) controller_label = i;
        if (controller_label < 0) { coverage.fail("missing_controller_family_" + std::string(action_name(type)), src); return; }
        if(spatial_action(ctx,type))
        {
            std::vector<SpatialTile> legal;
            const auto space=spatial_argument_tiles(ctx,type,legal);
            if(!space.usable()){coverage.fail("spatial_tiles_"+std::to_string(static_cast<int>(space.status))+"_"+action_name(type),src);return;}
            const int payload=type==a_use_consumable?(src.kind==20?1:src.kind==21?0:-2):-1;
            CandSet columns,rows;
            const auto xbuild=spatial_argument_candidates(ctx,type,legal,SpatialStage::column,{},columns);
            if(!xbuild.usable()){coverage.fail("spatial_columns_"+std::to_string(static_cast<int>(xbuild.status))+"_"+action_name(type),src);return;}
            int x=-1;for(int i=0;i<columns.n;++i)if(columns.c[i].target==payload&&columns.c[i].at.x==src.to.x)x=i;
            if(x<0){coverage.fail("missing_spatial_column_"+std::string(action_name(type)),src);return;}
            const SpatialColumn selected{payload,src.to.x};
            const auto ybuild=spatial_argument_candidates(ctx,type,legal,SpatialStage::row,selected,rows);
            if(!ybuild.usable()){coverage.fail("spatial_rows_"+std::to_string(static_cast<int>(ybuild.status))+"_"+action_name(type),src);return;}
            int y=-1;for(int i=0;i<rows.n;++i)if(rows.c[i].target==payload&&rows.c[i].at==src.to)y=i;
            if(y<0){coverage.fail("missing_spatial_row_"+std::string(action_name(type)),src);return;}
            float state[kState];state_vector(ctx,state);
            write(root,controller_label,state);write(columns,x,state);write(rows,y,state);++coverage.labelled;return;
        }
        const auto build = argument_candidates(ctx, type, arguments);
        if (!build.usable()) { coverage.fail("argument_build_" + std::to_string(static_cast<int>(build.status)) + "_" + action_name(type), src); return; }
        if (build.status == ActionBuildStatus::truncated) ++coverage.truncated[action_name(type)];
        const int label = argument_label(type, arguments);
        if (arguments.head != h_priority && label < 0) { coverage.fail("missing_or_ambiguous_argument_" + std::string(action_name(type)), src); return; }
        CandSet quantity;int count=0;
        if(type==a_buy)
        {
            const int item=purchase_target(false,false,&count);
            const auto quantity_build=quantity_candidates(ctx,item,quantity);
            if(!quantity_build.usable()||count<1){coverage.fail("unsupported_buy_quantity",src);return;}
        }
        float state[kState]; state_vector(ctx, state);
        write(root, controller_label, state); write(arguments, label, state);
        if(type==a_buy)write(quantity,count,state);
        ++coverage.labelled;
    }

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
};

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 4) { std::fprintf(stderr, "usage: exact_records INPUT.tokens OUTPUT.bin COVERAGE.json\n"); return 2; }
    Producer p;
    try
    {
        std::ifstream in(argv[1]); if (!in) throw std::runtime_error("cannot open tokens");
        p.teacher=teacher_inputs(argv[1]);for(std::size_t i=0;i<p.teacher.size();++i)p.teacher_index[p.teacher[i].source.input]=i;
        p.output = std::fopen(argv[2], "wb"); if (!p.output) throw std::runtime_error("cannot open records");
        std::string line; bool active = false; int line_no = 0;
        while (std::getline(in, line))
        {
            ++line_no; std::istringstream ss(line); std::string tag; ss >> tag;
            if (tag == "EXACT") { int schema; ss >> schema >> p.winner; if (schema != 2) throw std::runtime_error("token schema"); }
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
            else if (tag == "SKIP") { Source s; ss >> s.sequence >> s.input >> s.kind >> s.actor >> s.round; ++p.coverage.inputs; ++p.coverage.draft; ++p.coverage.native_kinds[std::to_string(s.kind)];p.coverage.policy_input(s);if(s.kind==19){++p.coverage.draft_picks;p.coverage.fail("missing_exact_draft_before_facts",s);}else if(s.kind==7)++p.coverage.draft_receipts;else p.coverage.fail("unsupported_native_draft_kind_"+std::to_string(s.kind),s); }
            else if (tag == "DRAFT")
            {
                DraftInput d;int n;ss>>d.source.sequence>>d.source.input>>d.source.kind>>d.source.round>>d.team>>d.team_size>>d.step_round>>n;
                if(d.team<0||d.team>1||d.team_size<1||n<0||n>d.team_size||d.step_round!=d.source.round)throw std::runtime_error("draft source facts");
                for(int i=0;i<n;++i){int pick;ss>>pick;d.ours.push_back(pick);}ss>>n;if(n<0||n>d.team_size)throw std::runtime_error("draft prefix size");
                for(int i=0;i<n;++i){int pick;ss>>pick;d.theirs.push_back(pick);}d.source.actor=-1;p.drafts.push_back(d);
                ++p.coverage.inputs;++p.coverage.draft;++p.coverage.native_kinds[std::to_string(d.source.kind)];p.coverage.policy_input(d.source);if(d.source.kind==19)++p.coverage.draft_picks;else if(d.source.kind==7)++p.coverage.draft_receipts;else p.coverage.fail("unsupported_native_draft_kind_"+std::to_string(d.source.kind),d.source);
            }
            else if (tag == "DRAFTLABEL") { int team,round,pick;ss>>team>>round>>pick;if(team<0||team>1||round<0||pick<0||pick>=12||p.draft_labels.contains({team,round}))throw std::runtime_error("completed draft label table");p.draft_labels[{team,round}]=pick; }
            else if (tag == "BEGIN")
            {
                if (active) throw std::runtime_error("nested input"); active = true; p.src = Source{}; p.obs = Obs{};
                int fx,fy,tx,ty; ss >> p.src.sequence >> p.src.input >> p.src.kind >> p.src.round >> p.src.actor >> fx >> fy >> tx >> ty >> p.src.slot >> p.src.priority;
                p.src.from={static_cast<std::int16_t>(fx),static_cast<std::int16_t>(fy)}; p.src.to={static_cast<std::int16_t>(tx),static_cast<std::int16_t>(ty)};
                p.obs.round=p.src.round; p.obs.group_status.assign(p.map.groups.size(),gs_unknown); p.obs.altar_lock.assign(p.map.altars.size(),lk_none);p.obs.altar_lock_hits.assign(p.map.altars.size(),-1);
            }
            else if (tag == "SELF" || tag == "UNIT")
            {
                UnitView u; int team,restrictions,channel; std::uint32_t bits; read_unit(ss,u,team,bits,restrictions,channel); u.roster=p.roster_slots.at(u.id);
                if (tag == "SELF")
                {
                    p.obs.self=u; p.obs.team=team; p.obs.alive=(bits&1u)!=0; p.obs.state_bits=bits; p.obs.restriction_bits=static_cast<std::uint8_t>(restrictions); p.obs.channel_remaining=std::max(0,channel);
                    p.obs.can_move=(bits&8u)!=0; p.obs.can_attack=(bits&16u)!=0; p.obs.can_cast=(bits&2048u)!=0; p.obs.recall_started=(bits&64u)!=0;
                }
                else (team == p.obs.team ? p.obs.allies : p.obs.enemies).push_back(u);
            }
            else if (tag == "ITEMS") { int n; ss >> n; if (n>kMaxItems) throw std::runtime_error("item count"); for (int i=0;i<n;++i) { ss >> p.obs.item_counts[i]; p.src.inventory_representable &= p.obs.item_counts[i]<=255u; p.obs.items[i]=static_cast<std::uint8_t>(std::min(255u,p.obs.item_counts[i])); } }
            else if (tag == "CONSUMABLES") { int n; ss >> n; if (n>4) throw std::runtime_error("consumable count"); for (int i=0;i<n;++i) ss >> p.src.raw_consumables[i]; p.obs.consumables[0]=p.src.raw_consumables[0]; p.obs.consumables[1]=p.src.raw_consumables[2]; }
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
            else if (tag == "VISION") { int n,value; ss >> n; for(int i=0;i<n;++i)ss >> value; }
            else if (tag == "RESERVED") { int n,x,y;ss>>n;for(int i=0;i<n;++i){ss>>x>>y;p.obs.reserved_tiles.push_back({static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)});}p.src.reservations_known=p.obs.reservation_facts=true; }
            else if (tag == "ALLYWARDS") { int n,x,y;ss>>n;for(int i=0;i<n;++i){ss>>x>>y;p.obs.ally_ward_tiles.push_back({static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)});}p.obs.ally_ward_facts=true; }
            else if (tag == "SHOPFLAGS") { ss >> p.src.shop >> p.src.consumable_shop >> p.src.sell; p.src.shop_known=true; }
            else if (tag == "WARDRESOURCES") { ss >> p.obs.ward_charges >> p.obs.ward_cooldown >> p.obs.priority >> p.src.priority_known; p.obs.ward_ready=p.obs.ward_charges>0; p.src.ward_resources_known=true; }
            else if (tag == "COUNTS") { int a,b; ss >> a >> b; p.obs.allies_alive=p.obs.team==0?a:b; p.obs.enemies_alive=p.obs.team==0?b:a; p.src.counts_known=true; }
            else if (tag == "WARD") { int x,y; ss >> x >> y; p.obs.wards.push_back({static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)}); }
            else if (tag == "COMMAND") { ss >> p.obs.can_recall >> p.obs.can_tribute >> p.obs.can_cancel >> p.obs.can_buy >> p.obs.ward_charges >> p.obs.ward_cooldown >> p.obs.priority >> p.obs.utility_known_mask; p.src.priority_known=p.src.ward_resources_known=true; }
            else if (tag == "BUYABLE") { int n,id; ss >> n; for(int i=0;i<n;++i){ss>>id;if(id<0||id>=kMaxItems)throw std::runtime_error("buyable item");p.obs.buyable_items[id]=true;}p.src.explicit_buyable=true; }
            else if (tag == "UTILITY") { int type,x,y; UtilityAction a; ss >> type >> a.target >> x >> y; a.type=static_cast<std::uint8_t>(type);a.at={static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)};for(float& v:a.phi)ss>>v;p.obs.utility_actions.push_back(a);p.src.explicit_utilities=true; }
            else if (tag == "AFTER") { int n; ss >> p.src.after_gold >> n; if(n<0||n>kMaxItems)throw std::runtime_error("after item count");for(int i=0;i<n;++i)ss>>p.src.after_items[i];ss>>n;if(n<0||n>4)throw std::runtime_error("after consumable count");for(int i=0;i<n;++i)ss>>p.src.after_consumables[i]; }
            else if (tag == "ENDINPUT") { if(!active)throw std::runtime_error("input end without begin"); p.finish_input();active=false; }
            else if (tag == "FINISH") { int records,observed,nonready,pass;ss>>records>>observed>>nonready>>pass;if(active||records!=p.coverage.inputs||records!=observed||nonready||!pass)throw std::runtime_error("export trailer mismatch");p.coverage.trailer=true; }
            else throw std::runtime_error("unknown token tag at line "+std::to_string(line_no)+": "+tag);
            if (!ss) throw std::runtime_error("bad token fields at line "+std::to_string(line_no));
            std::string extra; if (ss >> extra) throw std::runtime_error("extra token fields at line "+std::to_string(line_no));
        }
        if(active||!p.coverage.trailer)throw std::runtime_error("incomplete token stream");
        if(std::ferror(p.output))throw std::runtime_error("record output write failed");
        const int closed=std::fclose(p.output);p.output=nullptr;if(closed!=0)throw std::runtime_error("record output close failed");p.coverage.write(argv[3]);
        std::printf("inputs %d gameplay %d labelled %d records %d failure-kinds %zu\n",p.coverage.inputs,p.coverage.gameplay,p.coverage.labelled,p.coverage.rows,p.coverage.failures.size());
        return 0;
    }
    catch(const std::exception& e)
    {
        if(p.output)std::fclose(p.output);std::fprintf(stderr,"error exact_records: %s\n",e.what());return 1;
    }
}
