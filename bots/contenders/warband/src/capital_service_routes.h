#pragma once
#include "capital_service_forecast.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

// Conditional geometry/order producer for ONE shared group World. No lease,
// safety, utility, hidden state, arrival-time prediction or SDK permission.
namespace wb::capital::service_routes {
namespace f=service_forecast;
using Point=f::Point;
inline constexpr unsigned cell_limit=10000,altar_limit=64,hint_limit=16,cache_limit=24;
inline constexpr std::uint16_t none=std::numeric_limits<std::uint16_t>::max();
enum class State {Empty,Building,Deferred,Ready,Unknown};
enum class Reason {None,Shape,Source,Family,Order,OrderChanges,CurrentSlot,VargTieOrder,
    MissingFootprints,ForeignFootprint,Station,NeedApproach,Unreachable,Capacity,Stale,Workspace,Intent,Cache};
struct Source {
    std::uint64_t map{},roots{},foreign{},candidate{},world{},input{},effect{};
    int owner{-1},round{-1};friend bool operator==(const Source&,const Source&)=default;
};
enum class Lock {Unknown,Unlocked,Locked};
struct Altar {Point point{};Lock lock{Lock::Unknown};};
struct Map {
    int width{},height{};std::uint64_t revision{},roots_revision{},foreign_revision{};
    // Byte-per-cell immutable masks. blocked is EXACT walls/holes/altars/enemy
    // spawn, never a dynamic occupancy or discounted NPC-Life mask.
    std::span<const std::uint8_t> blocked{},own_spawn{},foreign_positive{};
    bool static_complete{},foreign_complete{};
    std::span<const Altar> altars{};int lock_round{-1};std::uint64_t lock_revision{};
    // Optional personally dated occupancy: only a first-edge dependency. It
    // does NOT enter permanent reachability or a group refutation/cache key.
    std::span<const std::uint8_t> occupied{};int occupancy_round{-1};std::uint64_t occupancy_revision{};
};
struct Participant {
    int id{-1},team{-1},current_priority{},next_priority{},draft_rank{-1};
    bool current_known{},next_committed{},draft_known{};
};
struct OrderInput {
    std::span<const Participant> participants{};int round1_starting_team{-1},actor{-1};unsigned actual_slot{};
    bool actual_slot_known{},fixed_next_order{};
};
struct Order {
    std::array<f::Slot,f::slot_limit> even{},odd{};unsigned count{},current_slot{};
    int actor{-1},round{-1};bool varg_before_vendetta{};
};
enum class Family {Solo,Joint};
struct Intent {
    bool shield{},heal{},reset{},require_useful_support{};
    std::span<const f::Purchase> purchases{};
    std::span<const Point> work_hints{};
};
struct Request {
    // MUST pin an OWNED IMMUTABLE retained snapshot and borrowed spans for the
    // entire computation. Never use borrowed SDK views or mutate an actual
    // frame in place. Every advance checks the original Source, not a redate.
    const f::World* world{};Source source{};OrderInput order{};Family family{Family::Solo};
    std::array<Intent,f::actor_limit> intent{};
    bool varg_splash_known{};unsigned varg_splash_radius{};
};
struct Work {void* context{};bool(*permit)(void*,unsigned){};bool step()const{return !permit||permit(context,1);}};
enum class Traversal {Alive,Cleared,FullAlive};
enum class Goal {Work,Capture,Release,Altar,Spawn,Point};
struct Query {
    Traversal traversal{Traversal::Alive};Goal goal{Goal::Point};Point origin{},point{},heal_anchor{};
    unsigned actor{},reserved_count{};bool require_heal_anchor{};
    std::array<Point,f::actor_limit> reserved{};std::span<const Point> hints{};
};
struct FieldKey {std::uint64_t map{},roots{},foreign{},candidate{},goals{};Traversal traversal{};
    friend bool operator==(const FieldKey&,const FieldKey&)=default;};
namespace detail {
inline void mix(std::uint64_t& h,std::uint64_t v){h=(h^v)*1099511628211ull;}
inline bool inside(const Map& m,Point p){return p.x>=0&&p.y>=0&&p.x<m.width&&p.y<m.height;}
inline unsigned index(const Map& m,Point p){return unsigned(p.y)*unsigned(m.width)+unsigned(p.x);}
inline Point point(const Map& m,unsigned cell){return {int(cell%unsigned(m.width)),int(cell/unsigned(m.width))};}
inline unsigned cells(const Map& m){return m.width>0&&m.height>0&&std::uint64_t(m.width)*unsigned(m.height)<=cell_limit?unsigned(m.width)*unsigned(m.height):0;}
inline bool candidate_cell(const f::World& w,Point p,bool living){
    for(unsigned n=0;n<w.npcs;++n)if((!living||w.camps[n].hp>0)&&w.camps[n].pose==p)return true;return false;
}
inline bool positive(const f::World& w,Point p){for(unsigned n=0;n<w.npcs;++n)if(w.camps[n].strength>0&&f::distance(p,w.camps[n].pose)<=int(w.camps[n].range))return true;return false;}
inline bool reserved(const Query& q,Point p){for(unsigned n=0;n<q.reserved_count;++n)if(q.reserved[n]==p)return true;return false;}
inline bool open(const Map& m,const f::World& w,const Query& q,Point p){
    if(!inside(m,p))return false;const auto cell=index(m,p);
    return !m.blocked[cell]&&!m.foreign_positive[cell]&&!reserved(q,p)&&
        (q.traversal==Traversal::Cleared||!candidate_cell(w,p,q.traversal==Traversal::Alive));
}
inline Lock lock(const Map& m,const Altar& a,int round){return m.lock_revision&&m.lock_round==round?a.lock:Lock::Unknown;}
inline bool goal(const Map& m,const f::World& w,const Query& q,Point p){
    if(!open(m,w,q,p))return false;
    switch(q.goal){
    case Goal::Point:return p==q.point;
    case Goal::Capture:return candidate_cell(w,p,false);
    case Goal::Release:return !positive(w,p);
    case Goal::Spawn:return m.own_spawn[index(m,p)]!=0;
    case Goal::Altar:for(const auto& a:m.altars)if(lock(m,a,w.round)!=Lock::Locked&&f::distance(p,a.point)==1)return true;return false;
    case Goal::Work:{
        const auto& b=w.bodies[q.actor];bool primary=false;
        for(unsigned n=0;n<w.npcs;++n)if(w.camps[n].hp>0&&f::distance(p,w.camps[n].pose)<=int(b.attack_range))primary=true;
        if(!primary)return false;
        if(q.require_heal_anchor&&f::distance(p,q.heal_anchor)>int(b.heal_range))return false;
        if(!q.hints.empty()){bool admitted=false;for(const auto hint:q.hints)admitted=admitted||hint==p;if(!admitted)return false;}
        return true;}
    }
    return false;
}
inline FieldKey key(const Map& m,const f::World& w,const Query& q,std::uint64_t candidate){
    std::uint64_t c=candidate,g=1469598103934665603ull;
    for(unsigned n=0;n<w.npcs;++n){const auto& x=w.camps[n];mix(c,unsigned(x.id));mix(c,unsigned(x.pose.x));mix(c,unsigned(x.pose.y));mix(c,x.hp>0);mix(c,x.range);mix(c,std::bit_cast<std::uint32_t>(x.strength));}
    mix(g,unsigned(q.goal));if(q.goal==Goal::Point){mix(g,unsigned(q.point.x));mix(g,unsigned(q.point.y));}mix(g,q.actor);mix(g,q.require_heal_anchor);
    if(q.require_heal_anchor){mix(g,unsigned(q.heal_anchor.x));mix(g,unsigned(q.heal_anchor.y));}
    if(q.actor<w.actors){const auto& b=w.bodies[q.actor];mix(g,unsigned(b.kit));mix(g,b.attack_range);mix(g,b.heal_range);}
    for(unsigned n=0;n<q.reserved_count;++n){mix(g,unsigned(q.reserved[n].x));mix(g,unsigned(q.reserved[n].y));}
    for(const auto p:q.hints){mix(g,unsigned(p.x));mix(g,unsigned(p.y));}
    if(q.goal==Goal::Altar)for(const auto& a:m.altars){mix(g,unsigned(a.point.x));mix(g,unsigned(a.point.y));mix(g,unsigned(lock(m,a,w.round)));}
    return {m.revision,m.roots_revision,m.foreign_revision,c?c:1,g?g:1,q.traversal};
}
inline int controlled(const f::World& w,int id){for(unsigned a=0;a<w.actors;++a)if(w.bodies[a].id==id)return int(a);return -1;}
inline int external(const f::World& w,int id){for(unsigned a=0;a<w.externals;++a)if(w.others[a].id==id)return int(a);return -1;}
}

// Legitimate caller-retained storage. Large arrays intentionally have no DMIs;
// no element is read until the explicit bounded cold pass has initialized its
// mark, or a current-generation write initialized its distance/parent/queue.
// Construction/caller allocation is NOT paid by advance's quanta declaration.
class Workspace {
    friend class Search;friend class ReverseField;
    std::array<std::uint16_t,cell_limit> distance_,parent_,queue_;
    std::array<std::uint32_t,cell_limit> marks_;
    std::uint32_t generation_{};unsigned initialized_cells_{};
public:
    // User-provided: Workspace{} also avoids value-initializing the large
    // fundamental arrays. Their actual object lifetime is ordinary C++.
    Workspace() noexcept {} Workspace(const Workspace&)=delete;Workspace& operator=(const Workspace&)=delete;
    bool cold_ready(unsigned cells)const{return cells<=initialized_cells_;}
    unsigned cold_work_remaining(unsigned cells)const{return cells>initialized_cells_?cells-initialized_cells_:0;}
};
class Search {
    friend class ReverseField;
    const Map* map_{};const f::World* world_{};Workspace* workspace_{};Query query_{};
    unsigned head_{},tail_{},root_cursor_{};std::uint16_t found_{none};std::uint32_t generation_{};bool reverse_{},started_{},ready_{},unknown_{};
    void enqueue(unsigned cell,std::uint16_t parent,std::uint16_t distance){auto& s=*workspace_;s.marks_[cell]=s.generation_;s.distance_[cell]=distance;s.parent_[cell]=parent;s.queue_[tail_++]=std::uint16_t(cell);}
public:
    void begin(const Map& map,const f::World& world,const Query& query,Workspace& workspace,bool reverse=false){
        map_=&map;world_=&world;workspace_=&workspace;query_=query;head_=tail_=root_cursor_=0;found_=none;
        reverse_=reverse;generation_=0;started_=ready_=unknown_=false;
    }
    bool unit(){
        if(ready_||unknown_||!workspace_)return false;auto& s=*workspace_;
        if(started_&&s.generation_!=generation_){unknown_=true;return false;}
        const auto size=detail::cells(*map_);
        if(s.initialized_cells_<size){s.marks_[s.initialized_cells_++]=0;return true;}
        if(!started_){if(s.generation_==std::numeric_limits<std::uint32_t>::max()){s.initialized_cells_=0;s.generation_=0;return true;}
            ++s.generation_;generation_=s.generation_;started_=true;
            if(!reverse_){if(!detail::open(*map_,*world_,query_,query_.origin)){unknown_=true;return true;}
                const auto root=detail::index(*map_,query_.origin);enqueue(root,std::uint16_t(root),0);}
            return true;}
        if(reverse_&&root_cursor_<size){const auto cell=root_cursor_++;if(detail::goal(*map_,*world_,query_,detail::point(*map_,cell)))enqueue(cell,std::uint16_t(cell),0);return true;}
        if(head_==tail_){ready_=true;return true;}
        const auto cell=s.queue_[head_++];const auto p=detail::point(*map_,cell);
        if(!reverse_&&detail::goal(*map_,*world_,query_,p)){found_=cell;ready_=true;return true;}
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){if(!dx&&!dy)continue;const Point next{p.x+dx,p.y+dy};
            if(!detail::open(*map_,*world_,query_,next))continue;const auto n=detail::index(*map_,next);
            if(s.marks_[n]==s.generation_)continue;enqueue(n,cell,std::uint16_t(s.distance_[cell]+1));}
        return true;
    }
    bool ready()const{return ready_&&workspace_&&workspace_->generation_==generation_;}
    bool unknown()const{return unknown_||(started_&&workspace_&&workspace_->generation_!=generation_);}
    bool found()const{return ready()&&found_!=none;}
    std::uint16_t destination()const{return found_;}
    std::uint16_t parent(std::uint16_t cell)const{return started_&&workspace_->generation_==generation_&&workspace_->marks_[cell]==generation_?workspace_->parent_[cell]:none;}
};
// Optional prepared reverse cache. The caller pins BOTH immutable inputs and a
// dedicated Workspace while a Ready field is borrowed. Reusing that workspace
// revokes the field by generation; it is never an aliasable immortal view.
class ReverseField {
    Search search_;FieldKey key_{};std::uint32_t generation_{};State state_{State::Empty};std::uint64_t work_{},serial_{};
public:
    void begin(Map&&,const f::World&,const Query&,std::uint64_t,Workspace&)=delete;
    void begin(const Map&,f::World&&,const Query&,std::uint64_t,Workspace&)=delete;
    void begin(const Map& map,const f::World& world,const Query& query,std::uint64_t candidate,Workspace& workspace){
        if(serial_==std::numeric_limits<std::uint64_t>::max()){state_=State::Unknown;return;}++serial_;
        const auto size=detail::cells(map);
        if(!size||map.blocked.size()!=size||map.foreign_positive.size()!=size||map.own_spawn.size()!=size||
           !map.static_complete||!map.foreign_complete||world.actors<1||world.actors>f::actor_limit||world.npcs>f::npc_limit||
           query.actor>=world.actors||query.reserved_count>f::actor_limit||query.hints.size()>hint_limit||map.altars.size()>altar_limit)
            {state_=State::Unknown;return;}
        key_=detail::key(map,world,query,candidate);search_.begin(map,world,query,workspace,true);state_=State::Building;work_=0;generation_=0;
    }
    State advance(FieldKey current,unsigned units,Work work={}){
        if(current!=key_){state_=State::Unknown;return state_;}
        if(state_==State::Ready)return ready()?state_:(state_=State::Unknown);
        if(state_!=State::Building&&state_!=State::Deferred)return state_;state_=State::Building;
        for(unsigned n=0;n<units;++n){if(!work.step()){state_=State::Deferred;break;}if(!search_.unit())break;++work_;
            if(search_.unknown()){state_=State::Unknown;break;}if(search_.ready()){generation_=search_.workspace_->generation_;state_=State::Ready;break;}}
        return state_;
    }
    bool ready()const{return state_==State::Ready&&search_.workspace_&&search_.workspace_->generation_==generation_;}
    const FieldKey& key()const{return key_;}std::uint64_t work_used()const{return work_;}
    std::uint64_t serial()const{return serial_;}
    const Workspace* workspace()const{return search_.workspace_;}
    bool reachable(Point p)const{if(!ready()||!detail::inside(*search_.map_,p))return false;const auto n=detail::index(*search_.map_,p);return search_.workspace_->marks_[n]==generation_;}
    unsigned distance(Point p)const{return reachable(p)?search_.workspace_->distance_[detail::index(*search_.map_,p)]:none;}
    Point next(Point p)const{return reachable(p)?detail::point(*search_.map_,search_.workspace_->parent_[detail::index(*search_.map_,p)]):Point{-1,-1};}
};
enum class Occupancy {Unknown,Open,Occupied};
struct LegProof {
    Point origin{},station{};Traversal traversal{};FieldKey field{};
    bool requires_selected_full_clear{},requires_fresh_edge{true};Occupancy first_edge{Occupancy::Unknown};
    std::uint64_t occupancy_revision{};
};
struct ActorGeometry {int id{-1};f::Kit kit{};Point origin{};unsigned attack{},heal{},move{};};
struct NpcGeometry {int id{-1};Point pose{};bool alive{};f::Scalar strength{};unsigned range{};};
struct GeometryIdentity {
    unsigned actors{},npcs{};int team{-1};std::array<ActorGeometry,f::actor_limit> bodies{};
    std::array<NpcGeometry,f::npc_limit> camps{};bool splash_known{};unsigned splash{};
};
struct Product {
    State state{State::Empty};Reason reason{Reason::None};Source source{};Order order{};
    const f::Program* program{};std::array<std::array<LegProof,6>,f::actor_limit> legs{};
    std::array<Lock,f::actor_limit> locks{};std::uint64_t work{};unsigned last_work{};
    bool requires_primary_revalidation{true};
    GeometryIdentity geometry{};
};
inline FieldKey field_key(const Map& map,const f::World& world,const Query& query,std::uint64_t candidate){
    if(world.actors<1||world.actors>f::actor_limit||world.npcs>f::npc_limit||query.actor>=world.actors||
       query.reserved_count>f::actor_limit||query.hints.size()>hint_limit||map.altars.size()>altar_limit)return {};
    return detail::key(map,world,query,candidate);
}
inline GeometryIdentity geometry_identity(const Request& request){
    GeometryIdentity out;const auto& w=*request.world;out.actors=w.actors;out.npcs=w.npcs;out.team=w.team;
    out.splash_known=request.varg_splash_known;out.splash=request.varg_splash_radius;
    for(unsigned a=0;a<w.actors;++a){const auto& b=w.bodies[a];out.bodies[a]={b.id,b.kit,b.pose,b.attack_range,b.heal_range,b.move_range};}
    for(unsigned n=0;n<w.npcs;++n){const auto& c=w.camps[n];out.camps[n]={c.id,c.pose,c.hp>0,c.strength,c.range};}return out;
}
inline Reason order(const f::World& world,const OrderInput& input,Order& out){
    if(!input.actual_slot_known||!input.fixed_next_order||input.round1_starting_team<0||input.round1_starting_team>1||
       input.participants.size()!=world.actors+world.externals||input.participants.empty()||input.participants.size()>f::slot_limit)return Reason::Order;
    std::array<const Participant*,f::slot_limit> current{},next{};unsigned n=0;
    for(const auto& p:input.participants){
        if(p.id<0||p.team<0||p.team>1||!p.current_known||!p.next_committed||!p.draft_known||p.draft_rank<0)return Reason::Order;
        const auto own=detail::controlled(world,p.id),other=detail::external(world,p.id);
        if((own<0)==(other<0)||(own>=0?p.team!=world.team:p.team!=world.others[unsigned(other)].team))return Reason::Order;
        for(unsigned j=0;j<n;++j)if(current[j]->id==p.id||(current[j]->team==p.team&&current[j]->draft_rank==p.draft_rank))return Reason::Order;
        current[n]=next[n]=&p;++n;
    }
    const auto less=[](const Participant* a,const Participant* b,bool future){
        if(a->team!=b->team)return a->team<b->team;
        const auto ap=future?a->next_priority:a->current_priority,bp=future?b->next_priority:b->current_priority;
        return ap!=bp?ap>bp:a->draft_rank<b->draft_rank;};
    std::sort(current.begin(),current.begin()+n,[&](auto a,auto b){return less(a,b,false);});
    std::sort(next.begin(),next.begin()+n,[&](auto a,auto b){return less(a,b,true);});
    for(unsigned i=0;i<n;++i)if(current[i]->id!=next[i]->id)return Reason::OrderChanges;
    out.count=n;out.actor=input.actor;out.round=world.round;
    for(unsigned parity=0;parity<2;++parity){auto& slots=parity?out.odd:out.even;
        const int starter=input.round1_starting_team^int((parity+1u)%2u);
        std::array<const Participant*,f::slot_limit> sides[2];unsigned count[2]{},cursor[2]{},used=0;
        for(unsigned i=0;i<n;++i)sides[next[i]->team][count[next[i]->team]++]=next[i];
        while(used<n)for(int k=0;k<2;++k){const int team=starter^k;if(cursor[team]>=count[team])continue;
            const auto* p=sides[team][cursor[team]++];const int own=detail::controlled(world,p->id);
            slots[used++]={own>=0,own>=0?unsigned(own):0u,own>=0?-1:p->id};}
    }
    if(input.actual_slot>=n||world.slot!=input.actual_slot||world.phase!=f::Phase::ActorAction)return Reason::CurrentSlot;
    const auto& slot=(world.round%2?out.odd:out.even)[input.actual_slot];
    if(!slot.controlled||world.bodies[slot.actor].id!=input.actor)return Reason::CurrentSlot;
    out.current_slot=input.actual_slot;int vr=-1,vd=-1;
    for(unsigned i=0;i<n;++i){const auto& s=out.even[i];if(!s.controlled)continue;
        if(world.bodies[s.actor].kit==f::Kit::Varg)vr=int(i);if(world.bodies[s.actor].kit==f::Kit::Vendetta)vd=int(i);}
    out.varg_before_vendetta=vr>=0&&vd>=0&&vr<vd;return Reason::None;
}
inline bool source_matches(const Map& map,const Request& request,Source current){
    if(!request.world||current!=request.source)return false;const auto& w=*request.world;
    if(w.actors<1||w.actors>f::actor_limit||w.npcs<1||w.npcs>f::npc_limit||w.externals>f::external_limit||
       map.revision!=current.map||map.roots_revision!=current.roots||map.foreign_revision!=current.foreign||
       w.revision!=current.world||w.round!=current.round||w.horizon<w.round||
       !map.static_complete||!map.foreign_complete||!w.group.roster_complete||w.group.life!=f::Life::Alive)return false;
    for(unsigned a=0;a<w.actors;++a)if(!w.bodies[a].actual||!w.bodies[a].alive)return false;
    return detail::controlled(w,current.owner)>=0;
}

