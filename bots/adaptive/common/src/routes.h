#pragma once
#include <cambc.h>
#include "route_payload.h"
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

namespace p8::native {
inline cambc::vec2 tile(p6::Point p) { return {p.x,p.y}; }
inline p6::Point point(cambc::vec2 p) { return {p.x,p.y}; }
inline int distance(p6::Point a,p6::Point b) { return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)); }
inline int group_id(const cambc::map::camp_group& g) {
    return static_cast<int>(static_cast<std::uint32_t>(static_cast<cambc::handle_camp_group_t>(g)));
}
inline cambc::bitboard singleton(p6::Point p) { cambc::bitboard b;b.set(tile(p));return b; }
struct Field {
    cambc::distance_field paths;
    p6::Point destination{};
    bool ready=false;
    // Fields are immutable after preparation; copies share their verified
    // row-major distances rather than repeating per-tile SDK host calls.
    mutable std::shared_ptr<const std::vector<std::uint16_t>> linear;
};
struct Site {
    cambc::map::camp_group group;
    Field approach{},capture{};
    std::array<Field,3> rally{},joint_capture{};
    int rally_count=0,joint_capture_count=0;
    explicit Site(cambc::map::camp_group value):group(value) {}
};
struct Bank { p6::Point altar{}; Field approach{}; };

