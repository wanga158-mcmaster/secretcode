#pragma once
#include "sdk.h"
#include <memory>
#ifndef P9_NAV_DIAGNOSTICS
#define P9_NAV_DIAGNOSTICS 0
#endif
#ifndef P9_NAV_DIAG_FROM
#define P9_NAV_DIAG_FROM 0
#endif
#ifndef P9_NAV_DIAG_TO
#define P9_NAV_DIAG_TO 2000
#endif
#ifndef P9_NAV_DIAG_ACTOR
#define P9_NAV_DIAG_ACTOR -1
#endif
#ifndef P9_NAV_ENTRY_TRACE
#define P9_NAV_ENTRY_TRACE 0
#endif
#ifndef P9_NAV_ENTRY_RESUME
#define P9_NAV_ENTRY_RESUME 0
#endif
#if P9_NAV_DIAGNOSTICS || P9_NAV_ENTRY_TRACE
#include <cstdio>
#include <iostream>
#endif

namespace p9::live {
#if P9_NAV_ENTRY_TRACE
// Keep scalar cuts locally; print once after the complete catalog call returns.
// No formatting, output or strategy decision occurs at a sampled cutpoint.
class EntryPhasePacket {
    struct Sample {int phase,group,round;std::uint64_t fuel;};
    std::array<Sample,96> samples_;
    unsigned count_=0;
    bool enabled_;
public:
    explicit EntryPhasePacket(int round):enabled_(round==1&&handle(cambc::self())==0){mark(0);}
    void mark(int phase,int group=-1) {
        if(!enabled_||count_==samples_.size())return;
        samples_[count_++]={phase,group,static_cast<int>(cambc::round_number()),cambc::fuel_remaining()};
    }
    ~EntryPhasePacket() {
        if(!enabled_||count_<2||samples_[count_-1].fuel<12000)return;
        unsigned cross=1;while(cross<count_&&samples_[cross].round==samples_[cross-1].round)++cross;
        const auto& before=samples_[cross<count_?cross-1:0];
        const auto& after=samples_[cross<count_?cross:count_-1];
        char line[320];const int count=std::snprintf(line,sizeof line,
            "p9 entry-packet actor=0 cuts=%u firstPhase=%d group=%d beforeRound=%d afterRound=%d beforeFuel=%llu afterFuel=%llu finishRound=%d\n",
            count_,cross<count_?after.phase:-1,after.group,before.round,after.round,
            static_cast<unsigned long long>(before.fuel),static_cast<unsigned long long>(after.fuel),samples_[count_-1].round);
        if(count>0)std::cout.write(line,std::min(count,static_cast<int>(sizeof line)-1));
    }
};
#endif
#if P9_NAV_DIAGNOSTICS
// Emit only after a measured call returns, never before the first suspension
// being attributed. Formatting is bounded and its own fuel cost is excluded.
class NavBudgetTrace {
    unsigned& reports_;
    int round_,detail_;
    const char* scope_;
    std::uint64_t before_;
    const std::array<std::uint64_t,6>* checkpoints_;
public:
    NavBudgetTrace(unsigned& reports,int round,const char* scope,int detail,
                   const std::array<std::uint64_t,6>* checkpoints=nullptr):
        reports_(reports),round_(round),detail_(detail),scope_(scope),before_(cambc::fuel_remaining()),checkpoints_(checkpoints){}
    ~NavBudgetTrace() {
        const auto after=cambc::fuel_remaining();const int now=static_cast<int>(cambc::round_number());
        if(reports_>=16||after<15000||round_<P9_NAV_DIAG_FROM||round_>P9_NAV_DIAG_TO||
           (now==round_&&(before_<=after||before_-after<60000)))return;
        const int actor=handle(cambc::self());
        if(P9_NAV_DIAG_ACTOR>=0&&actor!=P9_NAV_DIAG_ACTOR)return;
        char line[224];const int count=std::snprintf(line,sizeof line,
            "p9 nav-budget actor=%d round=%d now=%d scope=%s detail=%d before=%llu after=%llu\n",
            actor,round_,now,scope_,detail_,static_cast<unsigned long long>(before_),static_cast<unsigned long long>(after));
        if(count>0){std::cout.write(line,std::min(count,static_cast<int>(sizeof line)-1));++reports_;}
        if(checkpoints_&&now!=round_) {
            const auto& f=*checkpoints_;char stages[192];const int length=std::snprintf(stages,sizeof stages,
                "p9 observe-stages actor=%d round=%d fuel=%llu,%llu,%llu,%llu,%llu,%llu\n",actor,round_,
                static_cast<unsigned long long>(f[0]),static_cast<unsigned long long>(f[1]),
                static_cast<unsigned long long>(f[2]),static_cast<unsigned long long>(f[3]),
                static_cast<unsigned long long>(f[4]),static_cast<unsigned long long>(f[5]));
            if(length>0)std::cout.write(stages,std::min(length,static_cast<int>(sizeof stages)-1));
        }
    }
};
#endif
inline cambc::bitboard around(cambc::vec2 p,int radius) {
    cambc::bitboard out;out.draw_chebyshev(p,radius);return out;
}
// Distances and walking use these same retained terrain fields. Unknown or
// farther-than-horizon routes stay unavailable, rather than becoming straight
// line estimates. One new native field is prepared per owner turn.
class Navigation {
public:
    // The SDK exports one distance per tile in documented row-major order.
    // Retain that immutable copy so route pricing is a memory read, rather
    // than dozens of repeated native queries for every visible actor.
    struct RetainedField : cambc::distance_field {
        std::vector<std::uint16_t> values;
        int columns=0,rows=0;
        void compute(const cambc::bitboard& walls,const cambc::bitboard& roots,
                     cambc::bitboard& visited,std::uint16_t horizon) {
            cambc::distance_field::compute(walls,roots,visited,horizon);
            columns=width();rows=height();values=export_linear_buffer();
        }
        std::uint16_t get(cambc::vec2 p) const {
            if(p.x<0||p.y<0||p.x>=columns||p.y>=rows||values.empty())return unreachable;
            return values[static_cast<std::size_t>(p.y)*columns+p.x];
        }
    };
    struct Member {
        cambc::camp unit;
        // Public immutable fallback metadata; current HP/state still comes
        // from a fresh personally visible camp observation.
        Point spawn;
        double health=0,strength=0;
        int range=0,group=-1,index=-1;
    };
    struct Source {
        cambc::map::camp_group group;
        cambc::bitboard spawns,goals;
        std::vector<Member> members;
        std::vector<cambc::vec2> spawn_positions;
        std::vector<cambc::vec2> goal_positions;
        cambc::vec2 approach{};
        double priority=1;
        double base_health=0,base_incoming=0;
        std::unique_ptr<RetainedField> field;
        int own_distance=-1,enemy_distance=-1,to_bank=-1;
        std::array<int,2> bank_distances{-1,-1};
        bool metadata_ready=false;
#if P9_NAV_BOUNDED_PREPARE || P9_NAV_PRICING_RESUME
        std::size_t price_goal=0,price_bank=0,price_spawn=0;
        int price_stage=0,price_own=121,price_enemy=121,price_best=121;
        std::vector<Point> price_positions;
        std::vector<int> price_boundary;
        competition::ClearedBankWork price_work;
#endif
        int inspection_until=-1;
        explicit Source(cambc::map::camp_group g):group(g),spawns(g.spawn_tiles()){}
    };
    struct Bank {
        cambc::vec2 position;
        cambc::bitboard goals;
        cambc::vec2 approach{};
        std::unique_ptr<RetainedField> field;
    };
    std::vector<Source> sources;
    std::vector<Bank> banks;
    std::vector<Member> members;
    struct Threat {Point position;int range=0,id=-1;double damage=0;bool enemy=false;};
    std::vector<Threat> threats;
    cambc::bitboard terrain,static_blocked;
    std::unique_ptr<RetainedField> own_spawn,enemy_spawn;
    int max_camp_range=0;
    int field_round=-1,walk_round=-1;
    bool initialized=false;
    bool route_failed=false;
    bool home_dynamic=false;
private:
    struct Candidate {
        cambc::map::camp_group group;
        cambc::bitboard goals;
        std::vector<Member> members;
        int distance=-1,gems=0,gold=0;
        double hp=0,incoming=0,priority=0;
        bool initially_safe=false,catalogued=false;
    };
    struct ReleaseGroup {
        int id=-1;
        std::vector<cambc::vec2> tiles;
        mutable int checked_round=-1;
        mutable bool released=false;
    };
    std::vector<Candidate> candidates_;
    std::vector<ReleaseGroup> release_groups_;
    std::vector<int> release_at_;
    int columns_=0,rows_=0;
    mutable std::vector<unsigned> release_marks_;
    mutable std::vector<int> release_distances_,release_first_steps_,release_queue_;
    mutable unsigned release_epoch_=0;
    struct ReleaseExit {cambc::vec2 position{};int distance=0;cambc::vec2 first_step{};};
    struct ReleaseRoute {
        int start=-1;
        bool complete=false;
        std::vector<ReleaseExit> exits;
        std::vector<std::pair<const RetainedField*,int>> prices;
    };
    mutable std::vector<ReleaseRoute> release_routes_;
    mutable int release_routes_round_=-1,release_routes_count_=0;
#if P9_NAV_DIAGNOSTICS
    mutable unsigned diagnostic_reports_=0;
#endif
    std::unique_ptr<RetainedField> origin_;
    std::vector<int> selected_;
    int stage_=0,cursor_=0;
#if P9_NAV_BOUNDED_PREPARE || P9_NAV_ENTRY_RESUME
    std::vector<cambc::map::camp_group> initial_groups_;
    std::size_t catalog_group_=0,catalog_member_=0;
#endif
    double initial_strength_=1,initial_hp_=1;
    int tile_index(cambc::vec2 p) const {
        return p.x<0||p.y<0||p.x>=columns_||p.y>=rows_?-1:p.y*columns_+p.x;
    }
    bool verified_release(int index,int round,cambc::vec2 witness) const {
        if(index<0||index>=static_cast<int>(release_groups_.size()))return false;
        const auto& group=release_groups_[index];
        if(group.checked_round==round)return group.released;
        group.checked_round=round;group.released=false;
        if(!same_round(round)||cambc::fuel_remaining()<18000)return false;
        // Obtain current personal vision immediately before a lifecycle query.
        // A retained same-round result needs no further hidden-state query.
        const auto visible=cambc::vision::visible_tiles();
        bool seen=visible.is_set(witness);
        if(!seen)for(auto tile:group.tiles)if(visible.is_set(tile)){seen=true;break;}
        if(!seen||!same_round(round))return false;
        std::uint32_t state{};
        const auto code=cambc::detail::sdk_camp_group_state(static_cast<std::uint32_t>(group.id),&state);
        if(!same_round(round))return false;
        if(code==static_cast<std::int32_t>(cambc::error_code::api_not_in_vision)||
           code==static_cast<std::int32_t>(cambc::error_code::api_not_found))return false;
        cambc::detail::check(code);
        if(state>3)throw std::runtime_error("unknown engine camp group state");
        group.released=state==2||state==3;return group.released;
    }
    ReleaseRoute& released_exits(int start,int round) const {
#if P9_NAV_DIAGNOSTICS
        NavBudgetTrace measured(diagnostic_reports_,round,"released-exits",start);
#endif
        if(release_routes_round_!=round) {
            release_routes_round_=round;release_routes_count_=0;
        }
        for(int i=0;i<release_routes_count_;++i)if(release_routes_[i].start==start)return release_routes_[i];
        if(release_routes_count_==static_cast<int>(release_routes_.size()))release_routes_.emplace_back();
        auto& route=release_routes_[release_routes_count_++];
        route.start=start;route.complete=false;route.exits.clear();route.prices.clear();
        const cambc::vec2 position{start%columns_,start/columns_};
        if(!verified_release(release_at_[start],round,position))return route;
        if(++release_epoch_==0){std::fill(release_marks_.begin(),release_marks_.end(),0);++release_epoch_;}
        release_queue_.clear();release_queue_.push_back(start);
        release_marks_[start]=release_epoch_;release_distances_[start]=0;release_first_steps_[start]=start;
        // Geometry and lifecycle are independent of the requested field. Walk
        // this start's released component once, retaining every boundary exit's
        // shortest distance; all source/bank prices then use memory only.
        for(std::size_t cursor=0;cursor<release_queue_.size();++cursor) {
            if((cursor&15u)==0&&(!same_round(round)||cambc::fuel_remaining()<18000))return route;
            const int tile=release_queue_[cursor],walked=release_distances_[tile];
            const cambc::vec2 at{tile%columns_,tile/columns_};
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy) {
                const cambc::vec2 next{at.x+dx,at.y+dy};const int neighbor=tile_index(next);
                if(neighbor<0||release_marks_[neighbor]==release_epoch_)continue;
                const int first=walked==0?neighbor:release_first_steps_[tile];
                if(release_at_[neighbor]<0) {
                    // BFS visits in nondecreasing distance, so the first edge
                    // to a boundary tile is its shortest possible exit cost.
                    release_marks_[neighbor]=release_epoch_;
                    route.exits.push_back({next,walked+1,{first%columns_,first/columns_}});
                } else if(verified_release(release_at_[neighbor],round,next)) {
                    release_marks_[neighbor]=release_epoch_;release_distances_[neighbor]=walked+1;
                    release_first_steps_[neighbor]=first;
                    release_queue_.push_back(neighbor);
                }
            }
        }
        route.complete=same_round(round);return route;
    }
    void append_source(Candidate& candidate) {
        sources.emplace_back(candidate.group);auto& s=sources.back();s.goals=std::move(candidate.goals);
        s.members=std::move(candidate.members);
        s.spawn_positions=s.spawns.positions();s.goal_positions=s.goals.positions();s.priority=std::max(.01,candidate.priority);
        s.base_health=candidate.hp;s.base_incoming=candidate.incoming;
        if(!s.goal_positions.empty())s.approach=s.goal_positions.front();
        candidate.catalogued=true;
    }
    Candidate* next_catalog_candidate(int round) {
        const auto remaining=[](const auto& c){return !c.catalogued&&c.distance>=0;};
        if(std::none_of(candidates_.begin(),candidates_.end(),remaining))return nullptr;
        const bool gems=std::any_of(candidates_.begin(),candidates_.end(),[&](const auto& c){return remaining(c)&&c.gems>0;});
        auto& me=cambc::self();cambc::detail::champion_info live{};
        cambc::detail::check(cambc::detail::sdk_champion(static_cast<std::uint32_t>(handle(me)),&live));
        if(!same_round(round)||!std::isfinite(live.strength)||live.strength<=0||
           !std::isfinite(live.max_health)||live.max_health<=0)return nullptr;
        const auto items=me.item_count();double attack=1,defense=1;
        static const auto monster=cambc::item_type::monster_emblem_of_strength.info();
        static const auto jungle=cambc::item_type::jungle_emblem_of_health.info();
        if(items[cambc::item_type::monster_emblem_of_strength]||items[cambc::item_type::monster_emblem_of_focus]||
           items[cambc::item_type::monster_emblem_of_health]) {
            const double bonus=monster.passive_magnitude;
            if(std::isfinite(bonus)&&bonus>=0&&bonus<=1)attack+=bonus;
        }
        if(items[cambc::item_type::jungle_emblem_of_strength]||items[cambc::item_type::jungle_emblem_of_focus]||
           items[cambc::item_type::jungle_emblem_of_health]) {
            const double reduction=jungle.passive_magnitude;
            if(std::isfinite(reduction)&&reduction>=0&&reduction<1)defense-=reduction;
        }
        Candidate* best=nullptr;double value=-1;
        for(auto& c:candidates_)if(remaining(c)&&(!gems||c.gems>0)) {
            const double work=std::ceil(c.hp/(live.strength*attack))+2;
            const double loss=c.incoming*defense*work;
            // This is public economic potential, not permission to fight an
            // unseen group. A mild affordability discount retains future rich
            // sources; current observation and combat gates still decide use.
            const double reward=gems?c.gems:.4*c.gold;
            const double score=reward/(c.distance+work+cambc::constants::capture_time+8.)/(1.+loss/live.max_health);
            if(!best||score>value||(score==value&&handle(c.group)<handle(best->group))) {best=&c;value=score;}
        }
        if(best)best->priority=value;
        return same_round(round)?best:nullptr;
    }
public:

