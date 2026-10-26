#pragma once
#include <cambc.h>
#include "../common/allocation.h"
#include "public-model.h"
#include "ownership.h"
#include <set>

namespace plan3_assignment {
using plan3_matchmaker::PublicActor;
using plan3_matchmaker::PublicGroup;
using plan3_matchmaker::PublicTiming;
using plan3_matchmaker::Excursion;
using plan3_matchmaker::CellState;
using plan3_matchmaker::estimate_excursion;

enum class DraftStatus { pending, published, unavailable };

inline DraftPlan read_plan(Policy policy,unsigned actualTeamSize) {
    const cambc::draft_memory_buffer current; // SDK global draft_memory is a startup snapshot.
    auto plan=decode_plan(std::span<const std::byte>{current.data(),current.size()},
        unsigned(cambc::map::width()),unsigned(cambc::map::height()),policy);
    if (plan.complete && plan.actors != plan3_runtime::market_fixed_roster(actualTeamSize)) return {};
    return plan;
}

// All state is private to the draft process; no live observations or messages.
// SDK layout loading, positions enumeration, allocation and field computation
// are map-bounded indivisible operations. Their native cost remains unproved.
class DraftAllocator {
    struct Group {
        PublicGroup model;
        std::size_t source=0;
        int distance=-2;
    };
    Policy policy_;
    bool initialized_=false, unavailable_=false, published_=false;
    bool metadata_complete_=false, field_complete_=false, routes_complete_=false;
    bool matrix_started_=false, solver_started_=false, fingerprint_started_=false;
    unsigned width_=0,height_=0,truncated_=0,last_work_=0;
    std::vector<cambc::map::camp_group> public_groups_;
    std::vector<PublicActor> actors_;
    std::vector<Group> groups_;
    std::set<int> seen_keys_;
    std::optional<Group> gathering_;
    std::size_t metadata_at_=0,member_at_=0,spawn_at_=0,altar_at_=0,route_group_=0,route_member_=0;
    std::size_t edge_at_=0,fingerprint_at_=0;
    int spawn_relocation_=0,altar_distance_=-1,group_distance_=-1;
    std::vector<cambc::vec2> spawns_,approaches_;
    std::optional<cambc::bitboard> terrain_;
    std::optional<cambc::bitboard> seen_member_spawns_;
    std::optional<cambc::distance_field> hub_;
    std::optional<cambc::bitboard> visited_;
    cambc::vec2 hub_position_{};
    Matrix matrix_;
    std::vector<Excursion> estimates_;
    Solver solver_;
    Fingerprint matrix_fingerprint_;
    DraftPlan published_plan_;