// Immutable map paths are prepared one field per owner turn. Live occupancy is
// checked separately immediately before each exact retained step is dispatched.
class Routes {
    bool initialized_=false;
    int prepared_round_=-1;
    enum class InitStage { setup,groups,altars,done };
    InitStage init_stage_=InitStage::setup;
    int init_attack_range_=0;
    std::size_t init_group_=0,init_altar_=0;
    bool init_altars_loaded_=false;
    std::vector<cambc::map::camp_group> init_groups_;
    std::vector<cambc::vec2> init_altars_;
    cambc::bitboard terrain_;
    // At most fifty immutable fixed legs. Ingress always uses the current
    // position; these slots never hold a live observation or a forecast.
    mutable std::array<std::unique_ptr<const Route>,8> capture_routes_{},spawn_returns_{};
    mutable std::array<std::array<std::unique_ptr<const Route>,2>,8> bank_routes_{},renewal_routes_{};
    mutable std::array<std::unique_ptr<const Route>,2> spawn_routes_{};
    const Route* retain(std::unique_ptr<const Route>& stored,p6::Point from,const Field& field) const {
        if(!field.ready)return nullptr;
        if(stored) {
            if(!(stored->tiles[0]==from)||!(stored->tiles[stored->count-1]==field.destination))return nullptr;
            return stored.get();
        }
        auto value=std::unique_ptr<const Route>(new const Route(route(from,field)));
        if(!route_valid(*value))return nullptr;
        stored=std::move(value);return stored.get();
    }
    void initialize_group(const cambc::map::camp_group& g) {
        const auto members=g.camps();
        if(g.gem_value()<=0||members.empty())return;
        // Work's audited clear stage is stationary. Preserve the original
        // intersection and iteration ties; this is one inspected candidate.
        cambc::bitboard approaches;approaches.draw_chebyshev(members.front().spawn_position(),init_attack_range_);
        for(const auto& m:members) {
            cambc::bitboard reach;reach.draw_chebyshev(m.spawn_position(),init_attack_range_);approaches&=reach;
        }
        approaches&=~(terrain_|g.spawn_tiles());
        const auto legal=approaches.positions();if(legal.empty())return;
        auto best=legal.front();
        for(const auto p:legal)
            if(distance(spawn.destination,point(p))<distance(spawn.destination,point(best)))best=p;
        auto capture=members.front().spawn_position();
        for(const auto& m:members)
            if(distance(point(best),point(m.spawn_position()))<distance(point(best),point(capture)))
                capture=m.spawn_position();
        auto pads=legal;
        std::sort(pads.begin(),pads.end(),[&](const auto a,const auto b) {
            const int x=distance(point(best),point(a)),y=distance(point(best),point(b));
            if(x!=y)return x<y;
            return a.y!=b.y?a.y<b.y:a.x<b.x;
        });
        auto captures=g.spawn_tiles().positions();
        std::sort(captures.begin(),captures.end(),[&](const auto a,const auto b) {
            const int x=distance(point(capture),point(a)),y=distance(point(capture),point(b));
            if(x!=y)return x<y;
            return a.y!=b.y?a.y<b.y:a.x<b.x;
        });
        // Publish only after all candidate geometry queries/sorts completed.
        sites.emplace_back(g);auto& s=sites.back();
        s.approach.destination=point(best);s.capture.destination=point(capture);
        for(const auto pad:pads)if(!(point(pad)==point(best))&&s.rally_count<3)
            s.rally[s.rally_count++].destination=point(pad);
        for(const auto p:captures)if(s.joint_capture_count<3)
            s.joint_capture[s.joint_capture_count++].destination=point(p);
    }
    void initialize_altar(cambc::vec2 altar) {
        cambc::bitboard near;near.draw_chebyshev(altar,cambc::constants::submit_gems_range);
        near&=~terrain_;const auto possible=near.positions();if(possible.empty())return;
        auto best=possible.front();
        for(const auto p:possible)
            if(distance(spawn.destination,point(p))<distance(spawn.destination,point(best)))best=p;
        Bank bank;bank.altar=point(altar);bank.approach.destination=point(best);banks.push_back(std::move(bank));
    }
public:
    struct CyclePaths {
        const Route *capture=nullptr,*bank=nullptr,*renewal=nullptr,*home=nullptr,*back=nullptr;
        bool complete(bool spawn_visit) const {return capture&&bank&&(spawn_visit?home&&back:renewal!=nullptr);}
    };
    std::vector<Site> sites;
    std::vector<Bank> banks;
    Field spawn{};
    Routes()=default;
    // Custody remaps complete rally fields in an independent geometry view.
    // Copy geometry/progress, leaving all retained-route ownership slots empty.
    // Field::linear shares immutable bytes; SDK fields/terrain keep their own
    // normal value-copy semantics. No live state or action permission is cached.
    Routes(const Routes& other):
        initialized_(other.initialized_),prepared_round_(other.prepared_round_),
        init_stage_(other.init_stage_),init_attack_range_(other.init_attack_range_),
        init_group_(other.init_group_),init_altar_(other.init_altar_),
        init_altars_loaded_(other.init_altars_loaded_),init_groups_(other.init_groups_),
        init_altars_(other.init_altars_),terrain_(other.terrain_),
        sites(other.sites),banks(other.banks),spawn(other.spawn) {}
    Routes(Routes&&)=default;
    Routes& operator=(Routes&&)=default;
    const cambc::bitboard& terrain() const { return terrain_; }
    bool initialized() const { return initialized_; }
    // One immutable setup, then one candidate group (even a rejection), then
    // one of the original first two altar candidates per call. No live actor
    // state or forecast is retained; callers yield after a gameplay step.
    bool initialize_step(int attack_range) {
        if(init_stage_!=InitStage::setup&&attack_range!=init_attack_range_)
            throw std::invalid_argument("Routes initialization attack range changed");
        if(initialized_)return true;
        if(init_stage_==InitStage::setup) {
            init_attack_range_=attack_range;
            terrain_=cambc::map::walls()|cambc::map::holes()|cambc::map::altars()|
                cambc::map::spawn_region(cambc::team::enemy);
            const auto own=cambc::map::spawn_region(cambc::team::ally).positions();
            if(own.empty()){init_stage_=InitStage::done;initialized_=true;return true;}
            spawn.destination=point(own.front());
            init_groups_=cambc::map::camp_groups();
            std::sort(init_groups_.begin(),init_groups_.end(),[&](const auto& a,const auto& b) {
                const auto rank=[&](const auto& g) {
                    return distance(spawn.destination,point(g.camps().front().spawn_position()));
                };
                const int x=rank(a),y=rank(b);return x!=y?x<y:group_id(a)<group_id(b);
            });
            sites.reserve(8);banks.reserve(2);init_stage_=InitStage::groups;return false;
        }
        if(init_stage_==InitStage::groups) {
            if(init_group_<init_groups_.size()&&sites.size()<8) {
                initialize_group(init_groups_[init_group_]);++init_group_;
            }
            if(init_group_==init_groups_.size()||sites.size()==8)init_stage_=InitStage::altars;
            return false;
        }
        if(!init_altars_loaded_) {
            init_altars_=cambc::map::altars().positions();init_altars_loaded_=true;
        }
        if(init_altar_<init_altars_.size()&&init_altar_<2) {
            initialize_altar(init_altars_[init_altar_]);++init_altar_;
        }
        if(init_altar_==init_altars_.size()||init_altar_==2) {
            init_stage_=InitStage::done;initialized_=true;
        }
        return initialized_;
    }
    void initialize(int attack_range) {
        while(!initialize_step(attack_range)) {}
    }
    void prepare() {
        if(!initialized_)return;
        const int round=cambc::round_number();
        if (prepared_round_==round || cambc::fuel_remaining()<55000) return;
        prepared_round_=round;
        auto one=[&](Field& f) {
            if (f.ready) return false;
            cambc::bitboard visited;
            // Reused from arbitrary origins: an empty compute_to stop set
            // would be satisfied immediately and leave only the root reached.
            f.paths.compute(terrain_,singleton(f.destination),visited,256);
            f.ready=true;return true;
        };
        if (one(spawn)) return;
        for (auto& b:banks) if (one(b.approach)) return;
        for (auto& s:sites) if (one(s.approach) || one(s.capture)) return;
        for (auto& s:sites) {
            for(int i=0;i<s.rally_count;++i)if(one(s.rally[i]))return;
            for(int i=0;i<s.joint_capture_count;++i)if(one(s.joint_capture[i]))return;
        }
    }
    void prepare_draft() {
        if(!initialized_)return;
        // Only immutable public geometry; the larger first-draft allowance is
        // still bounded and must leave fuel to select and issue the actual pick.
        auto one=[&](Field& f) {
            if (f.ready || cambc::fuel_remaining()<150000) return;
            cambc::bitboard visited;
            f.paths.compute(terrain_,singleton(f.destination),visited,256);f.ready=true;
        };
        one(spawn);for(auto& b:banks)one(b.approach);
        for(auto& s:sites){one(s.approach);one(s.capture);}
    }
    int static_distance(p6::Point from,const Field& field) const {
        if(!initialized_||!field.ready||from.x<0||from.y<0||from.x>=cambc::map::width()||from.y>=cambc::map::height())return 65535;
        return field.paths.get(tile(from));
    }
    CyclePaths cycle_paths(int site,int bank,bool spawn_visit) const {
        if(!initialized_||site<0||site>=int(sites.size())||site>=8||bank<0||bank>=int(banks.size())||bank>=2)return {};
        const auto& s=sites[site];const auto& b=banks[bank];CyclePaths out;
        out.capture=retain(capture_routes_[site],s.approach.destination,s.capture);
        out.bank=retain(bank_routes_[site][bank],s.capture.destination,b.approach);
        if(spawn_visit) {
            out.home=retain(spawn_routes_[bank],b.approach.destination,spawn);
            out.back=retain(spawn_returns_[site],spawn.destination,s.approach);
        } else out.renewal=retain(renewal_routes_[site][bank],b.approach.destination,s.approach);
        return out;
    }
    bool route_into(p6::Point from,const Field& field,Route& result) const {
        reset_route_header(result);if (!initialized_||!field.ready) return false;
        const int width=cambc::map::width(),height=cambc::map::height();
        if(!field.linear)field.linear=std::make_shared<const std::vector<std::uint16_t>>(field.paths.export_linear_buffer());
        return route_from_distances_into(from,field.destination,width,height,*field.linear,result);
    }
    Route route(p6::Point from,const Field& field) const {
        Route result;route_into(from,field,result);return result;
    }
    static bool join_into(const Route& a,const Route& b,Route& result) {
        return ::p8::native::join_into(a,b,result);
    }
    static Route join(const Route& a,const Route& b) {
        Route result;join_into(a,b,result);return result;
    }
};
} // namespace p8::native