class Builder {
    const Map* map_{};Request request_{};Workspace* workspace_{};f::Program* program_{};
    std::span<const ReverseField* const> cache_{};Product out_;Search search_;Query query_{};const ReverseField* field_{};
    FieldKey field_key_{};std::uint64_t field_serial_{};
    std::array<unsigned,f::actor_limit> actor_order_{};unsigned planning_{},leg_{},trace_count_{},publish_{};
    std::array<std::array<Point,6>,f::actor_limit> stations_{};
    std::array<std::uint16_t,f::route_limit+1> trace_;
    std::uint16_t cursor_{none};bool tracing_{},publishing_{},forward_cache_{};
    bool stop(Reason r){out_.state=State::Unknown;out_.reason=r;out_.program=nullptr;return false;}
    const f::World& world()const{return *request_.world;}
    unsigned actor()const{return actor_order_[planning_];}
    bool source(Source current)const{return source_matches(*map_,request_,current);}
    void begin_leg(){
        const auto a=actor();query_=Query{};query_.actor=a;
        query_.origin=leg_?stations_[a][leg_-1]:world().bodies[a].pose;
        query_.traversal=leg_==1||leg_==2?Traversal::Cleared:leg_>2?Traversal::FullAlive:Traversal::Alive;
        query_.goal=leg_==0?Goal::Work:leg_==1?Goal::Capture:leg_==2?Goal::Release:leg_==3?Goal::Altar:leg_==4?Goal::Spawn:Goal::Point;
        query_.point=stations_[a][0];query_.hints=leg_==0?request_.intent[a].work_hints:std::span<const Point>{};
        if(leg_==0&&world().bodies[a].kit==f::Kit::Willow&&world().actors>1){
            for(unsigned n=0;n<planning_;++n)if(world().bodies[actor_order_[n]].kit==f::Kit::Varg){query_.require_heal_anchor=true;query_.heal_anchor=stations_[actor_order_[n]][0];}}
        for(unsigned n=0;n<planning_;++n)query_.reserved[query_.reserved_count++]=stations_[actor_order_[n]][leg_];
        field_=nullptr;const auto key=detail::key(*map_,world(),query_,request_.source.candidate);field_key_=key;
        for(const auto* cached:cache_)if(cached&&cached->ready()&&cached->key()==key&&cached->workspace()!=workspace_){field_=cached;break;}
        field_serial_=field_?field_->serial():0;
        trace_count_=publish_=0;tracing_=publishing_=false;forward_cache_=field_!=nullptr;
        if(field_){if(!field_->reachable(query_.origin)){stop(Reason::Unreachable);return;}cursor_=std::uint16_t(detail::index(*map_,query_.origin));tracing_=true;}
        else search_.begin(*map_,world(),query_,*workspace_);
    }
    bool coverage(){
        for(unsigned n=0;n<world().npcs;++n)if(world().camps[n].hp>0){bool covered=false;
            for(unsigned a=0;a<world().actors;++a){const auto& b=world().bodies[a];const auto station=stations_[a][0];
                if(f::distance(station,world().camps[n].pose)<=int(b.attack_range))covered=true;
                if(b.kit==f::Kit::Varg&&request_.varg_splash_known){
                    for(unsigned p=0;p<world().npcs;++p)if(world().camps[p].hp>0&&f::distance(station,world().camps[p].pose)<=int(b.attack_range)&&
                        f::distance(world().camps[p].pose,world().camps[n].pose)<=int(request_.varg_splash_radius))covered=true;}}
            if(!covered)return stop(Reason::NeedApproach);}
        return true;
    }
    bool finish_leg(){
        const auto a=actor();auto& program=*program_;const auto destination=detail::point(*map_,trace_[forward_cache_?trace_count_-1:0]);
        if(!detail::goal(*map_,world(),query_,destination))return stop(Reason::Cache);
        stations_[a][leg_]=destination;auto& proof=out_.legs[a][leg_];proof.origin=query_.origin;proof.station=destination;
        proof.traversal=query_.traversal;proof.requires_selected_full_clear=query_.traversal==Traversal::Cleared;
        proof.field=detail::key(*map_,world(),query_,request_.source.candidate);proof.occupancy_revision=map_->occupancy_revision;
        const auto count=trace_count_-1;const auto id=program.leg_count++;program.bodies[a].legs[leg_]=int(id);
        auto& leg=program.legs[id];leg.origin=query_.origin;leg.begin=program.route_count;leg.count=count;leg.complete=false;
        if(count&&map_->occupancy_revision&&map_->occupancy_round==request_.source.round&&map_->occupied.size()==detail::cells(*map_)){
            const auto first=trace_[forward_cache_?1:trace_count_-2];proof.first_edge=map_->occupied[first]?Occupancy::Occupied:Occupancy::Open;}
        if(leg_==3){
            bool found=false;
            for(const auto& altar:map_->altars){
                if(f::distance(destination,altar.point)!=1||detail::lock(*map_,altar,world().round)==Lock::Locked)continue;
                program.bodies[a].altar=altar.point;program.bodies[a].altar_known=true;
                out_.locks[a]=detail::lock(*map_,altar,world().round);found=true;break;
            }
            if(!found)return stop(Reason::Unreachable);
        }
        publish_=0;publishing_=true;tracing_=false;return true;
    }
    bool unit(){
        if(publishing_){auto& leg=program_->legs[program_->leg_count-1];
            if(publish_<leg.count){if(program_->route_count==f::route_limit)return stop(Reason::Capacity);
                const auto cell=trace_[forward_cache_?publish_+1:trace_count_-2-publish_];++publish_;
                program_->route[program_->route_count++]={detail::point(*map_,cell),request_.source.round,world().horizon};return true;}
            leg.complete=true;publishing_=false;
            if(++leg_==6){leg_=0;if(++planning_==world().actors){if(!coverage())return false;out_.state=State::Ready;out_.program=program_;return true;}}
            begin_leg();return out_.state!=State::Unknown;}
        if(tracing_){
            if(field_&&(!field_->ready()||field_->key()!=field_key_||field_->serial()!=field_serial_))return stop(Reason::Cache);
            if(trace_count_==trace_.size())return stop(Reason::Capacity);trace_[trace_count_++]=cursor_;
            const auto p=detail::point(*map_,cursor_);
            if(forward_cache_){if(field_->distance(p)==0)return finish_leg();const auto next=field_->next(p);
                if(!detail::inside(*map_,next)||f::distance(next,p)!=1)return stop(Reason::Cache);cursor_=std::uint16_t(detail::index(*map_,next));}
            else {if(p==query_.origin)return finish_leg();const auto next=search_.parent(cursor_);if(next==cursor_||next==none)return stop(Reason::Workspace);cursor_=next;}
            return true;}
        if(!search_.unit())return stop(Reason::Workspace);
        if(search_.unknown())return stop(Reason::Unreachable);
        if(search_.ready()){if(!search_.found())return stop(leg_==0?Reason::Station:Reason::Unreachable);cursor_=search_.destination();tracing_=true;}
        return true;
    }
public:
    Builder()=default;Builder(const Builder&)=delete;Builder& operator=(const Builder&)=delete;
    const Product& result()const{return out_;}
    State begin(Map&&,Request,f::Program&,Workspace&,std::span<const ReverseField* const> = {})=delete;
    State begin(const Map& map,Request request,f::Program& output,Workspace& workspace,std::span<const ReverseField* const> cache={}){
        map_=&map;request_=request;program_=&output;workspace_=&workspace;cache_=cache;out_=Product{};out_.source=request.source;
        const auto size=detail::cells(map);
        if(!request.world||!size||!map.static_complete||map.blocked.size()!=size||map.own_spawn.size()!=size||
           map.foreign_positive.size()!=size||!map.foreign_complete||!map.revision||!map.roots_revision||!map.foreign_revision||
           map.altars.empty()||map.altars.size()>altar_limit||cache.size()>cache_limit)return stop(Reason::Shape),out_.state;
        const auto& w=world();
        if(w.actors<1||w.actors>f::actor_limit||w.npcs<1||w.npcs>f::npc_limit||w.externals>f::external_limit||w.round<0||w.horizon<w.round||
           !source(request.source)||!request.source.candidate||!request.source.input||
           !w.group.roster_complete||w.group.life!=f::Life::Alive)return stop(Reason::Source),out_.state;
        if((request.family!=Family::Solo&&request.family!=Family::Joint)||(request.family==Family::Solo&&w.actors!=1)||
           (request.family==Family::Joint&&(w.actors<2||w.actors>3)))return stop(Reason::Family),out_.state;
        const auto reason=order(w,request.order,out_.order);if(reason!=Reason::None)return stop(reason),out_.state;
        bool varg=false,vendetta=false;unsigned purchase_count=0;
        for(unsigned a=0;a<w.actors;++a){const auto& b=w.bodies[a];const auto& intent=request.intent[a];
            if(!b.actual||!b.alive||!detail::inside(map,b.pose)||b.attack_range>100||b.heal_range>100||b.move_range<1||
               static_cast<unsigned>(b.kit)>static_cast<unsigned>(f::Kit::Vendetta)||intent.work_hints.size()>hint_limit||
               intent.purchases.size()>f::purchase_limit-purchase_count||
               (intent.shield&&b.kit!=f::Kit::Varg)||(intent.heal&&b.kit!=f::Kit::Willow)||(intent.reset&&b.kit==f::Kit::Willow))return stop(Reason::Intent),out_.state;
            for(unsigned j=0;j<a;++j)if(b.id==w.bodies[j].id||b.pose==w.bodies[j].pose||b.kit==w.bodies[j].kit)return stop(Reason::Family),out_.state;
            if(map.foreign_positive[detail::index(map,b.pose)])return stop(Reason::ForeignFootprint),out_.state;
            varg=varg||b.kit==f::Kit::Varg;vendetta=vendetta||b.kit==f::Kit::Vendetta;purchase_count+=unsigned(intent.purchases.size());actor_order_[a]=a;
        }
        if(varg&&vendetta&&!out_.order.varg_before_vendetta)return stop(Reason::VargTieOrder),out_.state;
        for(unsigned n=0;n<w.npcs;++n){const auto& npc=w.camps[n];if(!npc.actual||!detail::inside(map,npc.pose)||!std::isfinite(npc.hp)||npc.hp<0||!std::isfinite(npc.strength)||npc.strength<0||npc.range>100)return stop(Reason::Source),out_.state;}
        for(const auto& altar:map.altars)if(!detail::inside(map,altar.point)||!map.blocked[detail::index(map,altar.point)]||static_cast<unsigned>(altar.lock)>static_cast<unsigned>(Lock::Locked))return stop(Reason::Shape),out_.state;
        std::sort(actor_order_.begin(),actor_order_.begin()+w.actors,[&](unsigned a,unsigned b){
            const auto rank=[](f::Kit kit){return kit==f::Kit::Varg?0:kit==f::Kit::Vendetta?1:2;};return rank(w.bodies[a].kit)<rank(w.bodies[b].kit);});
        output.route_count=output.leg_count=output.purchase_count=0;output.id=request.source.world^request.source.candidate^request.source.input;if(!output.id)output.id=1;
        for(unsigned a=0;a<w.actors;++a){output.bodies[a]=f::BodyPlan{};auto& bp=output.bodies[a];const auto& intent=request.intent[a];
            bp.shield_for_work=intent.shield;bp.heal_for_work=intent.heal;bp.reset_for_work=intent.reset;bp.require_useful_support=intent.require_useful_support;
            bp.post_renewal=f::PostRenewal::Unknown;bp.purchase_begin=output.purchase_count;bp.purchase_count=unsigned(intent.purchases.size());
            for(const auto& p:intent.purchases){if(!p.count||!p.quote_revision||!std::isfinite(p.quoted_cost)||p.quoted_cost<=0||p.item>=f::item_limit)return stop(Reason::Intent),out_.state;output.purchases[output.purchase_count++]=p;}}
        out_.geometry=geometry_identity(request);planning_=leg_=0;out_.state=State::Building;begin_leg();return out_.state;
    }
    State advance(Source current,unsigned units,Work work={}){
        out_.last_work=0;if(!map_||!request_.world)return State::Unknown;
        if(!source(current))return stop(Reason::Stale),out_.state;
        if(out_.state!=State::Building&&out_.state!=State::Deferred)return out_.state;
        out_.state=State::Building;
        while(out_.last_work<units&&out_.state==State::Building){if(!work.step()){out_.state=State::Deferred;break;}
            ++out_.last_work;++out_.work;if(!unit())break;}
        return out_.state;
    }
};
inline bool same_geometry(const GeometryIdentity& a,const GeometryIdentity& b){
    if(a.actors!=b.actors||a.npcs!=b.npcs||a.team!=b.team||a.splash_known!=b.splash_known||a.splash!=b.splash)return false;
    for(unsigned i=0;i<a.actors;++i){const auto& x=a.bodies[i];const auto& y=b.bodies[i];
        if(x.id!=y.id||x.kit!=y.kit||!(x.origin==y.origin)||x.attack!=y.attack||x.heal!=y.heal||x.move!=y.move)return false;}
    for(unsigned i=0;i<a.npcs;++i){const auto& x=a.camps[i];const auto& y=b.camps[i];
        if(x.id!=y.id||!(x.pose==y.pose)||x.alive!=y.alive||x.strength!=y.strength||x.range!=y.range)return false;}
    return true;
}
inline bool same_orders(const Order& a,const Order& b){
    if(a.count!=b.count||a.actor!=b.actor)return false;
    for(unsigned i=0;i<a.count;++i)for(unsigned parity=0;parity<2;++parity){const auto& x=(parity?a.odd:a.even)[i];const auto& y=(parity?b.odd:b.even)[i];
        if(x.controlled!=y.controlled||x.actor!=y.actor||x.external_id!=y.external_id)return false;}
    return true;
}
// Reuse only completed GEOMETRY, into a distinct legitimately constructed
// Program. A NEW immutable actual World is required: no old simulation score,
// event, HP/CD/cash, lease, preparation latency or authority is republished.
// Parent must run a new Simulator and refresh native predicates before acting.
class Rebinder {
    const Product* original_{};const Map* map_{};Request request_{};f::Program* output_{};Product out_;
    const f::Program* original_program_{};Source original_source_{};std::uint64_t original_id_{};unsigned original_routes_{},original_legs_{};
    unsigned route_{},leg_{},actor_{},purchase_{};unsigned phase_{};
    bool stop(Reason reason){out_.state=State::Unknown;out_.reason=reason;out_.program=nullptr;return false;}
    bool unit(){
        const auto& old=*original_->program;auto& fresh=*output_;const auto& w=*request_.world;
        if(phase_==0){if(route_<old.route_count){const auto& step=old.route[route_++];fresh.route[fresh.route_count++]={step.destination,request_.source.round,w.horizon};return true;}phase_=1;return true;}
        if(phase_==1){if(leg_<old.leg_count){fresh.legs[fresh.leg_count++]=old.legs[leg_++];return true;}phase_=2;return true;}
        if(actor_==w.actors){out_.state=State::Ready;out_.program=output_;return true;}
        auto& bp=fresh.bodies[actor_];const auto& intent=request_.intent[actor_];
        if(phase_==2){
            bp=f::BodyPlan{};bp.legs=old.bodies[actor_].legs;bp.altar=old.bodies[actor_].altar;bp.altar_known=old.bodies[actor_].altar_known;
            bp.shield_for_work=intent.shield;bp.heal_for_work=intent.heal;bp.reset_for_work=intent.reset;
            bp.require_useful_support=intent.require_useful_support;bp.post_renewal=f::PostRenewal::Unknown;
            bp.purchase_begin=fresh.purchase_count;bp.purchase_count=unsigned(intent.purchases.size());purchase_=0;
            bool altar_found=false;for(const auto& altar:map_->altars)if(altar.point==bp.altar){out_.locks[actor_]=detail::lock(*map_,altar,w.round);altar_found=true;break;}
            if(!altar_found)return stop(Reason::Stale);
            for(unsigned k=0;k<6;++k){auto& proof=out_.legs[actor_][k];proof.first_edge=Occupancy::Unknown;proof.occupancy_revision=map_->occupancy_revision;
                const auto id=bp.legs[k];if(id<0||unsigned(id)>=fresh.leg_count)return stop(Reason::Shape);const auto& path=fresh.legs[unsigned(id)];
                if(path.count&&map_->occupancy_revision&&map_->occupancy_round==w.round&&map_->occupied.size()==detail::cells(*map_)){
                    const auto cell=detail::index(*map_,fresh.route[path.begin].destination);proof.first_edge=map_->occupied[cell]?Occupancy::Occupied:Occupancy::Open;}}
            phase_=3;return true;
        }
        if(purchase_<intent.purchases.size()){
            const auto p=intent.purchases[purchase_++];
            if(fresh.purchase_count==f::purchase_limit||!p.quote_revision||!p.count||!std::isfinite(p.quoted_cost)||p.quoted_cost<=0||p.item>=f::item_limit)return stop(Reason::Intent);
            fresh.purchases[fresh.purchase_count++]=p;return true;
        }
        ++actor_;phase_=2;return true;
    }
public:
    const Product& result()const{return out_;}
    State begin(Product&&,const Map&,Request,f::Program&)=delete;
    State begin(const Product&,Map&&,Request,f::Program&)=delete;
    State begin(const Product& original,const Map& map,Request request,f::Program& output){
        original_=&original;map_=&map;request_=request;output_=&output;out_=Product{};out_.source=request.source;
        const auto cells=detail::cells(map);
        if(original.state!=State::Ready||!original.program||original.program==&output||!cells||
           map.blocked.size()!=cells||map.own_spawn.size()!=cells||map.foreign_positive.size()!=cells||
           !map.static_complete||!map.foreign_complete||map.altars.empty()||map.altars.size()>altar_limit||
           !source_matches(map,request,request.source)||!request.source.candidate||!request.source.input||
           original.source.map!=request.source.map||original.source.roots!=request.source.roots||
           original.source.foreign!=request.source.foreign||original.source.candidate!=request.source.candidate)
            return stop(Reason::Stale),out_.state;
        if(!same_geometry(original.geometry,geometry_identity(request)))return stop(Reason::Stale),out_.state;
        if((request.family!=Family::Solo&&request.family!=Family::Joint)||
           (request.family==Family::Solo&&request.world->actors!=1)||
           (request.family==Family::Joint&&request.world->actors<2))return stop(Reason::Family),out_.state;
        const auto reason=order(*request.world,request.order,out_.order);if(reason!=Reason::None)return stop(reason),out_.state;
        if(!same_orders(original.order,out_.order))return stop(Reason::OrderChanges),out_.state;
        const auto& old=*original.program;
        if(old.route_count>f::route_limit||old.leg_count>f::leg_limit)return stop(Reason::Shape),out_.state;
        for(unsigned n=0;n<old.leg_count;++n)if(!old.legs[n].complete||old.legs[n].begin>old.route_count||old.legs[n].count>old.route_count-old.legs[n].begin)return stop(Reason::Shape),out_.state;
        for(unsigned n=0;n<old.route_count;++n)if(!detail::inside(map,old.route[n].destination))return stop(Reason::Shape),out_.state;
        unsigned purchases=0;for(unsigned a=0;a<request.world->actors;++a){const auto& i=request.intent[a];const auto kit=request.world->bodies[a].kit;
            if(i.purchases.size()>f::purchase_limit-purchases||(i.shield&&kit!=f::Kit::Varg)||(i.heal&&kit!=f::Kit::Willow)||(i.reset&&kit==f::Kit::Willow))return stop(Reason::Intent),out_.state;
            purchases+=unsigned(i.purchases.size());}
        // Only metadata/small fixed proof copies here. Route/leg/purchase copy
        // is explicitly staged below; begin/allocations still need own admission.
        out_.legs=original.legs;out_.geometry=original.geometry;
        original_program_=original.program;original_source_=original.source;original_id_=old.id;original_routes_=old.route_count;original_legs_=old.leg_count;
        output.route_count=output.leg_count=output.purchase_count=0;
        output.id=request.source.world^request.source.candidate^request.source.input;if(!output.id)output.id=1;
        route_=leg_=actor_=purchase_=phase_=0;out_.state=State::Building;return out_.state;
    }
    State advance(Source current,unsigned units,Work work={}){
        out_.last_work=0;if(!map_||!source_matches(*map_,request_,current))return stop(Reason::Stale),out_.state;
        if(!original_||original_->state!=State::Ready||original_->source!=original_source_||original_->program!=original_program_||
           !original_program_||original_program_->id!=original_id_||original_program_->route_count!=original_routes_||original_program_->leg_count!=original_legs_)
            return stop(Reason::Stale),out_.state;
        if(out_.state!=State::Building&&out_.state!=State::Deferred)return out_.state;out_.state=State::Building;
        while(out_.last_work<units&&out_.state==State::Building){if(!work.step()){out_.state=State::Deferred;break;}++out_.last_work;++out_.work;if(!unit())break;}
        return out_.state;
    }
};
} // namespace wb::capital::service_routes