    unsigned work_limit() const {return bargaining(policy_)?8192u:4096u;}
    bool budget(unsigned cost=1) {
        if (cost > work_limit()-last_work_ || cambc::fuel_remaining()<45000) return false;
        last_work_+=cost;return true;
    }
    void initialize() {
        // A thrown initialization query must not leave a partially reusable
        // object that silently duplicates metadata on the next draft turn.
        unavailable_=true;
        const int width=cambc::map::width(),height=cambc::map::height();
        if (width<1 || width>100 || height<1 || height>100 || cambc::constants::draft_memory_bytes!=1024) return;
        width_=unsigned(width);height_=unsigned(height);
        const auto roster=plan3_runtime::market_fixed_roster(cambc::draft::team_size());
        if (roster.empty()) return;
        for (int ordinal:roster) {
            const auto stats=cambc::champion_designs[ordinal].base_stats();
            actors_.push_back({ordinal,stats.base_health,stats.base_strength,stats.base_focus,
                stats.camp_damage_multiplier,double(stats.base_move_speed)});
        }
        public_groups_=cambc::map::camp_groups();
        seen_member_spawns_.emplace();
        spawns_=cambc::map::spawn_region(cambc::team::ally).positions();
        if (spawns_.empty()) return;
        hub_position_=*std::min_element(spawns_.begin(),spawns_.end(),[](auto a,auto b) {
            return a.y==b.y ? a.x<b.x : a.y<b.y;
        });
        terrain_=cambc::map::walls() | cambc::map::holes() | cambc::map::altars() |
            cambc::map::spawn_region(cambc::team::enemy);
        cambc::bitboard approaches;
        for (auto altar:cambc::map::altars().positions())
            approaches.draw_chebyshev(altar,cambc::constants::submit_gems_range);
        approaches_=approaches.positions();
        initialized_=true;unavailable_=false;
    }
    void metadata() {
        while (metadata_at_<public_groups_.size()) {
            if (!budget()) return;
            const auto& source=public_groups_[metadata_at_];
            if (!gathering_) {
                gathering_.emplace();gathering_->source=metadata_at_;
                gathering_->model.key=std::numeric_limits<int>::max();
                gathering_->model.gems=source.gem_value();
                // This is the SDK physical-group contract, not a guessed live
                // connectedness/lifecycle observation from another actor.
                gathering_->model.connected=true;member_at_=0;
            }
            const auto members=source.camps();
            if (members.empty()) { unavailable_=true;return; }
            while (member_at_<members.size()) {
                if (!budget()) return;
                const auto& member=members[member_at_];const auto p=member.spawn_position();
                if (p.x<0 || p.y<0 || unsigned(p.x)>=width_ || unsigned(p.y)>=height_ || seen_member_spawns_->is_set(p)) {
                    unavailable_=true;return;
                }
                seen_member_spawns_->set(p);
                auto& model=gathering_->model;
                model.key=std::min(model.key,p.y*int(width_)+p.x);
                if (model.gems) {
                    const auto stats=member.base_stats();
                    model.health+=stats.base_health;model.incoming+=stats.base_strength;
                    if (!std::isfinite(model.health) || !std::isfinite(model.incoming) || stats.base_health<0 || stats.base_strength<0) {
                        unavailable_=true;return;
                    }
                }
                ++model.members;++member_at_;
            }
            // Validate physical member aliases even in gold-only groups, then
            // exclude them before retaining/capping matching candidates.
            if (!gathering_->model.gems) { gathering_.reset();++metadata_at_;continue; }
            if (!seen_keys_.insert(gathering_->model.key).second) { unavailable_=true;return; }
            const auto at=std::lower_bound(groups_.begin(),groups_.end(),gathering_->model.key,
                [](const Group& g,int key) { return g.model.key<key; });
            groups_.insert(at,std::move(*gathering_));
            if (groups_.size()>maximum_groups) { groups_.pop_back();++truncated_; }
            gathering_.reset();++metadata_at_;
        }
        metadata_complete_=true;
    }
    void compute_field() {
        unavailable_=true;
        cambc::bitboard roots;roots.set(hub_position_);
        hub_.emplace();visited_.emplace();
        // The radius parameter is not a guest work allowance. This is one
        // complete native query and must receive separate native validation.
        hub_->compute(*terrain_,roots,*visited_);field_complete_=true;unavailable_=false;
    }
    int distance(cambc::vec2 p) const {
        if (!visited_->is_set(p)) return -1;
        const auto d=hub_->get(p);
        return d==cambc::distance_field::unreachable ? -1 : int(d);
    }
    void routes() {
        while (spawn_at_<spawns_.size()) {
            if (!budget()) return;
            const int d=distance(spawns_[spawn_at_]);++spawn_at_;
            if (d<0) spawn_relocation_=-1;
            else if (spawn_relocation_>=0) spawn_relocation_=std::max(spawn_relocation_,d);
        }
        while (altar_at_<approaches_.size()) {
            if (!budget()) return;
            const int d=distance(approaches_[altar_at_]);++altar_at_;
            if (d>=0 && (altar_distance_<0 || d<altar_distance_)) altar_distance_=d;
        }
        while (route_group_<groups_.size()) {
            const auto members=public_groups_[groups_[route_group_].source].camps();
            while (route_member_<members.size()) {
                if (!budget()) return;
                const int d=distance(members[route_member_].spawn_position());++route_member_;
                if (d>=0 && (group_distance_<0 || d<group_distance_)) group_distance_=d;
            }
            groups_[route_group_++].distance=group_distance_;route_member_=0;group_distance_=-1;
        }
        routes_complete_=true;
    }
    PublicTiming timing() const {
        return {cambc::constants::capture_time,cambc::constants::recall_time,
            cambc::constants::submit_gem_base_duration,cambc::constants::submit_gem_minimum_duration,
            cambc::constants::submit_gem_duration_multiplier,cambc::constants::spawn_healing};
    }
    void cells() {
        if (!matrix_started_) {
            // Debit the bounded <=320-cell initialization, not a fictitious
            // single cheap transition. Allocation itself is indivisible.
            if (!budget(unsigned(groups_.size()*actors_.size()+actors_.size()+groups_.size()+1))) return;
            for (const auto& a:actors_) matrix_.actors.push_back(a.ordinal);
            for (const auto& g:groups_) matrix_.groups.push_back(g.model.key);
            matrix_.cells.resize(actors_.size()*groups_.size());
            estimates_.resize(matrix_.cells.size());matrix_started_=true;
        }
        while (edge_at_<matrix_.cells.size()) {
            if (!budget()) return;
            const auto a=edge_at_/groups_.size(),g=edge_at_%groups_.size();
            const auto estimate=estimate_excursion(actors_[a],groups_[g].model,timing(),groups_[g].distance,altar_distance_,spawn_relocation_);
            if (estimate.cell.state==CellState::unknown) throw std::runtime_error("Assignment unfinished completed-route estimate");
            matrix_.cells[edge_at_]=estimate.cell;estimates_[edge_at_]=estimate;++edge_at_;
        }
    }
    bool fingerprint() {
        if (!fingerprint_started_) {
            if (!budget(unsigned(matrix_.actors.size()+matrix_.groups.size()+1))) return false;
            matrix_fingerprint_.text("P3AS-T1");matrix_fingerprint_.u16(horizon);
            matrix_fingerprint_.text(bargaining(policy_)?"idle-disagreement-zero":"optional-assignment");
            matrix_fingerprint_.u16(width_);matrix_fingerprint_.u16(height_);
            matrix_fingerprint_.u16(unsigned(matrix_.groups.size()));matrix_fingerprint_.byte(unsigned(matrix_.actors.size()));
            for (int a:matrix_.actors) matrix_fingerprint_.byte(unsigned(a));
            for (int g:matrix_.groups) matrix_fingerprint_.u16(unsigned(g));
            fingerprint_started_=true;
        }
        while (fingerprint_at_<matrix_.cells.size()) {
            if (!budget()) return false;
            const auto& cell=matrix_.cells[fingerprint_at_++];
            if (cell.state==CellState::unknown) throw std::runtime_error("Assignment unknown matrix fingerprint");
            matrix_fingerprint_.byte(cell.state==CellState::known ? 0u : 1u);
            matrix_fingerprint_.u32(cell.state==CellState::known ? cell.utility : 0u);
        }
        return true;
    }
    bool solve() {
        if (!solver_started_) {
            // Charge the bounded bridge copy and kernel-bank initialization;
            // none of these indivisible costs is hidden in step accounting.
            const unsigned startup=unsigned(matrix_.cells.size())*(bargaining(policy_)?2u:1u)+(bargaining(policy_)?4u:2u)*32u+1u;
            if (!budget(startup)) return false;
            solver_.start(matrix_);
            solver_started_=true;
        }
        for (;;) {
            const auto status=solver_.status();
            if (status==KernelStatus::complete) return true;
            if (status!=KernelStatus::pending) { unavailable_=true;return false; }
            if (last_work_>=work_limit() || cambc::fuel_remaining()<45000) return false;
            const unsigned request=std::min(8u,work_limit()-last_work_);
            solver_.step(request);
            const unsigned used=solver_.last_step_units();
            if (used>request || (!used && solver_.status()==KernelStatus::pending))
                throw std::runtime_error("Assignment solver work accounting");
            last_work_+=used;
        }
    }
    bool publish() {
        // A bounded1024-byte local codec/checksum is one publication batch;
        // its real fuel is not inferred from the logical work counter.
        if (!budget()) return false;
        DraftPlan plan;plan.complete=true;plan.policy=policy_;plan.width=width_;plan.height=height_;
        plan.feasible=solver_.result().feasible;plan.disagreement=solver_.result().disagreement;
        for (const auto& a:actors_) plan.actors.push_back(a.ordinal);
        for (const auto& g:groups_) plan.groups.push_back(g.model.key);
        plan.owners=solver_.result().owners;
        if (!plan.feasible) plan.owners.assign(groups_.size(),-1);
        plan.matrixFingerprint=matrix_fingerprint_.value;
        const auto bytes=encode_plan(plan);
        if (!bytes) throw std::runtime_error("Assignment invalid complete publication");
        cambc::draft_memory=*bytes;
        const auto joined=read_plan(policy_,unsigned(actors_.size()));
        if (!joined.complete || joined.matrixFingerprint!=plan.matrixFingerprint || joined.owners!=plan.owners)
            throw std::runtime_error("Assignment draft publication readback failed");
        published_plan_=std::move(plan);published_=true;return true;
    }
public:
    explicit DraftAllocator(Policy policy):policy_(policy),solver_(policy) {if(!valid_policy(policy))unavailable_=true;}
    DraftStatus step() {
        try { return step_impl(); }
        catch (...) { unavailable_=true;throw; }
    }
private:
    DraftStatus step_impl() {
        last_work_=0;
        if (published_) return DraftStatus::published;
        if (unavailable_ || !cambc::is_draft()) return DraftStatus::unavailable;
        if (!initialized_) { if (!budget()) return DraftStatus::pending;initialize(); }
        if (unavailable_) return DraftStatus::unavailable;
        if (!metadata_complete_) metadata();
        if (unavailable_) return DraftStatus::unavailable;
        if (!metadata_complete_) return DraftStatus::pending;
        // An empty public matrix needs no field; the kernels still distinguish
        // a feasible optional matching from infeasible full-actor bargaining.
        if (!groups_.empty()) {
            if (!field_complete_) { if (!budget()) return DraftStatus::pending;compute_field(); }
            if (!routes_complete_) routes();
            if (!routes_complete_) return DraftStatus::pending;
        }
        cells();
        if (!matrix_started_ || edge_at_!=matrix_.cells.size() || !fingerprint()) return DraftStatus::pending;
        if (!solve()) return unavailable_ ? DraftStatus::unavailable : DraftStatus::pending;
        return publish() ? DraftStatus::published : DraftStatus::pending;
    }
public:
    unsigned considered_groups() const { return unsigned(groups_.size()); }
    unsigned truncated_groups() const { return truncated_; }
    unsigned last_work_units() const { return last_work_; }
    const DraftPlan& plan() const { return published_plan_; }
    const Matrix& matrix() const { return matrix_; }
    const Result& result() const { return solver_.result(); }
    unsigned maximum_callback_work() const {return work_limit();}
    const std::vector<Excursion>& estimates() const { return estimates_; }
    std::uint32_t matrix_fingerprint() const { return matrix_fingerprint_.value; }
};

} // namespace plan3_assignment