    static int steps(const RetainedField* field,cambc::vec2 p) {
        if(!field)return -1;
        const auto d=field->get(p);return d==cambc::distance_field::unreachable?-1:static_cast<int>(d);
    }
    static int minimum(const RetainedField* field,const cambc::bitboard& tiles) {
        int best=121;
        if(field)for(auto p:tiles.positions()) {const int d=steps(field,p);if(d>=0)best=std::min(best,d);}
        return best==121?-1:best;
    }
    int live_steps(const RetainedField* field,cambc::vec2 position) const {
        const int direct=steps(field,position);if(direct>=0||!field)return direct;
        const int start=tile_index(position);if(start<0||release_at_[start]<0)return -1;
        const int round=static_cast<int>(cambc::round_number());
        auto& route=released_exits(start,round);if(!route.complete)return -1;
        for(const auto& price:route.prices)if(price.first==field)return price.second;
        int best=std::numeric_limits<int>::max();
        for(const auto& exit:route.exits) {
            const int outside=steps(field,exit.position);
            if(outside>=0)best=std::min(best,exit.distance+outside);
        }
        const int result=best==std::numeric_limits<int>::max()?-1:best;
        route.prices.push_back({field,result});
        return result;
    }
    cambc::bitboard blocked() const {
        auto b=terrain|cambc::vision::champion_tiles()|cambc::vision::camp_tiles()|cambc::vision::teleport_tiles();
        b.set(cambc::self().position(),false);return b;
    }
    double exposure(cambc::vec2 position,int allowed_group=-1,int allowed_enemy=-1) const {
        double damage=0;
        for(const auto& threat:threats) {
            if((threat.enemy?allowed_enemy:allowed_group)==threat.id)continue;
            if(distance(point(position),threat.position)<=threat.range)damage+=threat.damage;
        }
        return damage;
    }
    cambc::bitboard route_blocked(int allowed_group=-1,int allowed_enemy=-1) const {
        auto walls=blocked();
        for(const auto& threat:threats) {
            if((threat.enemy?allowed_enemy:allowed_group)==threat.id)continue;
            walls|=around(point(threat.position),threat.range);
        }
        walls.set(cambc::self().position(),false);return walls;
    }
    void initialize(int round) {
        // Cheap stages share the owner's fuel allowance; native distance-field
        // construction remains limited to one per owner turn.
        for(int jobs=0;jobs<24&&!initialized&&same_round(round)&&cambc::fuel_remaining()>=75000;++jobs) {
            if(stage_==0) {
#if P9_NAV_ENTRY_TRACE
                EntryPhasePacket packet(round);
#endif
#if P9_NAV_DIAGNOSTICS
                NavBudgetTrace measured(diagnostic_reports_,round,"initial-catalog",0);
#endif
                terrain=cambc::map::walls()|cambc::map::holes()|cambc::map::altars()|cambc::map::spawn_region(cambc::team::enemy);
#if P9_NAV_ENTRY_TRACE
                packet.mark(1);
#endif
                static_blocked=terrain;columns_=cambc::map::width();rows_=cambc::map::height();
                const auto area=static_cast<std::size_t>(columns_)*rows_;
                release_at_.assign(area,-1);release_marks_.assign(area,0);release_distances_.resize(area);release_first_steps_.resize(area);
                sources.reserve(group_capacity);
#if P9_NAV_ENTRY_TRACE
                packet.mark(2);
#endif
#if P9_NAV_BOUNDED_PREPARE || P9_NAV_ENTRY_RESUME
                initial_groups_=cambc::map::camp_groups();stage_=-1;continue;
            }
            if(stage_==-1) {
                if(catalog_group_<initial_groups_.size()) {
                    const auto& g=initial_groups_[catalog_group_];
                    if(catalog_member_==0) {
                        candidates_.push_back({g,{}, {}});ReleaseGroup component;component.id=handle(g);
                        release_groups_.push_back(std::move(component));
                    }
                    // Setting every original spawn directly is the same union
                    // as the old complete group masks, with a bounded chunk.
                    for(int work=0;work<8&&catalog_member_<g.camps().size();++work) {
                        const auto tile=g.camps()[catalog_member_++].spawn_position();
                        static_blocked.set(tile);release_groups_.back().tiles.push_back(tile);
                        release_at_[static_cast<std::size_t>(tile.y)*columns_+tile.x]=static_cast<int>(release_groups_.size())-1;
                    }
                    if(catalog_member_==g.camps().size()){++catalog_group_;catalog_member_=0;}
                    continue;
                }
                initial_groups_.clear();
#else
                const auto groups=cambc::map::camp_groups();
#if P9_NAV_ENTRY_TRACE
                packet.mark(3);
#endif
                for(const auto& g:groups) {
#if P9_NAV_ENTRY_TRACE
                    {auto tiles=g.spawn_tiles();packet.mark(4,handle(g));static_blocked|=tiles;packet.mark(5,handle(g));}
#else
                    static_blocked|=g.spawn_tiles();
#endif
                    candidates_.push_back({g,{}, {}});
#if P9_NAV_ENTRY_TRACE
                    packet.mark(6,handle(g));
#endif
                    ReleaseGroup component;component.id=handle(g);
                    for(const auto& c:g) {
                        const auto tile=c.spawn_position();component.tiles.push_back(tile);
                        release_at_[static_cast<std::size_t>(tile.y)*columns_+tile.x]=static_cast<int>(release_groups_.size());
                    }
                    release_groups_.push_back(std::move(component));
#if P9_NAV_ENTRY_TRACE
                    packet.mark(7,handle(g));
#endif
                }
#endif
                initial_strength_=std::max(1.f,cambc::self().strength());initial_hp_=std::max(1.f,cambc::self().max_health());
#if P9_NAV_ENTRY_TRACE
                packet.mark(8);
#endif
                stage_=1;continue;
            }
            if(stage_==1) {
#if P9_NAV_DIAGNOSTICS
                NavBudgetTrace measured(diagnostic_reports_,round,"initial-origin-field",0);
#endif
                if(field_round==round)return;
                field_round=round;origin_=std::make_unique<RetainedField>();cambc::bitboard visited;
                origin_->compute(static_blocked,around(cambc::self().position(),0),visited,120);
                stage_=2;continue;
            }
            if(stage_==2&&cursor_<static_cast<int>(candidates_.size())) {
#if P9_NAV_DIAGNOSTICS
                NavBudgetTrace measured(diagnostic_reports_,round,"initial-group-metadata",cursor_);
#endif
                auto& candidate=candidates_[cursor_++];
                const int group=handle(candidate.group);
                for(const auto& c:candidate.group) {
                    const auto spawn=c.spawn_position();const auto stats=c.base_stats();
                    candidate.goals|=around(spawn,1);
                    candidate.members.push_back({c,point(spawn),stats.base_health,stats.base_strength,stats.base_attack_range,group,static_cast<int>(members.size())});
                    members.push_back(candidate.members.back());
                    max_camp_range=std::max(max_camp_range,stats.base_attack_range);
                    candidate.hp+=stats.base_health;candidate.incoming+=stats.base_strength;
                }
                candidate.goals&=~static_blocked;candidate.distance=minimum(origin_.get(),candidate.goals);
                candidate.gems=candidate.group.gem_value();candidate.gold=candidate.group.gold_value();
                const double work=std::ceil(candidate.hp/initial_strength_)+2,loss=candidate.incoming*work;
                const double safety=loss<=.8*initial_hp_?1.:.08/(1.+loss/initial_hp_);
                candidate.initially_safe=loss<=.8*initial_hp_;
                candidate.priority=double(candidate.gems)*safety/(std::max(0,candidate.distance)+work+8);
                continue;
            }
            if(stage_==2) {
                std::vector<std::pair<int,int>> nearest;
                for(int i=0;i<static_cast<int>(candidates_.size());++i)
                    if(candidates_[i].distance>=0)nearest.emplace_back(candidates_[i].distance,i);
                std::sort(nearest.begin(),nearest.end(),[&](auto a,auto b){
                    return a.first!=b.first?a.first<b.first:handle(candidates_[a.second].group)<handle(candidates_[b.second].group);});
                auto scoring=nearest;
                std::sort(scoring.begin(),scoring.end(),[&](auto a,auto b){
                    const auto av=candidates_[a.second].priority,bv=candidates_[b.second].priority;
                    return av!=bv?av>bv:(a.first!=b.first?a.first<b.first:a.second<b.second);});
                const auto ready_count=std::count_if(candidates_.begin(),candidates_.end(),[](const auto& c){return c.distance>=0&&c.gems>0&&c.initially_safe;});
                const std::size_t gem_slots=ready_count>=4?6:4;
                for(auto [d,index]:scoring) { (void)d;
                    if(candidates_[index].gems>0&&selected_.size()<gem_slots)selected_.push_back(index);
                }
                for(auto [d,index]:nearest) { (void)d;
                    if(selected_.size()==8)break;
                    if(candidates_[index].gems==0&&candidates_[index].gold>0)selected_.push_back(index);
                }
                for(auto [d,index]:nearest) { (void)d;
                    if(selected_.size()==8)break;
                    if(std::find(selected_.begin(),selected_.end(),index)==selected_.end())selected_.push_back(index);
                }
                cursor_=0;stage_=3;continue;
            }
            if(stage_==3&&cursor_<static_cast<int>(selected_.size())) {
                auto& candidate=candidates_[selected_[cursor_++]];
                append_source(candidate);
                continue;
            }
            // Opening banks are the nearest two certified approaches. Pricing
            // each altar and every pair is deferred out of the opening path.
            const auto altars=cambc::map::altars().positions();
            std::vector<std::pair<int,cambc::vec2>> ranked;
            const int radius=static_cast<int>(cambc::constants::submit_gems_range);
            for(auto altar:altars) {
                int best=std::numeric_limits<int>::max();
                for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx) {
                    const int d=steps(origin_.get(),{altar.x+dx,altar.y+dy});if(d>=0)best=std::min(best,d);
                }
                if(best==std::numeric_limits<int>::max())best=10000+distance(point(altar),point(cambc::self().position()));
                ranked.push_back({best,altar});
            }
            std::sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b){
                return a.first!=b.first?a.first<b.first:(a.second.y!=b.second.y?a.second.y<b.second.y:a.second.x<b.second.x);});
            for(const auto& entry:ranked) {
                if(banks.size()==2)break;const auto p=entry.second;
                auto goals=around(p,radius)&~static_blocked;const auto approaches=goals.positions();
                banks.push_back({p,std::move(goals),approaches.empty()?p:approaches.front(),nullptr});
            }
            selected_.clear();origin_.reset();initialized=true;
        }
    }
    void prepare(int round,bool allow_late=true) {
        initialize(round);
        if(!initialized||!same_round(round)||field_round==round||cambc::fuel_remaining()<75000)return;
        auto build=[&](auto& entry) {
#if P9_NAV_DIAGNOSTICS
            NavBudgetTrace measured(diagnostic_reports_,round,"retained-field",static_cast<int>(sources.size()));
#endif
            entry.field=std::make_unique<RetainedField>();cambc::bitboard visited;
            field_round=round;entry.field->compute(static_blocked,entry.goals,visited,120);
        };
        for(auto& b:banks)if(!b.field){build(b);return;}
        if(!own_spawn||!enemy_spawn) {
            const bool own=!own_spawn;auto& field=own?own_spawn:enemy_spawn;
            auto walls=static_blocked;
            if(!own) {walls&=~cambc::map::spawn_region(cambc::team::enemy);walls|=cambc::map::spawn_region(cambc::team::ally);}
            field=std::make_unique<RetainedField>();cambc::bitboard visited;
            field_round=round;
            field->compute(walls,cambc::map::spawn_region(own?cambc::team::ally:cambc::team::enemy),visited,120);
            return;
        }
        // Make each prepared source usable immediately; the opening does not
        // wait for all seven other source fields before pricing its first trip.
        for(std::size_t index=0;;++index) {
            if(index==sources.size()) {
                // Existing policy anchors keep their IDs and ordering. Only
                // after every opening field is usable may a new slot appear.
                if(!allow_late||sources.size()>=group_capacity||!same_round(round)||cambc::fuel_remaining()<75000)return;
                auto* candidate=next_catalog_candidate(round);
                if(!candidate||!same_round(round)||cambc::fuel_remaining()<75000)return;
                append_source(*candidate);
            }
            auto& s=sources[index];
            if(s.metadata_ready)continue;
#if P9_NAV_DIAGNOSTICS
            NavBudgetTrace measured(diagnostic_reports_,round,"source-price",handle(s.group));
#endif
            const bool built=!s.field;
            if(built) {
                if(field_round==round||!same_round(round)||cambc::fuel_remaining()<75000)return;
                build(s);
            }
            if(!same_round(round)||cambc::fuel_remaining()<65000)return;
#if P9_NAV_BOUNDED_PREPARE || P9_NAV_PRICING_RESUME
            // Resume only immutable distance work. Publish the original final
            // metadata atomically after every goal, bank and spawn is priced.
            while(same_round(round)&&cambc::fuel_remaining()>=65000&&!s.metadata_ready) {
                if(s.price_stage==0) {
                    for(int work=0;work<8&&s.price_goal<s.goal_positions.size();++work) {
                        const auto tile=s.goal_positions[s.price_goal++];
                        const int a=steps(own_spawn.get(),tile),b=steps(enemy_spawn.get(),tile);
                        if(a>=0)s.price_own=std::min(s.price_own,a);if(b>=0)s.price_enemy=std::min(s.price_enemy,b);
                    }
                    if(s.price_goal<s.goal_positions.size())continue;
                    s.own_distance=s.price_own==121?-1:s.price_own;s.enemy_distance=s.price_enemy==121?-1:s.price_enemy;
                    if(s.own_distance>=0)++s.own_distance;if(s.enemy_distance>=0)++s.enemy_distance;
                    for(auto spawn:s.spawn_positions)s.price_positions.push_back(point(spawn));
                    s.price_stage=1;continue;
                }
                if(s.price_bank==banks.size()) {
                    s.to_bank=s.price_best==121?-1:s.price_best;s.metadata_ready=true;break;
                }
                if(s.price_stage==1) {
                    if(s.price_spawn<s.spawn_positions.size()) {
                        const auto spawn=s.spawn_positions[s.price_spawn++];int exit=-1;
                        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy) {
                            const int d=steps(banks[s.price_bank].field.get(),{spawn.x+dx,spawn.y+dy});
                            if(d>=0&&(exit<0||d+1<exit))exit=d+1;
                        }
                        s.price_boundary.push_back(exit);continue;
                    }
                    s.price_work.begin(s.price_positions,s.price_boundary);s.price_stage=2;continue;
                }
                if(s.price_work.step()) {
                    const int worst=s.price_work.result();s.bank_distances[s.price_bank]=worst;
                    if(worst>=0)s.price_best=std::min(s.price_best,worst);
                    ++s.price_bank;s.price_spawn=0;s.price_boundary.clear();s.price_stage=1;
                }
            }
            if(!s.metadata_ready)return;
#else
            int own=121,enemy=121;
            for(auto tile:s.goal_positions) {
                const int a=steps(own_spawn.get(),tile),b=steps(enemy_spawn.get(),tile);
                if(a>=0)own=std::min(own,a);if(b>=0)enemy=std::min(enemy,b);
            }
            s.own_distance=own==121?-1:own;s.enemy_distance=enemy==121?-1:enemy;
            if(s.own_distance>=0)++s.own_distance;if(s.enemy_distance>=0)++s.enemy_distance;
            int best=121;
            for(std::size_t bank=0;bank<banks.size();++bank) {
                auto& b=banks[bank];
                std::vector<Point> positions;std::vector<int> boundary;
                for(auto spawn:s.spawn_positions) {
                    positions.push_back(point(spawn));int exit=-1;
                    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(dx||dy) {
                        const int d=steps(b.field.get(),{spawn.x+dx,spawn.y+dy});
                        if(d>=0&&(exit<0||d+1<exit))exit=d+1;
                    }
                    boundary.push_back(exit);
                }
                const int worst=competition::cleared_group_bank_distance(positions,boundary);
                s.bank_distances[bank]=worst;
                if(worst>=0)best=std::min(best,worst);
            }
            s.to_bank=best==121?-1:best;s.metadata_ready=true;
#endif
            if(built)return;
        }
    }
    Source* source(int id) {for(auto& s:sources)if(handle(s.group)==id)return &s;return nullptr;}
    template<class Field> bool walk_field(const Field& field,int round,bool strategic,
                                        int allowed_group=-1,int allowed_enemy=-1,int reserve=25000) {
        auto& me=cambc::self();
        if(!same_round(round)||channel_active()||!me.can_move()||cambc::fuel_remaining()<static_cast<unsigned>(reserve))return false;
        for(int attempt=0;attempt<3&&same_round(round);++attempt) {
            if(cambc::fuel_remaining()<static_cast<unsigned>(reserve))return false;
            auto best=me.position();auto value=field.get(best);
            const double current=exposure(best,allowed_group,allowed_enemy);
            const auto occupied=blocked();
            for(auto p:occupied.reachable_tiles(best,1).positions())
                if(competition::safe_progress(current,exposure(p,allowed_group,allowed_enemy))&&field.get(p)<value){best=p;value=field.get(p);}
            if(best==me.position()){if(strategic&&value!=0)route_failed=true;return false;}
            if(!same_round(round))return false;
            if(me.move_absolute(best))return true;
        }
        if(strategic)route_failed=true;return false;
    }
    bool walk_released(const RetainedField* field,int round,int allowed_group=-1,int allowed_enemy=-1) {
        auto& me=cambc::self();
        for(int attempt=0;attempt<3;++attempt) {
            if(!field||!same_round(round)||cambc::fuel_remaining()<18000)return false;
            const auto state=me.state();
            if(!state.contains(cambc::champion_state::can_move)||
               state.contains(cambc::champion_state::recalling)||
               state.contains(cambc::champion_state::submitting_gems)||
               state.contains(cambc::champion_state::teleporting))return false;
            const auto position=me.position();const int start=tile_index(position);
            if(start<0||release_at_[start]<0||steps(field,position)>=0)return false;
            const auto& route=released_exits(start,round);if(!route.complete)return false;
            // The retained exit proves a route through personally verified
            // released spawns. Check only its next step against fresh units
            // and exposure; exiting needs no new native distance field.
            const auto legal=blocked().reachable_tiles(position,1).positions();
            const double current=exposure(position,allowed_group,allowed_enemy);
            auto best=position;int cost=std::numeric_limits<int>::max();
            for(const auto& exit:route.exits) {
                const int outside=steps(field,exit.position);if(outside<0)continue;
                const int total=exit.distance+outside;
                if(total>=cost||std::find(legal.begin(),legal.end(),exit.first_step)==legal.end()||
                   !competition::safe_progress(current,exposure(exit.first_step,allowed_group,allowed_enemy)))continue;
                best=exit.first_step;cost=total;
            }
            if(best==position||!same_round(round))return false;
            if(me.move_absolute(best))return true;
            // A newly revealed hidden occupant preserves movement. The next
            // attempt refreshes the action flags and current obstacle mask.
        }
        return false;
    }
    bool walk_source(int id,int round,bool strategic=false,bool combat=false) {
        auto* s=source(id);if(!s||!s->field)return false;
        const int allowed=combat?id:-1;
        if(steps(s->field.get(),cambc::self().position())<0) {
            if(walk_released(s->field.get(),round,allowed))return true;
            return walk_dynamic(s->goals,round,strategic,allowed);
        }
        if(walk_field(*s->field,round,strategic,allowed))return true;
        return walk_dynamic(s->goals,round,strategic,allowed);
    }
    bool walk_bank(int id,int round,bool strategic=false) {
        if(id<0||id>=static_cast<int>(banks.size())||!banks[id].field)return false;
        if(steps(banks[id].field.get(),cambc::self().position())<0) {
            if(walk_released(banks[id].field.get(),round))return true;
            return walk_dynamic(banks[id].goals,round,strategic);
        }
        if(walk_field(*banks[id].field,round,strategic))return true;
        return walk_dynamic(banks[id].goals,round,strategic);
    }
    bool walk_home(int round) {
        if(steps(own_spawn.get(),cambc::self().position())<0&&walk_released(own_spawn.get(),round)) {
            // A verified exit reconnects to this retained home field. Commit
            // to that model after moving, rather than requiring another full
            // dynamic field on the following tile of the same recovery.
            home_dynamic=false;return true;
        }
        // A cleared original camp can offer a shorter live route than the
        // immutable field. Once that detour is needed, retain one routing
        // model for this recovery instead of reversing on the next tile.
        if(!home_dynamic&&steps(own_spawn.get(),cambc::self().position())>=0&&
           walk_field(*own_spawn,round,false))return true;
        home_dynamic=true;
        return walk_dynamic(cambc::map::spawn_region(cambc::team::ally),round);
    }
    bool step_away(int round) {
        auto& me=cambc::self();
        if(!same_round(round)||channel_active()||!me.can_move()||cambc::fuel_remaining()<22000)return false;
        const auto clearance=[&](cambc::vec2 p) {
            double value=0;
            for(const auto& threat:threats)
                value+=std::min(distance(point(p),threat.position),threat.range+1)*threat.damage;
            return value;
        };
        auto best=me.position();double harm=exposure(best);
        double clear=clearance(best);
        auto home_distance=[&](cambc::vec2 p){const int d=home_dynamic?-1:steps(own_spawn.get(),p);return d<0?std::numeric_limits<int>::max():d;};
        int home=home_distance(best);
        for(auto p:blocked().reachable_tiles(best,1).positions()) {
            const double next=exposure(p);const int toward=home_distance(p);
            if(!competition::safe_progress(harm,next)||harm<0||next<0)continue;
            const double next_clear=clearance(p);
            // Equal exposure can hide progress toward a threat's range edge.
            // Prefer increasing this potential before home-distance ties;
            // with unchanged threats and home routing, reversing an equal-
            // damage step cannot improve the same ordered preferences.
            if(next<harm||next_clear>clear||(next_clear==clear&&competition::retreat_progress(harm,next,home,toward))) {
                best=p;harm=next;clear=next_clear;home=toward;
            }
        }
        return best!=me.position()&&same_round(round)&&me.move_absolute(best);
    }
    bool walk_local(const cambc::bitboard& goals,int round) {
        // Capture frequently needs exactly one adjacent step. A fresh legal
        // step needs no distance-field construction or 40k routing admission.
        auto& me=cambc::self();
        for(int attempt=0;attempt<3;++attempt) {
            if(!same_round(round)||channel_active()||cambc::fuel_remaining()<22000||!me.can_move())return false;
            const auto origin=me.position();const double current=exposure(origin);
            const auto legal=blocked().reachable_tiles(origin,1)&goals;
            bool retry=false;
            for(auto p:legal.positions()) {
                if(p==origin||!competition::safe_progress(current,exposure(p)))continue;
                if(!same_round(round))return false;
                if(me.move_absolute(p))return true;
                retry=true;
                break; // Refresh occupancy after a hidden collision.
            }
            if(!retry)return false;
        }
        return false;
    }
    bool walk_dynamic(cambc::bitboard goals,int round,bool strategic=false,int allowed_group=-1,int allowed_enemy=-1) {
        auto& me=cambc::self();
        if(walk_round==round||!same_round(round)||channel_active()||!me.can_move()||cambc::fuel_remaining()<40000)return false;
        auto walls=exposure(me.position(),allowed_group,allowed_enemy)>0?blocked():route_blocked(allowed_group,allowed_enemy);
        goals&=~walls;if(goals.is_empty()||goals.is_set(me.position()))return false;
        cambc::distance_field field;cambc::bitboard visited;
        // 120 is a path-distance limit, not an expansion-count/fuel promise.
        field.compute_to(walls,goals,around(me.position(),0),visited,120);walk_round=round;
        if(!same_round(round))return false;
        return walk_field(field,round,strategic,allowed_group,allowed_enemy,20000);
    }
};
} // namespace p9::live
