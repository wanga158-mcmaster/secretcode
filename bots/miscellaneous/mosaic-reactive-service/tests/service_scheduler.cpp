// Conditional SDK-double scheduler/control fixture, NOT proof of production
// caller placement, native dynamics, runtime fuel costs or competitive strength.
// Reuse the independent nine-member Host/fact collector and explicit free-floor,
// vision, zero-pressure, capture/timer premises from service_completion.cpp.
// Environment owner advances are modeled clock phases, not hidden acquisitions.
// A denied admission reserve is injected at attempt(), NEVER by falsifying a
// Packet.complete flag or pretending that the actual owner clock crossed.
// Scaffolding copied verbatim from service_completion.cpp@c4423a5c; its
// independent original main is excluded. No existing test source is edited.
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

#include "service_continuation.h"
#include <string>

namespace scheduling {
using namespace mosaic;
using completion::Pos;
enum Mode : std::uint32_t {work=1,delivery=2};
struct Counts {int requests{},captures{},wait_captures{},acquisitions{},nominees{},attempt_callbacks{},physical_attempts{},applied{},refused{};};
struct Scheduler;
struct Ops {
    completion::Fixture facts;
    continuation::Context ctx;
    continuation::Pending* pending{};
    selected::Packet captured;
    std::optional<selected::Assessment> decision;
    std::optional<transition::Command> cached_nominee;
    Counts counts;
    std::string order;
    bool admitted{true},refuse_next{},captured_had_certificate{};
    int last_bag{},model_receipts{};
    Ops(){ctx.mode=work;ctx.group=completion::group_id;ctx.goal_generation=1;ctx.layout=facts.index.borrow();}
    selected::Stamp now()const{return facts.host.stamp();}
    bool current(selected::Stamp stamp)const{return now().matches(stamp);}
    continuation::Binding binding()const{return continuation::make_binding(0,facts.host.self,ctx);}
    void observe_receipt(){
        // This input's model Host has actually paid; no prediction changes mode.
        if(facts.host.self.gems>last_bag){
            assert(facts.host.paid&&facts.host.payouts==1&&facts.host.self.gems==28);
            ++model_receipts;++ctx.receipt_generation;++ctx.goal_generation;ctx.mode=delivery;ctx.group=-1;
        }
        last_bag=facts.host.self.gems;
    }
    std::optional<transition::Command> nominate(const continuation::Binding&){
        ++counts.nominees;order+='N';return cached_nominee; // Cached raw value ONLY.
    }
    bool valid_target(const transition::Command& command,const continuation::Binding& bound)const{
        // Actual caller classification: a complete current grant with priced
        // positive action/origin volley is not a reserve-only deferral.
        if(decision&&decision->certificate&&captured.matches(now(),captured.request)&&
            selected::same_command(command,captured.request.command)&&
            (decision->result.action.next_volley>0||decision->result.origin_wait.next_volley>0))return false;
        if(command.kind==transition::Kind::move)
            return command.destination!=bound.origin&&completion::distance(bound.origin,command.destination)<=bound.body.move_speed&&
                ((bound.context.mode==work&&bound.bag==0)||(bound.context.mode==delivery&&bound.bag>0));
        if(command.kind!=transition::Kind::basic||command.target_kind!=transition::TargetKind::camp||bound.context.mode!=work)return false;
        const auto it=std::find(completion::handles.begin(),completion::handles.end(),command.target_id);
        return it!=completion::handles.end()&&facts.lifecycle.may_propose(std::size_t(it-completion::handles.begin()),facts.host.epoch);
    }
    continuation::Context context(const selected::Packet&)const{return ctx;}
    const selected::Packet& packet()const{return captured;}
    selected::Request request(transition::Command command){
        ++counts.requests;order+='R';
        // The model collector reserves/increments this serial exactly once when
        // capture occurs; no returned packet envelope or completeness is edited.
        return {command,facts.host.serial+1};
    }
    selected::Assessment& capture(const selected::Request& request){
        ++counts.captures;order+='C';if(request.command.kind==transition::Kind::wait)++counts.wait_captures;
        captured=facts.capture(request.command);assert(selected::same_request(captured.request,request));
        assert(captured.stamp.matches(now())&&captured.complete&&captured.self_complete&&captured.enemies_complete);
        decision.emplace(selected::assess(captured,request));captured_had_certificate=bool(decision->certificate);return *decision;
    }
    continuation::Attempt attempt(selected::Assessment& assessment,const selected::Request& request){
        ++counts.attempt_callbacks;order+='A';assert(pending&&!pending->active()); // Clear BEFORE possible SDK attempt.
        assert(current(captured.stamp)&&assessment.certificate&&assessment.result.action.eligible);
        if(!admitted)return continuation::Attempt::not_attempted; // Complete/current facts remain true.
        auto consumed=assessment.certificate->consume(now(),request,captured.layout,facts.host.self.pos,captured.nonce);assert(consumed);
        assert(!assessment.certificate->consume(now(),request,captured.layout,facts.host.self.pos,captured.nonce));
        ++counts.physical_attempts;
        if(refuse_next){
            refuse_next=false;++counts.refused;
            // Injected Host API refusal tests protocol disposal only. It is not
            // an assertion that these complete facts predict native refusal.
            return continuation::Attempt::refused;
        }
        const auto before=facts.host.bodies;facts.host.apply(*consumed);++counts.applied;
        for(std::size_t i=0;i<before.size();++i){
            const auto count=std::count_if(assessment.result.action.camps.begin(),assessment.result.action.camps.end(),
                [&](const auto& e){return e.handle==completion::handles[i];});
            assert(count==(before[i].hp!=facts.host.bodies[i].hp?1:0));
        }
        for(const auto& e:assessment.result.action.camps){
            const auto it=std::find(completion::handles.begin(),completion::handles.end(),e.handle);assert(it!=completion::handles.end());
            const auto i=std::size_t(it-completion::handles.begin());assert(e.before_hp==before[i].hp&&e.after_hp==facts.host.bodies[i].hp);
        }
        const auto& predicted=assessment.result.action.after_self;const auto& actual=facts.host.self;
        assert(predicted.pos==actual.pos&&predicted.can_move==actual.can_move&&predicted.can_attack==actual.can_attack&&
            predicted.can_cast==actual.can_cast&&predicted.moved==actual.moved&&predicted.attacked==actual.attacked);
        return continuation::Attempt::applied;
    }
    transition::Command acquire_nominee(){
        ++counts.acquisitions;order+='Q';transition::Command command;
        if(ctx.mode==work){
            const auto next=service_task::nominate_basic(completion::members,completion::point(facts.host.self.pos),1,40,
                [&](std::size_t i){return facts.read(i);});
            if(next){command.kind=transition::Kind::basic;command.target_kind=transition::TargetKind::camp;
                command.target_id=completion::handles[*next];command.target_index=int(*next);}
            else{command.kind=transition::Kind::move;command.destination=facts.host.dead()?completion::delivery.front():facts.next_frontier_step();}
        }else{
            assert(ctx.mode==delivery&&facts.host.self.gems==28);command.kind=transition::Kind::move;
            const auto it=std::find(completion::delivery.begin(),completion::delivery.end(),facts.host.self.pos);
            if(it==completion::delivery.end())command.destination=completion::delivery.front();
            else{assert(it+1!=completion::delivery.end());command.destination=*(it+1);}
        }
        cached_nominee=command;return command;
    }
};
struct Entry {continuation::Report first,final;bool acquired{},held_before_capture{};selected::OriginWait held_origin;selected::Stamp stamp;};
struct Scheduler {
    Ops ops;continuation::Pending pending;int entries{},advances{};bool ordinary_capture_admitted{true};
    Scheduler(){ops.pending=&pending;}
    Entry entry(bool ordinary=true){
        assert(++entries<=48);ops.order.clear();ops.observe_receipt();Entry result;result.stamp=ops.now();
        // This is the ACTUAL helper entry seam, before any ordinary Wait/field.
        result.first=continuation::dispatch_first(pending,ops.now(),ops.binding(),ops);result.final=result.first;
        if(result.first.outcome!=continuation::Outcome::replan||!ordinary)return result;
        transition::Command wait;wait.kind=transition::Kind::wait;
        wait.expected_epoch=ops.now().epoch;wait.expected_actor=0;wait.expected_revision=ops.now().revision;
        auto request=ops.request(wait);auto& basis=ops.capture(request);
        assert(basis.result.action.eligible&&basis.result.origin_wait.complete&&basis.result.origin_wait.safe);
        const auto nominee=ops.acquire_nominee();assert(pending.retain(nominee,ops.binding(),ops.now().epoch));result.acquired=true;
        // Publication precedes the caller's ordinary52K capture reserve. This
        // is a caller-phase hold, not a fabricated dispatch result/partial fact.
        if(!ordinary_capture_admitted){result.held_before_capture=true;result.held_origin=basis.result.origin_wait;return result;}
        result.final=continuation::dispatch_first(pending,ops.now(),ops.binding(),ops);return result;
    }
    void advance(const Entry& result){
        if(result.held_before_capture){
            assert(pending.active()&&result.held_origin.complete&&result.held_origin.safe&&result.held_origin.next_volley==0&&ops.current(result.stamp));
        }else if(result.final.outcome==continuation::Outcome::deferred){
            assert(!result.final.attempted&&result.final.origin_wait.complete&&result.final.origin_wait.safe&&
                result.final.origin_wait.next_volley==0&&ops.current(result.stamp));
        }else assert(result.final.outcome==continuation::Outcome::applied);
        // Supplied environment clock: quiet Host clusters have strength0 and no
        // other bodies exist. No old certificate is carried through this phase.
        ops.facts.host.owner_end_and_next_entry();++advances;
    }
};
void direct_once(const Counts& before,const Ops& ops){
    const auto& after=ops.counts;
    assert(after.captures-before.captures==1&&after.requests-before.requests==1&&after.attempt_callbacks-before.attempt_callbacks==1);
    assert(after.physical_attempts-before.physical_attempts==1&&after.applied-before.applied==1);
    assert(after.wait_captures==before.wait_captures&&after.acquisitions==before.acquisitions&&after.nominees==before.nominees);
    assert(ops.order=="RCA");
}
} // namespace scheduling

