// Conditional SDK-double composition fixture, NOT native physics/refinement,
// engine fuel health, autonomous route quality, or competitive evidence.
// Premises: only g2 exists; cluster strength0/range3, full private vision radius4,
// a free31x31 static floor apart from living members and the blocked altar,
// no other bodies,
// contest, damage, locks or interruption, g2 receipt28gems/100gold, capture arm
// then5ticks, tribute ceil(bag/10) ticks, recall5ticks to a free supplied home.
// Host state is an independent model. Evaluator predictions NEVER become facts.
#define main shared_service_completion_transition_main
#include "transition.cpp"
#undef main
#include "service_task.h"
#include "proposal_lifecycle.h"
#include "selected_conversion.h"
#include <queue>
namespace cambc::detail {
extern "C" std::int32_t sdk_camp(std::uint32_t,camp_state* out){++sdk_calls;*out={};return 0;}
extern "C" std::int32_t sdk_camp_design(std::int32_t,camp_design* out){++sdk_calls;*out={};return 0;}
}

namespace completion {
using namespace mosaic;
using Pos=cambc::vec2;
constexpr std::array<int,9> handles{36,40,38,32,6,14,34,12,4};
constexpr std::array<std::size_t,9> members{0,1,2,3,4,5,6,7,8};
constexpr std::array<Pos,8> delivery{{{10,16},{9,15},{8,14},{7,13},{6,12},{5,12},{4,13},{3,14}}};
// Saved Packs geometry: landing3,14 is adjacent to blocked altar2,15;
// home1,1 lies inside A's supplied free spawn region[0,3)x[0,3).
constexpr Pos altar{2,15},home{1,1};
constexpr int group_id=2,primitive_bound=96;
int distance(Pos a,Pos b){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
service_task::Point point(Pos p){return {p.x,p.y};}
struct Body {Pos spawn;int hp{120};};
struct Host {
    std::array<Body,9> bodies{};
    frame::Self self=::packet().self;
    int epoch{1};std::uint64_t revision{},nonce{},serial{};
    int primitives{},basics{},moves{},waits{},damage{},payouts{},banks{},submitted{};
    bool capture_armed{},paid{};int capture_ticks{},respawn_remaining{},channel_remaining{};
    int tribute_begin{-1},bank_epoch{-1},recall_begin{-1},arrival_epoch{-1};
    conversion::History history{epoch,0};
    Host(){
        for(std::size_t i=0;i<bodies.size();++i)bodies[i].spawn={10+int(i%3),16+int(i/3)};
        self.pos={9,15};self.hp=self.max_hp=1000;self.splash_radius=1;self.range=self.move_speed=1;
        self.strength=self.basic_damage=40;self.basic_damage_known=self.damage_modifiers_known=true;
    }
    selected::Stamp stamp()const{return {epoch,0,revision};}
    bool dead()const{return std::all_of(bodies.begin(),bodies.end(),[](const auto& b){return b.hp==0;});}
    bool visible(Pos p)const{return distance(self.pos,p)<=4;}
    bool on_spawn()const{return std::any_of(bodies.begin(),bodies.end(),[&](const auto& b){return b.spawn==self.pos;});}
    bool free(Pos p)const{
        if(p.x<0||p.y<0||p.x>=31||p.y>=31||p==altar)return false;
        return std::none_of(bodies.begin(),bodies.end(),[&](const auto& b){return b.hp>0&&b.spawn==p;});
    }
    void changed(){assert(++primitives<=primitive_bound);++revision;}
    void apply(const transition::Command& command){
        assert(command.expected_epoch==epoch&&command.expected_actor==0&&command.expected_revision==revision);
        if(command.kind==transition::Kind::basic){
            assert(self.alive&&self.can_attack&&!self.stunned&&!self.recalling&&!self.submitting);
            const auto it=std::find(handles.begin(),handles.end(),command.target_id);assert(it!=handles.end());
            const auto target=std::size_t(it-handles.begin());
            assert(bodies[target].hp>0&&visible(bodies[target].spawn)&&distance(self.pos,bodies[target].spawn)<=1);
            // Independent host formula; each member is hit once, including primary.
            const auto center=bodies[target].spawn;
            for(auto& body:bodies)if(body.hp>0&&distance(center,body.spawn)<=1){
                const int removed=std::min(body.hp,40);body.hp-=removed;damage+=removed;
            }
            self.can_attack=false;self.attacked=true;++basics;
        }else if(command.kind==transition::Kind::move){
            assert(self.alive&&self.can_move&&!self.stunned&&!self.recalling&&!self.submitting);
            assert(command.destination!=self.pos&&distance(self.pos,command.destination)<=1&&free(command.destination));
            self.pos=command.destination;self.can_move=false;self.moved=true;++moves;
        }else{assert(command.kind==transition::Kind::wait);++waits;}
        changed();
    }
    void apply(const conversion::Request& request){
        assert(request.stamp.matches(stamp())&&self.alive&&!self.stunned&&!self.submitting&&!self.recalling);
        if(request.kind==conversion::Kind::tribute_start){
            assert(self.gems>0&&self.pos==delivery.back()&&request.altar==altar&&distance(self.pos,altar)<=1&&!free(altar));
            self.submitting=true;channel_remaining=(self.gems+9)/10;tribute_begin=epoch;
        }else{
            assert(request.kind==conversion::Kind::recall_start&&self.gems==0);
            self.recalling=true;channel_remaining=5;recall_begin=epoch;
        }
        self.can_move=self.can_attack=self.can_cast=false;
        const auto advanced=conversion::advanced(history,request);assert(advanced);history=*advanced;
        changed();
    }
    void owner_end_and_next_entry(){
        // Independent model phases. No predicted receipt is installed by a certificate.
        if(paid){assert(respawn_remaining>0);--respawn_remaining;}
        if(dead()&&!paid){
            if(!on_spawn()){capture_armed=false;capture_ticks=0;}
            else if(!capture_armed){capture_armed=true;capture_ticks=0;}
            else if(++capture_ticks==5){paid=true;++payouts;self.gems+=28;self.gold+=100;respawn_remaining=50;}
        }
        bool completed=false;
        if(channel_remaining>0)completed=--channel_remaining==0;
        ++epoch;revision=0;history={epoch,0};
        if(completed&&self.submitting){
            submitted+=self.gems;self.gems=0;self.submitting=false;++banks;bank_epoch=epoch;
        }else if(completed&&self.recalling){self.recalling=false;self.pos=home;arrival_epoch=epoch;}
        self.can_move=self.can_attack=self.can_cast=!(self.submitting||self.recalling);
        self.moved=self.attacked=self.cast_spent=false;
        assert(epoch<64); // A separate temporal bound, not a runtime fuel claim.
    }
};
struct Fixture {
    Host host;
    spatial::Index index;
    std::vector<frame::SourceCamp> catalog;
    proposal::Lifecycle lifecycle;
    std::array<int,3> respawn{0,0,50};
    int selected_consumed{},conversion_consumed{},captures{},checks{};
    Fixture(){
        std::vector<spatial::Entry> entries;
        for(std::size_t i=0;i<host.bodies.size();++i){
            frame::Camp c;c.handle=handles[i];c.group=group_id;c.member=int(i);c.pos=c.spawn=host.bodies[i].spawn;
            c.hp=c.design_hp=120;c.range=3;c.design_strength=c.strength=0;
            auto h=cambc::detail::handle_access::make_camp(static_cast<cambc::handle_camp_t>(c.handle),
                static_cast<cambc::handle_camp_group_t>(group_id),i%2?cambc::camp_type::gem_cluster:cambc::camp_type::gold_cluster,c.spawn);
            catalog.push_back({h,c});entries.push_back({c.handle,c.spawn.x,c.spawn.y,c.range});
        }
        assert(index.build(entries,true));assert(lifecycle.bind(catalog,index.borrow(),3));
    }
    selected::Packet capture(transition::Command command){
        selected::Packet p;++captures;
        p.stamp=host.stamp();p.origin_stamp=p.stamp;p.nonce=++host.nonce;
        command.expected_epoch=host.epoch;command.expected_actor=0;command.expected_revision=host.revision;
        p.request={command,++host.serial};p.layout=index.borrow();p.catalog=catalog;p.self=host.self;
        p.complete=p.self_complete=p.enemies_complete=p.origin_complete=true;
        p.move_test_complete=true;p.move_legal=host.free(command.destination)&&distance(host.self.pos,command.destination)<=1;
        p.effect_champions_clear=true;
        frame::Group g;g.id=group_id;g.gems=28;g.gold=100;g.members.assign(members.begin(),members.end());
        const bool sees_group=std::any_of(host.bodies.begin(),host.bodies.end(),[&](const auto& b){return host.visible(b.spawn);});
        if(sees_group){
            g.observed_round=host.epoch;g.state=host.paid?frame::GroupState::respawning:(host.dead()?frame::GroupState::reward:frame::GroupState::alive);
            g.cooldown=host.paid?host.respawn_remaining:0;
        }
        p.groups.push_back(g);
        for(std::size_t i=0;i<catalog.size();++i){
            auto c=catalog[i].design;const auto& actual=host.bodies[i];c.observed_round=host.epoch;
            c.visible=c.known=host.visible(actual.spawn);c.alive=c.known&&actual.hp>0;
            c.hp=c.known?float(actual.hp):0;c.pos=c.alive?actual.spawn:Pos{-1,-1};
            c.possible_alive=!c.known||c.alive;c.buffs_known=c.alive;c.awake=c.alive&&host.basics>0;
            p.rows.push_back({i,c,p.stamp,true,true,true,c.known,c.alive,c.alive});
        }
        if(g.observed_round==host.epoch)assert(lifecycle.observe_group(g,p.stamp,p.stamp));
        assert(lifecycle.observe(p,p.stamp,respawn));
        return p;
    }
    selected::Packet fresh_wait(){transition::Command c;c.kind=transition::Kind::wait;return capture(c);}
    service_task::Member read(std::size_t i)const{
        // Dated work estimate deliberately stays120; only the real negative reader retires it.
        return {point(catalog[i].design.spawn),catalog[i].design.hp,lifecycle.may_propose(i,host.epoch)};
    }
    void primitive(transition::Command command){
        auto p=capture(command);auto assessed=selected::assess(p,p.request);
        if(!assessed.certificate){std::printf("completion missing cert kind%d epoch%d pos%d,%d status%d flags%llu origin%d/%d\n",
            int(command.kind),host.epoch,host.self.pos.x,host.self.pos.y,int(assessed.result.status),
            static_cast<unsigned long long>(assessed.result.action.flags),int(assessed.result.origin_wait.complete),int(assessed.result.origin_wait.safe));}
        assert(assessed.result.action.eligible&&assessed.certificate);
        auto consumed=assessed.certificate->consume(host.stamp(),p.request,p.layout,host.self.pos,p.nonce);assert(consumed);
        assert(!assessed.certificate->consume(host.stamp(),p.request,p.layout,host.self.pos,p.nonce));
        const auto hp_before=host.bodies;host.apply(*consumed);++selected_consumed;
        for(const auto& effect:assessed.result.action.camps){
            const auto it=std::find(handles.begin(),handles.end(),effect.handle);assert(it!=handles.end());
            const auto i=std::size_t(it-handles.begin());assert(float(hp_before[i].hp)==effect.before_hp&&float(host.bodies[i].hp)==effect.after_hp);
        }
        // Bidirectional oracle: an omitted collateral effect must fail even if
        // the independent Host and the following recapture know that body died.
        for(std::size_t i=0;i<host.bodies.size();++i){
            const auto count=std::count_if(assessed.result.action.camps.begin(),assessed.result.action.camps.end(),
                [&](const auto& effect){return effect.handle==handles[i];});
            assert(count==(hp_before[i].hp!=host.bodies[i].hp?1:0));
        }
        const auto& predicted=assessed.result.action.after_self;
        assert(predicted.pos==host.self.pos&&predicted.can_move==host.self.can_move&&predicted.can_attack==host.self.can_attack&&
            predicted.can_cast==host.self.can_cast&&predicted.moved==host.self.moved&&predicted.attacked==host.self.attacked);
        auto observed=fresh_wait();assert(observed.stamp.revision==p.stamp.revision+1&&observed.nonce!=p.nonce);
        assert(!p.matches(host.stamp(),p.request)); // Previous observation has no current authority.
        ++checks;
    }
    void tick(){transition::Command c;c.kind=transition::Kind::wait;primitive(c);host.owner_end_and_next_entry();}
    void basic(std::size_t i){transition::Command c;c.kind=transition::Kind::basic;c.target_kind=transition::TargetKind::camp;c.target_id=handles[i];c.target_index=int(i);primitive(c);}
    void move(Pos p){transition::Command c;c.kind=transition::Kind::move;c.destination=p;primitive(c);}
    Pos next_frontier_step(){
        // Finite BFS supplies only a path proposal. Host free() is the stipulated
        // floor/occupancy oracle, and each step still needs its own fresh cert.
        std::array<int,31*31> previous;previous.fill(-1);std::queue<Pos> q;
        const auto origin=host.self.pos;const int start=origin.y*31+origin.x;previous[start]=start;q.push(origin);
        int goal=-1,visited=0;
        while(!q.empty()){
            const auto p=q.front();q.pop();assert(++visited<=31*31);
            if(service_task::in_frontier(members,point(p),1,[&](std::size_t i){return read(i);})){goal=p.y*31+p.x;break;}
            // Forward diagonals first; all eight neighbors are tested finitely.
            for(const auto d:std::array<Pos,8>{{{1,1},{1,0},{0,1},{-1,1},{1,-1},{-1,0},{0,-1},{-1,-1}}}){
                const Pos n{p.x+d.x,p.y+d.y};if(!host.free(n))continue;const int key=n.y*31+n.x;
                if(previous[key]>=0)continue;previous[key]=p.y*31+p.x;q.push(n);
            }
        }
        assert(goal>=0&&goal!=start);
        while(previous[goal]!=start)goal=previous[goal];
        return {goal%31,goal/31};
    }
    conversion::Result convert(conversion::Kind kind){
        auto p=fresh_wait();auto basis=selected::assess(p,p.request);assert(basis.certificate&&basis.result.action.eligible);
        assert(basis.certificate->consume(host.stamp(),p.request,p.layout,host.self.pos,p.nonce));++selected_consumed;
        conversion::Request r;r.kind=kind;r.stamp=host.stamp();r.serial=++host.serial;r.altar=altar;
        conversion::Facts f;f.request=r;f.stamp=host.stamp();f.packet_nonce=p.nonce;f.layout=p.layout;f.origin=host.self.pos;
        f.complete=f.history_complete=f.bag_complete=f.channel_complete=f.settings_complete=true;
        f.altar_complete=f.lock_complete=f.altar_valid=true;f.altar_visible=host.visible(altar);f.history=host.history;
        f.channel=conversion::observed_channel(host.self);f.remaining=host.channel_remaining;f.gems=host.self.gems;
        f.recall_duration=5;f.submit_range=1;f.submit_min=1;f.submit_multiplier=0.1;f.altar=altar;
        auto assessed=conversion::assess(p,f,r);assert(assessed.certificate&&assessed.result.eligible&&assessed.result.completion_is_conditional);
        auto consumed=assessed.certificate->consume(host.stamp(),r,p.request,p.layout,host.self.pos,p.nonce);assert(consumed);
        assert(!assessed.certificate->consume(host.stamp(),r,p.request,p.layout,host.self.pos,p.nonce));
        host.apply(*consumed);++conversion_consumed;++checks;return assessed.result;
    }
};
} // namespace completion

int main(){
    using namespace mosaic;using namespace completion;
    const int initialized=sdk_calls;Fixture f;
    for(int n=0;n<3;++n){
        f.fresh_wait();const auto i=service_task::nominate_basic(members,point(f.host.self.pos),1,40,[&](std::size_t j){return f.read(j);});
        assert(i&&*i==0);f.basic(*i);if(n<2)f.tick();
    }
    assert(f.host.damage==480&&f.host.basics==3&&f.host.self.pos==Pos(9,15));
    for(auto i:{0u,1u,3u,4u})assert(f.host.bodies[i].hp==0&&!f.read(i).proposed&&f.catalog[i].design.hp==120);
    for(auto i:{2u,5u,6u,7u,8u})assert(f.host.bodies[i].hp==120&&f.read(i).proposed);
    auto unfiltered=[&](std::size_t i){auto m=f.read(i);m.proposed=true;return m;};
    assert(service_task::in_frontier(members,{9,15},1,unfiltered));
    assert(service_task::nominate_basic(members,{9,15},1,40,unfiltered)==std::optional<std::size_t>{0});
    assert(!service_task::in_frontier(members,{9,15},1,[&](std::size_t i){return f.read(i);}));
    assert(!service_task::nominate_basic(members,{9,15},1,40,[&](std::size_t i){return f.read(i);}));
    // A valid Wait remains safe, but it is not completion. Next owner must advance.
    f.tick();const auto postkill_moves=f.host.moves;
    for(int iterations=0;!f.host.dead();++iterations){
        assert(iterations<32);f.fresh_wait();
        const auto candidate=service_task::nominate_basic(members,point(f.host.self.pos),1,40,[&](std::size_t i){return f.read(i);});
        if(candidate)f.basic(*candidate);else f.move(f.next_frontier_step());
        if(!f.host.dead())f.tick();
    }
    assert(f.host.moves>postkill_moves&&f.host.damage==9*120&&f.host.basics==12);
    f.fresh_wait();int reward_goals=0,work_goals=0;
    for(auto i:members){reward_goals+=service_task::goal_member(i,true,[&](std::size_t j){return f.read(j);});
        work_goals+=service_task::goal_member(i,false,[&](std::size_t j){return f.read(j);});}
    assert(reward_goals==9&&work_goals==0); // Filtered work never erases capture goals.
    assert(f.host.self.pos==Pos(11,17)&&f.host.self.can_move);f.move(delivery.front());
    assert(f.host.self.gems==0&&!f.host.capture_armed);f.tick();
    assert(f.host.capture_armed&&f.host.capture_ticks==0&&f.host.self.gems==0);
    for(int tick=1;tick<=5;++tick){f.tick();assert(f.host.self.pos==delivery.front());
        assert(f.host.capture_ticks==tick&&f.host.self.gems==(tick==5?28:0));}
    assert(f.host.payouts==1&&f.host.self.gold==100&&f.host.submitted==0);
    const int before_delivery_moves=f.host.moves;
    for(std::size_t i=1;i<delivery.size();++i){f.move(delivery[i]);f.tick();assert(f.host.self.gems==28&&f.host.payouts==1&&f.host.submitted==0);}
    assert(f.host.moves-before_delivery_moves==7);
    assert(f.host.self.pos==delivery.back());const auto tribute=f.convert(conversion::Kind::tribute_start);
    assert(tribute.duration==3&&tribute.starting_bag==28&&f.host.self.submitting&&f.host.submitted==0);
    for(int tick=1;tick<=3;++tick){f.tick();assert(f.host.submitted==(tick==3?28:0)&&f.host.self.gems==(tick==3?0:28));}
    assert(f.host.banks==1&&f.host.bank_epoch==tribute.expected_completion_epoch&&!f.host.self.submitting&&f.host.self.alive);
    const auto recall=f.convert(conversion::Kind::recall_start);assert(recall.duration==5&&f.host.self.recalling);
    for(int tick=1;tick<=5;++tick){f.tick();assert(f.host.self.recalling==(tick!=5));}
    const bool return_ready=f.host.self.alive&&!f.host.self.submitting&&!f.host.self.recalling&&f.host.self.pos==home&&
        f.host.self.hp==f.host.self.max_hp&&f.host.self.gems==0&&f.host.submitted==28;
    assert(return_ready&&f.host.arrival_epoch==recall.expected_completion_epoch&&f.host.payouts==1&&f.host.banks==1);
    assert(f.host.primitives<primitive_bound&&f.host.moves==before_delivery_moves+7&&f.conversion_consumed==2);
    assert(sdk_calls==initialized);
    std::printf("conditional service completion: %d fresh command checks, %d model primitives, %d basics, %d moves; full clear1080/capture28/bank28/return-ready; SDK calls afterstartup0\n",
        f.checks,f.host.primitives,f.host.basics,f.host.moves);
}
