#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

// Constructive POSITION histories for one explicit future fixed-Hold scenario.
// These are not reach envelopes, observed hidden state, kit/stat models, action
// permissions or utilities. An unseen origin stays on one public spawn tile;
// a positive sample begins a NEW hypothesis at its actual observed position.
// Every later supplied complete personal observation must remain consistent.
namespace wb::capital::service_scenario {
inline constexpr unsigned actor_limit=6,cell_limit=10000,mask_bytes=(cell_limit+7)/8;
enum class Entity {Champion,Npc};
struct Actor {int id{-1},team{-1};Entity entity{Entity::Champion};};
inline constexpr bool operator==(Actor a,Actor b){return a.id==b.id&&a.team==b.team&&a.entity==b.entity;}
struct Point {int x{-1},y{-1};};
inline constexpr bool operator==(Point a,Point b){return a.x==b.x&&a.y==b.y;}
struct Stamp {int round{-1};std::uint64_t input{},effect{};};
inline constexpr bool operator==(Stamp a,Stamp b){return a.round==b.round&&a.input==b.input&&a.effect==b.effect;}
struct Source {std::uint64_t game{},map{},roster{};Actor observer{};};
inline constexpr bool operator==(Source a,Source b){return a.game==b.game&&a.map==b.map&&a.roster==b.roster&&a.observer==b.observer;}
enum class Presence {Hidden,Visible};
enum class Life {Unknown,Alive,Dead};
enum class Channel {Unknown,None,Active};
enum class Effects {Unknown,NoAutomatic,Unsupported};
struct Facts {Life life{Life::Unknown};Channel channel{Channel::Unknown};Effects effects{Effects::Unknown};};
struct Sample {
    Actor actor{};Presence presence{Presence::Hidden};Point position{};Facts facts{};
    // Refers to separately owned ACTUAL stat/kit/equipment facts. This module
    // never fills, extrapolates or turns that binding into fresh hidden stats.
    std::uint64_t actual_binding{};
};
struct Spawn {int team{-1};std::span<const std::uint8_t> cells{};};
struct Initial {
    Source source{};Stamp game_start{};unsigned width{},height{};
    std::span<const Actor> roster{};std::span<const Spawn> spawns{};
    bool complete_roster{},public_spawns_complete{};
};
struct Batch {
    Source source{};Stamp stamp{};
    // Exactly ceil(width*height/8) bytes. A set bit means personally visible;
    // Hidden samples remove that tile. Visible samples must occupy a set bit.
    std::span<const std::uint8_t> visible{};std::span<const Sample> samples{};
    bool complete_visibility{},complete_roster{};
};
enum class Status {Empty,Ready,Applied,Duplicate,Rejected,Stale,Conflict,Capacity,Unknown};
enum class Reason {None,Source,Stamp,Roster,Namespace,SpawnMask,VisibilityMask,SampleCount,
    SampleActor,SamplePosition,PositiveOutsideVision,ActualBinding,NotObserved,Generation,Funding,JointOrigins};
enum class Geometry {Unknown,Supported,Inconsistent};
enum class Evidence {None,ActualAtCurrent,ConditionalFixedOrigin};
enum class Assignment {NotAttempted,Complete,Inconsistent,NeedFuel};
enum class GeometryReason {None,NoCurrentObservation,NoSurvivingSpawn,ObservedOriginRefuted};
enum class ClassReason {None,LifeUnknown,Dead,ChannelUnknown,ActiveChannel,EffectsUnknown,UnsupportedEffects};
struct Actual {
    bool present{};Point position{};Stamp stamp{};Facts facts{};std::uint64_t binding{};
};
struct Witness {
    Actor actor{};Geometry geometry{Geometry::Unknown};Evidence evidence{Evidence::None};Point point{};
    Stamp origin{},last_consistent{};bool origin_is_public_spawn{},point_assigned{},required_class_usable{};
    GeometryReason geometry_reason{GeometryReason::NoCurrentObservation};ClassReason class_reason{ClassReason::LifeUnknown};
    Facts current{};Actual last_actual{};
};
struct Snapshot {
    Status status{Status::Empty};Reason reason{Reason::NotObserved};Source source{};Stamp stamp{};
    std::uint64_t generation{},batch_fingerprint{};unsigned count{};std::array<Witness,actor_limit> actors{};
    Assignment assignment{Assignment::NotAttempted};bool joint_positions_validated{},all_required_classes_usable{};
};
struct Update {Status status{Status::Empty};Reason reason{Reason::None};std::uint64_t generation{};bool any_inconsistent{};};
struct Work {void* context{};bool(*permit)(void*,unsigned){};};

class Tracker {
    struct History {
        // Active prefix only, strictly x/y lexicographic. Reset owns copies;
        // negative observation compacts survivors without restoring old tails.
        Actor actor{};std::vector<std::uint16_t> candidates;
        bool observed_origin{},inconsistent{},current_visible{};Point origin{};Stamp origin_stamp{},consistent{};
        Facts current{};Actual actual{};
    };
    Source source_{};Stamp start_{},last_{};unsigned width_{},height_{},cells_{},bytes_{},count_{};
    std::array<History,actor_limit> history_{};std::uint64_t generation_{},fingerprint_{};bool initialized_{},observed_{};
    static bool champion(Actor actor){return actor.entity==Entity::Champion;}
    static bool valid(Actor actor){return champion(actor)&&actor.id>=0&&(actor.team==0||actor.team==1);}
    static bool valid(Stamp stamp){return stamp.round>=0;}
    static bool later(Stamp next,Stamp old){return next.round>=old.round&&(next.input>old.input||(next.input==old.input&&next.round==old.round&&next.effect>old.effect));}
    bool inside(Point p)const{return p.x>=0&&p.y>=0&&unsigned(p.x)<width_&&unsigned(p.y)<height_;}
    unsigned at(Point p)const{return unsigned(p.y)*width_+unsigned(p.x);}
    bool bit(std::span<const std::uint8_t> mask,Point p)const{return inside(p)&&(mask[at(p)/8]&(1u<<(at(p)%8)));}
    bool full_mask(std::span<const std::uint8_t> mask)const {
        if(mask.size()!=bytes_)return false;
        const unsigned tail=cells_%8;return !tail||!(mask[bytes_-1]&static_cast<std::uint8_t>(255u<<tail));
    }
    bool nonempty(std::span<const std::uint8_t> mask)const{for(auto value:mask)if(value)return true;return false;}
    bool advance_generation(){if(generation_==std::numeric_limits<std::uint64_t>::max())return false;++generation_;return true;}
    Update result(Status status,Reason reason=Reason::None)const{return {status,reason,generation_,false};}
    static ClassReason class_reason(const History& h) {
        // A later unseen Hold hypothesis cannot erase an ACTUALLY observed
        // active channel/death/unsupported effect. A fresh positive sample is
        // required to establish the changed state in this narrow module.
        const auto prior=h.actual.present&&!h.current_visible?h.actual.facts:Facts{};
        if(h.current.life==Life::Dead||prior.life==Life::Dead)return ClassReason::Dead;
        if(h.current.life!=Life::Alive)return ClassReason::LifeUnknown;
        if(h.current.channel==Channel::Active||prior.channel==Channel::Active)return ClassReason::ActiveChannel;
        if(h.current.channel!=Channel::None)return ClassReason::ChannelUnknown;
        if(h.current.effects==Effects::Unsupported||prior.effects==Effects::Unsupported)return ClassReason::UnsupportedEffects;
        if(h.current.effects!=Effects::NoAutomatic)return ClassReason::EffectsUnknown;
        return ClassReason::None;
    }
    enum class Match {Found,Impossible,Deferred};
    bool pay(Work work,std::uint64_t generation)const{return work.permit&&work.permit(work.context,1)&&initialized_&&observed_&&generation_==generation;}
    Match augment(unsigned actor,std::array<int,actor_limit>& cells,std::array<bool,actor_limit>& visited,Work work,std::uint64_t generation)const {
        if(visited[actor])return Match::Impossible;visited[actor]=true;
        // Every tested surviving candidate pays one unit. Empty map cells and
        // already-refuted candidates are no longer traversed. Each root visits
        // at most 6 actors; the total bound is 6*sum(active counts)+6 fixed
        // reservations, still <=6*6*10000+6. Reset/indexing and observe/filter
        // costs are separate caller-paid work, not hidden in this accounting.
        // The original x/y order and augmenting-path semantics are unchanged.
        for(const auto cell:history_[actor].candidates){if(!pay(work,generation))return Match::Deferred;
            unsigned owner=count_;for(unsigned a=0;a<count_;++a)if(cells[a]==int(cell))owner=a;
            if(owner==count_){cells[actor]=int(cell);return Match::Found;}
            if(history_[owner].observed_origin)continue;
            const auto displaced=augment(owner,cells,visited,work,generation);if(displaced==Match::Deferred)return displaced;
            if(displaced==Match::Found){cells[actor]=int(cell);return Match::Found;}
        }
        return Match::Impossible;
    }
    static void hash(std::uint64_t& value,std::uint64_t part){value=(value^part)*1099511628211ull;}
    std::uint64_t batch_hash(const Batch& batch,const std::array<unsigned,actor_limit>& indices)const {
        std::uint64_t value=1469598103934665603ull;
        hash(value,batch.source.game);hash(value,batch.source.map);hash(value,batch.source.roster);hash(value,unsigned(batch.stamp.round));hash(value,batch.stamp.input);hash(value,batch.stamp.effect);
        for(auto byte:batch.visible)hash(value,byte);
        for(unsigned a=0;a<count_;++a){const auto& s=batch.samples[indices[a]];
            hash(value,unsigned(s.actor.id));hash(value,unsigned(s.actor.team));hash(value,unsigned(s.presence));hash(value,unsigned(s.position.x));hash(value,unsigned(s.position.y));
            hash(value,unsigned(s.facts.life));hash(value,unsigned(s.facts.channel));hash(value,unsigned(s.facts.effects));hash(value,s.actual_binding);}
        return value;
    }
public:
    // Builds at most 2*10000 public-spawn index entries in x/y order, then owns
    // <=6*10000 copied active candidates. Allocation/index construction/copies
    // are separately caller-paid cold/reset work, not native fuel proof. No
    // span survives this call. Invalid inputs leave prior state untouched.
    Update reset(const Initial& in) {
        if(!in.source.game||!in.source.map||!in.source.roster||!valid(in.source.observer))return result(Status::Rejected,champion(in.source.observer)?Reason::Source:Reason::Namespace);
        if(!valid(in.game_start))return result(Status::Rejected,Reason::Stamp);
        const auto cells=std::uint64_t(in.width)*in.height;
        if(!in.width||!in.height||cells>cell_limit||in.roster.empty()||in.roster.size()>actor_limit||in.spawns.empty()||in.spawns.size()>2)return result(Status::Capacity,Reason::Roster);
        if(!in.complete_roster||!in.public_spawns_complete)return result(Status::Rejected,Reason::Roster);
        const unsigned bytes=unsigned((cells+7)/8);bool owner=false;
        for(unsigned a=0;a<in.roster.size();++a){const auto actor=in.roster[a];
            if(!valid(actor))return result(Status::Rejected,champion(actor)?Reason::Roster:Reason::Namespace);
            owner=owner||actor==in.source.observer;for(unsigned b=0;b<a;++b)if(in.roster[b].id==actor.id)return result(Status::Rejected,Reason::Roster);
            bool found=false;for(const auto& spawn:in.spawns)if(spawn.team==actor.team)found=true;
            if(!found)return result(Status::Rejected,Reason::SpawnMask);
        }
        if(!owner)return result(Status::Rejected,Reason::Roster);
        for(unsigned a=0;a<in.spawns.size();++a){const auto& spawn=in.spawns[a];
            if((spawn.team!=0&&spawn.team!=1)||spawn.cells.size()!=bytes||!nonempty(spawn.cells))return result(Status::Rejected,Reason::SpawnMask);
            for(unsigned b=0;b<a;++b)if(in.spawns[b].team==spawn.team)return result(Status::Rejected,Reason::SpawnMask);
            if(cells%8&&(spawn.cells[bytes-1]&static_cast<std::uint8_t>(255u<<(cells%8))))return result(Status::Rejected,Reason::SpawnMask);
        }
        std::array<std::vector<std::uint16_t>,2> spawn_index;
        for(const auto& spawn:in.spawns){auto& index=spawn_index[unsigned(spawn.team)];unsigned size=0;
            for(auto byte:spawn.cells)size+=std::popcount(unsigned(byte));index.reserve(size);
            for(unsigned x=0;x<in.width;++x)for(unsigned y=0;y<in.height;++y){const unsigned cell=y*in.width+x;
                if(spawn.cells[cell/8]&(1u<<(cell%8)))index.push_back(static_cast<std::uint16_t>(cell));}}
        std::array<History,actor_limit> next;
        for(unsigned a=0;a<in.roster.size();++a){auto& h=next[a];h.actor=in.roster[a];h.candidates=spawn_index[unsigned(h.actor.team)];h.origin_stamp=h.consistent=in.game_start;}
        if(!advance_generation())return result(Status::Capacity,Reason::Generation);
        source_=in.source;start_=in.game_start;last_={};width_=in.width;height_=in.height;cells_=unsigned(cells);bytes_=bytes;count_=unsigned(in.roster.size());observed_=false;fingerprint_=0;
        history_=std::move(next);
        initialized_=true;return result(Status::Ready);
    }
    Update observe(const Batch& batch) {
        if(!initialized_||!(batch.source==source_))return result(Status::Rejected,Reason::Source);
        if(!valid(batch.stamp)||batch.stamp.round<start_.round||batch.stamp.input<start_.input||
            (batch.stamp.input==start_.input&&(batch.stamp.round!=start_.round||batch.stamp.effect<start_.effect)))return result(Status::Rejected,Reason::Stamp);
        if(!batch.complete_visibility||!full_mask(batch.visible))return result(Status::Rejected,Reason::VisibilityMask);
        if(!batch.complete_roster||batch.samples.size()!=count_)return result(Status::Rejected,Reason::SampleCount);
        std::array<unsigned,actor_limit> indices{};std::array<bool,actor_limit> seen{};
        for(unsigned s=0;s<batch.samples.size();++s){const auto& sample=batch.samples[s];
            if(!champion(sample.actor))return result(Status::Rejected,Reason::Namespace);
            if(sample.presence!=Presence::Hidden&&sample.presence!=Presence::Visible)return result(Status::Rejected,Reason::SampleActor);
            unsigned a=count_;for(unsigned n=0;n<count_;++n)if(history_[n].actor==sample.actor)a=n;
            if(a==count_||seen[a])return result(Status::Rejected,Reason::SampleActor);seen[a]=true;indices[a]=s;
            if(sample.presence==Presence::Visible){if(!inside(sample.position))return result(Status::Rejected,Reason::SamplePosition);
                if(!bit(batch.visible,sample.position))return result(Status::Rejected,Reason::PositiveOutsideVision);
                if(!sample.actual_binding)return result(Status::Rejected,Reason::ActualBinding);}
            else if(sample.actual_binding)return result(Status::Rejected,Reason::ActualBinding);
        }
        const auto fingerprint=batch_hash(batch,indices);
        if(observed_&&batch.stamp==last_)return result(fingerprint==fingerprint_?Status::Duplicate:Status::Conflict, fingerprint==fingerprint_?Reason::None:Reason::Stamp);
        if(observed_&&!later(batch.stamp,last_))return result(Status::Stale,Reason::Stamp);
        if(!advance_generation())return result(Status::Capacity,Reason::Generation);
        bool inconsistent=false;
        for(unsigned a=0;a<count_;++a){auto& h=history_[a];const auto& sample=batch.samples[indices[a]];h.current=sample.facts;h.current_visible=sample.presence==Presence::Visible;
            if(h.current_visible){h.observed_origin=true;h.inconsistent=false;h.origin=sample.position;h.origin_stamp=h.consistent=batch.stamp;
                h.actual={true,sample.position,batch.stamp,sample.facts,sample.actual_binding};h.candidates.clear();}
            else if(h.observed_origin){if(bit(batch.visible,h.origin))h.inconsistent=true;else if(!h.inconsistent)h.consistent=batch.stamp;}
            else {std::size_t keep=0;for(const auto cell:h.candidates)if(!(batch.visible[cell/8]&(1u<<(cell%8))))h.candidates[keep++]=cell;
                h.candidates.resize(keep);h.inconsistent=h.candidates.empty();if(!h.inconsistent)h.consistent=batch.stamp;}
            inconsistent=inconsistent||h.inconsistent;
        }
        last_=batch.stamp;fingerprint_=fingerprint;observed_=true;return {Status::Applied,Reason::None,generation_,inconsistent};
    }
    // An explicit expected source+stamp prevents reusing old actual-at-current
    // facts after a rejected newer observation or a source reset.
    Snapshot read(Source source,Stamp stamp,Work work={})const {
        Snapshot out;out.source=source_;out.stamp=last_;out.generation=generation_;out.batch_fingerprint=fingerprint_;
        if(!initialized_||!(source==source_)){out.status=Status::Rejected;out.reason=Reason::Source;return out;}
        if(!observed_){out.reason=Reason::NotObserved;return out;}
        if(!(stamp==last_)){out.status=Status::Stale;out.reason=Reason::Stamp;return out;}
        out.status=Status::Unknown;out.reason=Reason::None;out.count=count_;
        bool inconsistent=false;
        for(unsigned a=0;a<count_;++a){const auto& h=history_[a];auto& w=out.actors[a];w.actor=h.actor;w.current=h.current;w.last_actual=h.actual;w.origin=h.origin_stamp;w.last_consistent=h.consistent;
            w.origin_is_public_spawn=!h.observed_origin;w.class_reason=class_reason(h);
            if(h.inconsistent){inconsistent=true;w.geometry=Geometry::Inconsistent;w.geometry_reason=h.observed_origin?GeometryReason::ObservedOriginRefuted:GeometryReason::NoSurvivingSpawn;continue;}
            w.geometry=Geometry::Supported;w.geometry_reason=GeometryReason::None;w.evidence=h.current_visible?Evidence::ActualAtCurrent:Evidence::ConditionalFixedOrigin;
            if(h.observed_origin){w.point=h.origin;w.point_assigned=true;}
        }
        if(inconsistent){out.reason=Reason::JointOrigins;out.assignment=Assignment::Inconsistent;return out;}
        std::array<int,actor_limit> assigned;assigned.fill(-1);
        for(unsigned a=0;a<count_;++a)if(history_[a].observed_origin){if(!pay(work,out.generation)){
                out.reason=generation_==out.generation?Reason::Funding:Reason::Source;out.status=generation_==out.generation?Status::Unknown:Status::Stale;
                out.assignment=generation_==out.generation?Assignment::NeedFuel:Assignment::NotAttempted;return out;}
            assigned[a]=int(at(history_[a].origin));for(unsigned b=0;b<a;++b)if(assigned[a]==assigned[b]){
                out.reason=Reason::JointOrigins;out.assignment=Assignment::Inconsistent;return out;}}
        for(unsigned a=0;a<count_;++a)if(!history_[a].observed_origin){std::array<bool,actor_limit> visited{};const auto match=augment(a,assigned,visited,work,out.generation);
            if(match!=Match::Found){if(generation_!=out.generation){out.status=Status::Stale;out.reason=Reason::Source;out.assignment=Assignment::NotAttempted;return out;}
                out.reason=match==Match::Deferred?Reason::Funding:Reason::JointOrigins;out.assignment=match==Match::Deferred?Assignment::NeedFuel:Assignment::Inconsistent;return out;}}
        out.status=Status::Ready;out.assignment=Assignment::Complete;out.joint_positions_validated=out.all_required_classes_usable=true;
        for(unsigned a=0;a<count_;++a){auto& w=out.actors[a];w.point={assigned[a]%int(width_),assigned[a]/int(width_)};w.point_assigned=true;
            w.required_class_usable=w.class_reason==ClassReason::None;out.all_required_classes_usable=out.all_required_classes_usable&&w.required_class_usable;}
        return out;
    }
    void invalidate(){if(advance_generation()){initialized_=observed_=false;count_=0;fingerprint_=0;source_={};last_={};}}
};
} // namespace wb::capital::service_scenario
