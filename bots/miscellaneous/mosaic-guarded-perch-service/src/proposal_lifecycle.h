#pragma once
// Negative NOMINATION evidence only. This never changes dated member facts,
// action/safety authority, occupancy, capture goals, or a body's possible life.
#include "selected_transition.h"

namespace mosaic::proposal {
class Lifecycle {
    struct Mark {int observed{-1},probe{-1};};
    struct Group {int nonalive_epoch{-1};std::uint64_t generation{};};
    std::span<const frame::SourceCamp> catalog_;
    spatial::Token layout_;
    std::vector<Mark> marks_;
    std::vector<Group> groups_;
    selected::Stamp latest_;
    static bool earlier(selected::Stamp a,selected::Stamp b) {
        return a.epoch<b.epoch||(a.epoch==b.epoch&&a.revision<b.revision);
    }
    bool current_binding() const{return layout_.valid(catalog_.size())&&marks_.size()==catalog_.size();}
    bool ordered(selected::Stamp current) const {
        return current.matches(current)&&(latest_.actor<0||
            (current.actor==latest_.actor&&!earlier(current,latest_)));
    }
    void clear_group(int id) {
        for(std::size_t i=0;i<catalog_.size();++i)if(catalog_[i].design.group==id)marks_[i]={};
    }
    static int probe_at(int epoch,int count) {
        // At most one decrement per round and no within-owner-input respawn.
        // One tick of publication slack; equality ONLY permits a fresh probe.
        const auto slack=count>0?std::int64_t(count)-1:0;
        const auto result=std::int64_t(epoch)+slack;
        // An unrepresentable deadline supplies no durable negative interval.
        return result>std::numeric_limits<int>::max()?epoch:static_cast<int>(result);
    }
public:
    void reset(){catalog_={};layout_={};marks_.clear();groups_.clear();latest_={};}
    bool bind(std::span<const frame::SourceCamp> catalog,spatial::Token token,std::size_t group_count) {
        if(!token.valid(catalog.size())){reset();return false;}
        if(catalog_.data()==catalog.data()&&catalog_.size()==catalog.size()&&
           selected::same_token(layout_,token)&&groups_.size()==group_count)return true;
        reset();
        for(const auto& row:catalog)if(row.design.handle<0||row.design.group<0||std::size_t(row.design.group)>=group_count)return false;
        catalog_=catalog;layout_=token;marks_.resize(catalog.size());groups_.resize(group_count);return true;
    }
    bool marked(std::size_t index) const {
        return current_binding()&&index<marks_.size()&&marks_[index].observed>=0;
    }
    std::uint64_t group_generation(std::size_t id) const {
        return current_binding()&&id<groups_.size()?groups_[id].generation:0;
    }
    bool suppressed(std::size_t index,int epoch) const {
        if(!marked(index)||epoch<marks_[index].observed)return false;
        return epoch==marks_[index].observed||epoch<marks_[index].probe;
    }
    bool may_propose(std::size_t index,int epoch) const {
        return current_binding()&&index<marks_.size()&&epoch>=0&&!suppressed(index,epoch);
    }
    // Immediate actual-query event, NOT a copied packet Group with only a round
    // date. Preserve the query Stamp; a replay from an older revision is rejected.
    bool observe_group(const frame::Group& fact,selected::Stamp observed,selected::Stamp current) {
        if(!current_binding()||!observed.matches(current)||!ordered(current)||
           fact.id<0||std::size_t(fact.id)>=groups_.size()||fact.observed_round!=observed.epoch)return false;
        auto& state=groups_[std::size_t(fact.id)];
        if(fact.state==frame::GroupState::alive) {
            if(state.nonalive_epoch>=0&&observed.epoch>state.nonalive_epoch) {
                clear_group(fact.id);state.nonalive_epoch=-1;++state.generation;
            }
            // Same-input Alive cannot establish a new group respawn.
        } else if(fact.state==frame::GroupState::reward||fact.state==frame::GroupState::respawning)
            state.nonalive_epoch=std::max(state.nonalive_epoch,observed.epoch);
        latest_=current;return true;
    }
    bool observe(const selected::Packet& packet,selected::Stamp current,std::span<const int> configured_respawn) {
        if(!current_binding()||!ordered(current)||!packet.matches(current,packet.request)||
           !selected::same_token(packet.layout,layout_)||packet.catalog.data()!=catalog_.data()||
           configured_respawn.size()!=groups_.size())return false;
        const selected::RowLookup lookup(packet);if(!lookup.valid)return false;
        // Copied same-input lifecycle may supply a conservative interval or an
        // old nonalive barrier. It NEVER promotes copied Alive to a new cycle.
        for(const auto& fact:packet.groups)if(fact.observed_round==current.epoch&&fact.id>=0&&
            std::size_t(fact.id)<groups_.size()&&(fact.state==frame::GroupState::reward||fact.state==frame::GroupState::respawning))
            groups_[std::size_t(fact.id)].nonalive_epoch=std::max(groups_[std::size_t(fact.id)].nonalive_epoch,current.epoch);
        // Parent feeds any actual group-cycle event BEFORE this packet, so a
        // newly observed death survives invalidation of the old proposal cycle.
        for(const auto& row:packet.rows) {
            if(!row.state_fresh||!row.camp.known)continue;
            auto& mark=marks_[row.index];
            if(row.camp.alive){mark={};continue;}
            int count=0;const auto* fact=selected::group(packet,row.camp.group);
            if(fact&&fact->observed_round==current.epoch) {
                if(fact->state==frame::GroupState::respawning)count=fact->cooldown;
                else if(fact->state==frame::GroupState::alive||fact->state==frame::GroupState::reward)
                    count=configured_respawn[std::size_t(row.camp.group)];
            }
            const auto next=probe_at(current.epoch,count);
            // A later unknown lifecycle cannot renew the bound, but need not
            // erase a still-valid bound established by an earlier actual query.
            mark={current.epoch,std::max(mark.probe,next)};
        }
        latest_=current;return true;
    }
};
} // namespace mosaic::proposal