int main(){
    using namespace mosaic;using namespace scheduling;const int initialized=sdk_calls;int checks=0;
    Scheduler scheduler;auto& ops=scheduler.ops;
    for(int i=0;i<3;++i){auto result=scheduler.entry();assert(result.final.outcome==continuation::Outcome::applied&&
        ops.captured.request.command.kind==transition::Kind::basic&&!scheduler.pending.active()&&!result.final.origin_wait.complete);
        scheduler.advance(result);++checks;}
    assert(ops.facts.host.damage==480&&ops.facts.host.basics==3&&ops.facts.host.self.pos==Pos(9,15));
    ops.admitted=false;const auto before_work=ops.counts;auto deferred=scheduler.entry();
    assert(deferred.final.outcome==continuation::Outcome::deferred&&!deferred.final.attempted&&scheduler.pending.active());
    assert(ops.captured.complete&&ops.decision->certificate&&ops.captured.request.command.kind==transition::Kind::move&&ops.current(deferred.stamp));
    assert(ops.counts.captures-before_work.captures==2&&ops.counts.wait_captures-before_work.wait_captures==1&&
        ops.counts.acquisitions-before_work.acquisitions==1&&ops.counts.physical_attempts==before_work.physical_attempts);
    for(auto i:{0u,1u,3u,4u})assert(!ops.facts.read(i).proposed&&ops.facts.catalog[i].design.hp==120);
    assert(!service_task::in_frontier(completion::members,{9,15},1,[&](std::size_t i){return ops.facts.read(i);}));
    assert(ops.facts.host.self.pos==Pos(9,15));scheduler.advance(deferred);++checks;
    ops.admitted=true;const auto before_retry=ops.counts;auto moved=scheduler.entry();
    assert(moved.first.outcome==continuation::Outcome::applied&&!moved.acquired&&!scheduler.pending.active());
    direct_once(before_retry,ops);assert(ops.facts.host.self.pos==Pos(10,16));scheduler.advance(moved);++checks;
    for(int i=0;!ops.facts.host.dead();++i){
        assert(i<24);auto result=scheduler.entry();assert(result.final.outcome==continuation::Outcome::applied);
        scheduler.advance(result);++checks;
    }
    assert(ops.facts.host.damage==1080&&ops.facts.host.basics==12&&ops.facts.host.self.pos==Pos(11,17)&&!ops.facts.host.paid);
    ops.admitted=false;auto holding=scheduler.entry();assert(holding.final.outcome==continuation::Outcome::deferred&&scheduler.pending.active());
    for(int i=0;!ops.facts.host.paid;++i){
        assert(i<6);scheduler.advance(holding);
        if(!ops.facts.host.paid){holding=scheduler.entry();assert(holding.first.outcome==continuation::Outcome::deferred&&!holding.acquired);}
    }
    assert(ops.facts.host.payouts==1&&ops.facts.host.self.gems==28&&ops.facts.host.self.gold==100&&ops.model_receipts==0&&scheduler.pending.active());
    const auto before_receipt=ops.counts;auto delivery_deferred=scheduler.entry();
    assert(delivery_deferred.first.outcome==continuation::Outcome::replan&&!delivery_deferred.first.captured&&!delivery_deferred.first.attempted);
    assert(delivery_deferred.acquired&&delivery_deferred.final.outcome==continuation::Outcome::deferred&&scheduler.pending.active());
    assert(ops.ctx.mode==delivery&&ops.ctx.receipt_generation==1&&ops.model_receipts==1&&ops.captured.self.gems==28&&ops.captured.complete);
    assert(ops.counts.captures-before_receipt.captures==2&&ops.counts.wait_captures-before_receipt.wait_captures==1&&
        ops.counts.acquisitions-before_receipt.acquisitions==1&&ops.counts.physical_attempts==before_receipt.physical_attempts);
    scheduler.advance(delivery_deferred);ops.admitted=true;const auto before_delivery=ops.counts;auto loaded_move=scheduler.entry();
    assert(loaded_move.first.outcome==continuation::Outcome::applied&&!loaded_move.acquired&&!loaded_move.final.origin_wait.complete&&!scheduler.pending.active());
    direct_once(before_delivery,ops);assert(ops.facts.host.self.gems==28&&ops.facts.host.self.pos==Pos(10,16));checks+=3;
    {
        Scheduler publication;publication.ordinary_capture_admitted=false;
        auto held=publication.entry();auto& p=publication.ops;
        assert(held.acquired&&held.held_before_capture&&held.first.outcome==continuation::Outcome::replan&&publication.pending.active());
        assert(p.counts.captures==1&&p.counts.wait_captures==1&&p.counts.acquisitions==1&&p.counts.attempt_callbacks==0);
        assert(p.captured.complete&&p.captured.request.command.kind==transition::Kind::wait&&p.facts.host.damage==0);
        publication.advance(held);publication.ordinary_capture_admitted=true;const auto before=p.counts;auto result=publication.entry();
        assert(result.first.outcome==continuation::Outcome::applied&&!result.acquired&&!publication.pending.active());
        direct_once(before,p);assert(p.captured.request.command.kind==transition::Kind::basic&&p.facts.host.damage==160);++checks;
    }
    {
        // Separate supplied-world veto: a wounded wolf replaces the front
        // cluster, with actual constant strength20. No owner phase is advanced.
        Scheduler harmful;auto& h=harmful.ops;auto& design=h.facts.catalog[0].design;
        design.strength=design.design_strength=20;design.design_hp=1350;
        h.facts.catalog[0].handle=cambc::detail::handle_access::make_camp(static_cast<cambc::handle_camp_t>(design.handle),
            static_cast<cambc::handle_camp_group_t>(design.group),cambc::camp_type::wolf,design.spawn);
        transition::Command command;command.kind=transition::Kind::move;command.destination={8,15};
        assert(harmful.pending.retain(command,h.binding(),h.now().epoch));const auto result=harmful.entry(false);
        assert(h.captured.complete&&h.captured_had_certificate&&h.decision->result.action.eligible&&h.decision->result.origin_wait.next_volley==20);
        assert(result.first.outcome==continuation::Outcome::replan&&!result.first.attempted&&!harmful.pending.active()&&!h.decision->certificate);
        assert(h.counts.captures==1&&h.counts.attempt_callbacks==0&&h.counts.physical_attempts==0&&h.counts.acquisitions==0);
        assert(h.facts.host.self.pos==Pos(9,15));++checks;
    }
    {
        Scheduler refused;auto& r=refused.ops;transition::Command command;command.kind=transition::Kind::move;command.destination={8,15};
        assert(refused.pending.retain(command,r.binding(),r.now().epoch));r.refuse_next=true;const auto before=r.counts;auto result=refused.entry();
        assert(result.first.outcome==continuation::Outcome::refused&&result.first.attempted&&!result.first.origin_wait.complete&&!result.acquired&&!refused.pending.active());
        assert(r.counts.captures-before.captures==1&&r.counts.physical_attempts-before.physical_attempts==1&&r.counts.refused==1&&r.counts.applied==0);
        assert(r.facts.host.self.pos==Pos(9,15)&&r.facts.host.damage==0);
        const auto after=r.counts;const auto empty=refused.entry(false);assert(empty.first.outcome==continuation::Outcome::replan&&!empty.first.captured&&!empty.first.attempted);
        assert(r.counts.captures==after.captures&&r.counts.attempt_callbacks==after.attempt_callbacks);++checks;
    }
    assert(sdk_calls==initialized);++checks;
    std::printf("conditional scheduler: %d checks; nine-member1080/receipt28, Work+loadedDelivery direct retry ONEcapture/noWait/noacquisition, refusal disposal; SDK calls afterstartup0\n",checks);
}
