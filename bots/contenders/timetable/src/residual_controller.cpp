#include "residual_controller.h"
#include "residual_observation.h"
#include "residual_routes.h"
#include "residual_words.h"
#include "config.h"
#include "residual_probe.h"
#include <cambc.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <span>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace residual {
int probe::write(const char* bytes,std::size_t size) noexcept {
#if defined(_WIN32)
    return _write(1,bytes,static_cast<unsigned>(size));
#else
    return static_cast<int>(::write(1,bytes,size));
#endif
}
namespace {
enum class Mode {Acquire,Work,Capture,Bank,Recover};
struct FieldTask {
    int key{-999};std::unique_ptr<routes::Builder> builder;std::shared_ptr<const routes::RouteField> field;
    void reset(){key=-999;builder.reset();field.reset();}
};
struct State {
    observation::WorldCache cache;Frame frame;
    Mode mode{Mode::Acquire};int group{-1},epoch{-1},healing_epoch{-1},capture_epoch{-1};
    unsigned prior_gems{},prior_gold{},cycles{};bool walked{},receipt{},physical_refused{};
    std::array<Point,128> trail{};std::size_t trail_size{};double trail_raw{};
    struct Seen {float hp{};int until{-1};};std::vector<Seen> seen;
    FieldTask work,bank,home;Point expected{-1,-1};
};
State& state(){static State value;return value;}
bool current(int epoch){
    if(epoch<0)return false;const auto actual=cambc::round_number();
    if constexpr(tt::cfg::residual_probe)probe::clock(epoch,static_cast<int>(actual));
    return actual==static_cast<unsigned>(epoch);
}
bool funded(int epoch,std::uint64_t reserve){
    if(!current(epoch))return false;const auto fuel=cambc::fuel_remaining();
    if constexpr(tt::cfg::residual_probe){probe::budget(epoch,reserve,fuel);probe::offer(epoch,reserve,fuel,probe::write);}
    return fuel>reserve;
}
bool inside(const Grid& grid,Point p){return p.x>=0&&p.y>=0&&p.x<grid.width&&p.y<grid.height;}
bool open(const Grid& grid,Point p){return inside(grid,p)&&!grid.blocked[static_cast<std::size_t>(p.y)*grid.width+p.x];}
const GroupReading* life(const Frame& f,int group){for(const auto& v:f.groups)if(v.index==group)return &v;return nullptr;}
const CampReading* reading(const Frame& f,int camp){for(const auto& v:f.camps)if(v.index==camp)return &v;return nullptr;}
bool occupied(const Frame& f,Point p){
    for(const auto& v:f.camps)if(v.alive&&v.pos==p)return true;
    for(const auto& v:f.foes)if(v.pos==p)return true;
    for(const auto v:f.allies)if(v==p&&v!=f.body.pos)return true;
    return false;
}
// Immutable all-present, all-awake catalogue envelope. It contains no optimistic
// timer/hidden-health assumption. It prices the ACTUAL reverse trail, not damage
// already incurred along its forward traversal.
double raw(const World& world,Point p){
    double value=0;for(const auto& c:world.camps)if(distance(p,c.pos)<=c.range)value+=c.strength;return value;
}
double contact_upper(const Frame& f,Point p){
    double value=0;
    for(const auto& foe:f.foes){int reach=foe.range;
        for(const auto& s:foe.spells)reach=std::max(reach,s.range+s.radius);
        if(distance(p,foe.pos)>reach+2)continue;
        // A deliberately conservative two-opportunity prefix, not an average
        // chase rate or a promise of a whole escape through player contact.
        double burst=4*foe.strength;for(const auto& s:foe.spells)burst+=s.damage;value+=burst;
    }
    return value;
}
void anchor(State& s,const World& w,Point p){s.trail_size=1;s.trail[0]=p;s.trail_raw=raw(w,p);s.expected=p;}
void remember_step(State& s,const World& w,Point from,Point to){
    if(!s.trail_size||s.trail[s.trail_size-1]!=from){s.trail_size=0;s.trail_raw=0;return;}
    if(s.trail_size>1&&s.trail[s.trail_size-2]==to){s.trail_raw-=raw(w,from);--s.trail_size;}
    else if(s.trail_size<s.trail.size()){s.trail[s.trail_size++]=to;s.trail_raw+=raw(w,to);}
    else{s.trail_size=0;s.trail_raw=0;}
}
bool refresh(State& s){
    if constexpr(tt::cfg::residual_probe)probe::before(s.epoch,2,s.cache.stage(),static_cast<unsigned>(s.cache.status()));
    const auto observed=s.cache.observe(s.frame,s.walked,s.group);
    if constexpr(tt::cfg::residual_probe)probe::returned(s.epoch,2,static_cast<unsigned>(observed),observed==ReadStatus::Stale);
    if(observed!=ReadStatus::Ready||!s.frame.ready)return false;
    if(!current(s.frame.epoch))return false;
    if(s.expected!=Point{-1,-1}&&s.frame.body.pos!=s.expected&&!s.frame.own_spawn){s.trail_size=0;s.trail_raw=0;}
    s.expected=s.frame.body.pos;
    if(const auto* w=s.cache.result()){
        // Optional dated nomination memory: at most512 small records, allocated
        // once under its own admission. Cache incompleteness never gates a
        // current primitive; no per-record SDK fuel/epoch imports are needed.
        if(s.seen.empty()&&w->camps.size()<=512&&funded(s.frame.epoch,55000)){
            if constexpr(tt::cfg::residual_probe)probe::before(s.epoch,5,s.cache.stage(),static_cast<unsigned>(s.cache.status()));
            s.seen.resize(w->camps.size());
            if constexpr(tt::cfg::residual_probe)probe::returned(s.epoch,5,0);
        }
        for(const auto& c:s.frame.camps)if(c.index>=0&&static_cast<std::size_t>(c.index)<s.seen.size()){
            const auto& spec=w->camps[static_cast<std::size_t>(c.index)];const auto* group=life(s.frame,spec.group);
            int until=s.frame.epoch;
            if(group&&(group->life==Life::Living||group->life==Life::Reward))
                until=static_cast<int>(std::min<std::uint64_t>(std::numeric_limits<int>::max(),
                    static_cast<std::uint64_t>(until)+w->groups[static_cast<std::size_t>(spec.group)].respawn));
            else if(group&&group->life==Life::Respawning&&group->respawn>1)
                until=static_cast<int>(std::min<std::uint64_t>(std::numeric_limits<int>::max(),
                    static_cast<std::uint64_t>(until)+static_cast<unsigned>(group->respawn)-1));
            s.seen[static_cast<std::size_t>(c.index)]={c.hp,until};
        }
    }
    return true;
}
bool move(State& s,const World& w,Point to){
    auto& f=s.frame;const Point from=f.body.pos;
    if(!f.ready||!f.alive||!f.body.can_move||distance(from,to)!=1||!open(*w.grid,to)||occupied(f,to)||
       !funded(f.epoch,26000))return false;
    const bool back=s.trail_size>1&&s.trail[s.trail_size-2]==to;
    if(!s.trail_size&&!f.own_spawn&&s.mode!=Mode::Recover)return false;
    const double future=back?s.trail_raw-raw(w,from):s.trail_raw+raw(w,to);
    const double next=raw(w,to)*f.body.incoming_scale+contact_upper(f,to);
    const double shield=f.body.shield_next_batch?f.body.shield:0;
    // The current shield can cover only the NEXT interval. Duration1 cannot
    // fund later reverse-trail exposure after its owner's next input starts.
    const double needed=std::max(0.0,next-shield)+(std::max(0.0,future-raw(w,to))+2*raw(w,to))*f.body.incoming_scale;
    if(s.mode!=Mode::Recover&&f.body.hp<=needed)return false;
    if(s.mode==Mode::Recover&&f.body.hp<=std::max(0.0,next-shield)&&
       next>=raw(w,from)*f.body.incoming_scale+contact_upper(f,from))return false;
    const auto epoch=f.epoch;
    // No dynamic destination is treated as authority merely because the field
    // named it. A false hidden-obstruction result ends this input's physical word.
    if(cambc::vision::teleport_tiles().is_set({to.x,to.y})||!current(epoch))return false;
    const bool accepted=cambc::self().move_absolute({to.x,to.y});
    if(!accepted){s.physical_refused=true;s.frame.ready=false;refresh(s);return false;}
    if(!current(epoch)){s.trail_size=0;s.frame.ready=false;return true;}
    s.walked=true;remember_step(s,w,from,to);s.expected=to;
    refresh(s);return true;
}
void request_field(State& s,FieldTask& task,const World& w,int key,std::vector<Point> roots){
    if(task.key==key)return;
    if(!funded(s.frame.epoch,55000))return;
    task.reset();task.key=key;task.builder=std::make_unique<routes::Builder>(w.grid,std::move(roots));
}
void prepare_field(State& s,FieldTask& task){
    if(!task.builder||task.field)return;
    if constexpr(tt::cfg::residual_probe)probe::before(s.epoch,4,s.cache.stage(),static_cast<unsigned>(task.builder->status()));
    while(funded(s.frame.epoch,task.builder->needs_allocation()?55000:42000)&&task.builder->status()==routes::Status::NeedCompute){
        task.builder->advance(1);if(!task.builder->last_work())break;
    }
    if(task.builder->status()==routes::Status::Ready)task.field=task.builder->result();
    if constexpr(tt::cfg::residual_probe)probe::returned(s.epoch,4,static_cast<unsigned>(task.builder->status()));
}
std::vector<Point> roots(const World& w,Point centre,int radius,bool exact=false){
    std::vector<Point> out;out.reserve(static_cast<std::size_t>((2*radius+1)*(2*radius+1)));
    for(int y=centre.y-radius;y<=centre.y+radius;++y)for(int x=centre.x-radius;x<=centre.x+radius;++x){
        const Point p{x,y};if(open(*w.grid,p)&&(!exact||p==centre))out.push_back(p);
    }return out;
}
Point representative(const World& w,int group){return w.camps[static_cast<std::size_t>(w.groups[static_cast<std::size_t>(group)].members.front())].pos;}
bool step_towards(State& s,const World& w,FieldTask& task,Point goal,int expected_key){
    if constexpr(tt::cfg::residual_probe)probe::before(s.epoch,7,s.cache.stage(),static_cast<unsigned>(s.cache.status()));
    if(!s.frame.body.can_move)return false;
    const auto from=s.frame.body.pos;Point best=from;double score=std::numeric_limits<double>::infinity();
    const auto* field=task.key==expected_key?task.field.get():nullptr;
    const auto old=field?field->distance(from):std::optional<std::uint32_t>{};
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){if(!dx&&!dy)continue;const Point p{from.x+dx,from.y+dy};
        if(!open(*w.grid,p)||occupied(s.frame,p))continue;
        const auto d=field?field->distance(p):std::optional<std::uint32_t>{};
        if(field&&(!d||(old&&*d>=*old)))continue;
        const double value=(d?static_cast<double>(*d):distance(p,goal))*1000+raw(w,p);
        if(value<score){score=value;best=p;}
    }
    return best!=from&&move(s,w,best);
}
bool primitive(State& s,const World& w,const Action& a){
    if constexpr(tt::cfg::residual_probe)probe::before(s.epoch,8,s.cache.stage(),static_cast<unsigned>(s.cache.status()));
    auto& f=s.frame;if(!f.ready||!funded(f.epoch,28000))return false;
    const int epoch=f.epoch;auto& self=cambc::self();
    if(a.kind==ActionKind::Move)return move(s,w,a.destination);
    if(a.kind==ActionKind::Basic){
        const auto* observed=reading(f,a.camp);const auto* handle=s.cache.camp(a.camp);
        if(!f.body.can_attack||!observed||!observed->alive||observed->hp<=0||!handle||distance(f.body.pos,observed->pos)>f.body.range)return false;
        self.attack(*handle);
    }else{
        if(!f.body.can_cast||a.slot>=f.spells.size())return false;const auto& spell=f.spells[a.slot];
        if(!spell.ready||spell.cooldown||(spell.exclusive&&f.body.walked))return false;
        const auto abilities=self.abilities();const auto& ability=abilities[a.slot];bool accepted=false;
        if(a.kind==ActionKind::CastTargeted){
            const auto* observed=reading(f,a.camp);const auto* handle=s.cache.camp(a.camp);
            if(!spell.target_camps||!observed||!observed->alive||!handle||distance(f.body.pos,observed->pos)>spell.range)return false;
            accepted=ability.use(*handle);
        }else if(a.kind==ActionKind::CastArea){
            if(!spell.target_ground||distance(f.body.pos,a.destination)>spell.range)return false;
            accepted=ability.use({a.destination.x,a.destination.y});
        }else if(a.kind==ActionKind::Reset||a.kind==ActionKind::Shield||a.kind==ActionKind::Heal){
            if(!spell.target_self)return false;
            accepted=a.kind==ActionKind::Reset?ability.use():ability.use(self);
        }else return false;
        if(!accepted){s.physical_refused=true;s.frame.ready=false;refresh(s);return false;}
    }
    if(!current(epoch)){s.frame.ready=false;return true;}
    refresh(s);return true;
}
Word simple_word(const Frame& f,const World& w,int group){
    Word out;if(group<0||static_cast<std::size_t>(group)>=w.groups.size())return out;
    int target=-1;float remaining=std::numeric_limits<float>::infinity();
    for(const auto i:w.groups[static_cast<std::size_t>(group)].members){const auto* c=reading(f,i);
        if(c&&c->alive&&c->hp>0&&distance(f.body.pos,c->pos)<=f.body.range&&c->hp+c->shield<remaining){target=i;remaining=c->hp+c->shield;}}
    if(target<0)return out;
    if(f.body.can_attack)out.actions[out.count++]={ActionKind::Basic,0,target,{}};
    else if(f.body.can_cast)for(const auto& spell:f.spells)
        if(spell.kind==SpellKind::Reset&&spell.target_self&&spell.ready&&!spell.cooldown&&(!spell.exclusive||!f.body.walked)){
            out.actions[out.count++]={ActionKind::Reset,spell.slot,-1,{}};out.actions[out.count++]={ActionKind::Basic,0,target,{}};break;}
    out.supported=out.count!=0;out.useful_damage=out.count?std::min(f.body.strength,remaining):0;return out;
}
WordAdmission word_admission(void*,int epoch,WordWork) noexcept {
    if(!current(epoch))return WordAdmission::Stale;
    // The existing Work reserve belongs to the physical continuation. Optional
    // improvement search cannot consume it merely to finish an optimum.
    return cambc::fuel_remaining()>48000?WordAdmission::Admit:WordAdmission::Defer;
}
bool work(State& s,const World& w){
    // This controller executes only damage-producing words. With no actual
    // living personal target, every represented word has zero HP progress;
    // preserve the travel owner's fuel instead of searching that empty class.
    bool target_seen=false;
    if(s.group>=0&&static_cast<std::size_t>(s.group)<w.groups.size())
        for(const auto i:w.groups[static_cast<std::size_t>(s.group)].members){const auto* c=reading(s.frame,i);
            if(c&&c->alive&&c->hp>0){target_seen=true;break;}}
    if(!target_seen)return false;
    bool any=false;unsigned executed{};
    while(executed<4&&s.frame.ready&&funded(s.frame.epoch,48000)){
        if constexpr(tt::cfg::residual_probe)probe::before(s.epoch,3,s.cache.stage(),static_cast<unsigned>(s.cache.status()));
        const auto fallback=simple_word(s.frame,w,s.group);
        const auto selected=choose_word_bounded(s.frame,w,s.group,s.frame.body.pos,WordBudget{nullptr,word_admission});
        if constexpr(tt::cfg::residual_probe)probe::returned(s.epoch,3,static_cast<unsigned>(selected.status),selected.status==WordResult::Status::Stale);
        if(selected.status==WordResult::Status::Stale||!current(s.frame.epoch)){s.frame.ready=false;return any;}
        auto word=selected.word;
        if(!word.supported||!word.count||word.useful_damage<=0)word=fallback;
        if(!word.count||word.useful_damage<=0)break;
        for(unsigned i=0;i<word.count&&executed<4;++i){
            if(!s.frame.ready||!funded(s.frame.epoch,30000))return any;
            const auto& next=word.actions[i];
            const double at=raw(w,s.frame.body.pos),suffix=std::max(0.0,s.trail_raw-at)*s.frame.body.incoming_scale;
            // Every prefix survives independently. A later proposed kill,
            // shield or heal cannot pay for an unfinished earlier primitive.
            double shield=s.frame.body.shield_next_batch?s.frame.body.shield:0;
            if(next.kind==ActionKind::Shield&&next.slot<s.frame.spells.size()&&s.frame.spells[next.slot].duration>=1)
                shield=s.frame.spells[next.slot].shield;
            const double loss=std::max(0.0,at*s.frame.body.incoming_scale-shield);
            const double healing=next.kind==ActionKind::Heal&&next.slot<s.frame.spells.size()?
                std::min(s.frame.body.max_hp-s.frame.body.hp,s.frame.spells[next.slot].heal):0;
            if(!s.trail_size||s.frame.body.hp+healing<=loss+suffix+2*at*s.frame.body.incoming_scale){s.mode=Mode::Recover;return any;}
            if(!primitive(s,w,next))return any;++executed;any=true;
            if(!s.frame.foes.empty()){s.mode=Mode::Recover;return any;}
        }
    }return any;
}
void after_work(State& s,const World& w){
    if(!s.frame.ready||s.physical_refused||!s.frame.body.can_move||s.mode==Mode::Recover)return;
    const auto* observed=life(s.frame,s.group);int target=-1;int nearest=std::numeric_limits<int>::max();
    const bool reward=observed&&observed->life==Life::Reward;
    for(const auto i:w.groups[static_cast<std::size_t>(s.group)].members){
        const auto* c=reading(s.frame,i);if(!reward&&(!c||!c->alive))continue;
        const auto p=w.camps[static_cast<std::size_t>(i)].pos;const int d=distance(s.frame.body.pos,p);
        if(reward&&d==0)return;if(d<nearest){nearest=d;target=i;}}
    if(target<0||(!reward&&nearest<=s.frame.body.range))return;
    const auto goal=w.camps[static_cast<std::size_t>(target)].pos;
    request_field(s,s.work,w,reward?10000+s.group:target,roots(w,goal,reward?0:std::max(1,s.frame.body.range),reward));
    if(reward)s.mode=Mode::Capture;step_towards(s,w,s.work,goal,reward?10000+s.group:target);prepare_field(s,s.work);
}
std::array<std::byte,8> claims(){
    std::array<std::byte,8> out{};if(cambc::shared_memory.size()==out.size())cambc::shared_memory.read(0,std::span<std::byte>{out});return out;
}
void advertise(const Frame& f,int group){
    const auto role=static_cast<unsigned>(f.role);if(role>=5||cambc::shared_memory.size()!=8)return;
    cambc::shared_memory[role]=static_cast<std::uint8_t>(group>=0&&group<254?group+1:0);
}
int choose_group(State& s,const World& w){
    if constexpr(tt::cfg::residual_probe)probe::before(s.epoch,6,s.cache.stage(),static_cast<unsigned>(s.cache.status()));
    const auto hints=claims();int best=-1;double score=-1;
    for(std::size_t g=0;g<w.groups.size();++g){const auto& spec=w.groups[g];if(!spec.gems||spec.members.empty())continue;
        const auto* observed=life(s.frame,static_cast<int>(g));if(observed&&observed->life==Life::Respawning)continue;
        double hp=0;int travel=std::numeric_limits<int>::max();
        for(const auto i:spec.members){const auto& c=w.camps[static_cast<std::size_t>(i)];const auto* v=reading(s.frame,i);
            const bool dated=static_cast<std::size_t>(i)<s.seen.size()&&s.seen[static_cast<std::size_t>(i)].until>=s.frame.epoch;
            hp+=v?v->hp:dated?s.seen[static_cast<std::size_t>(i)].hp:c.max_hp;
            travel=std::min(travel,distance(s.frame.body.pos,c.pos));}
        unsigned competing{};for(unsigned role=0;role<5;++role)
            if(role!=static_cast<unsigned>(s.frame.role)&&std::to_integer<unsigned>(hints[role])==g+1)++competing;
        // These bytes are advisory own nominations and may be stale. They add
        // finite displaced-service cost, never foreign HP/kit/Life authority.
        const double work_inputs=hp/std::max(1.0f,s.frame.body.strength);
        const double time=std::max(0,travel-1)+work_inputs+cambc::constants::capture_time+5;
        const double value=spec.gems/(time*(1+0.5*competing));
        if(value>score){score=value;best=static_cast<int>(g);}
    }
    if constexpr(tt::cfg::residual_probe)probe::returned(s.epoch,6,best>=0?0:3);
    return best;
}
void recover(State& s,const World& w){
    auto& f=s.frame;
    if(f.own_spawn){
        if(s.healing_epoch<0){s.healing_epoch=f.epoch;anchor(s,w,f.body.pos);return;}
        if(f.epoch<=s.healing_epoch)return;
        // One actual spawn end has occurred. Re-entry uses the newly observed
        // HP and renewed per-step debt; full HP may be unobservable under fire.
        anchor(s,w,f.body.pos);s.healing_epoch=-1;s.mode=Mode::Acquire;s.group=-1;s.work.reset();++s.cycles;
        std::printf("residual renewed r%d role%u hp%.0f cycles%u\n",f.epoch,static_cast<unsigned>(f.role),f.body.hp,s.cycles);return;
    }
    if(f.recalling)return;
    if(f.foes.empty()&&raw(w,f.body.pos)==0&&funded(f.epoch,28000)){
        cambc::self().recall();return;
    }
    if(s.trail_size>1){move(s,w,s.trail[s.trail_size-2]);return;}
    Point home=w.grid->own_spawn.empty()?f.body.pos:w.grid->own_spawn.front();
    for(const auto p:w.grid->own_spawn)if(distance(f.body.pos,p)<distance(f.body.pos,home))home=p;
    request_field(s,s.home,w,-3,w.grid->own_spawn);
    step_towards(s,w,s.home,home,-3);prepare_field(s,s.home);
}
void bank(State& s,const World& w){
    auto& f=s.frame;
    if(!f.gems){s.mode=Mode::Recover;s.receipt=true;return;}
    if(f.submitting)return;
    std::vector<Point> stands;Point altar{};int nearest=std::numeric_limits<int>::max();
    const int reach=std::max(0,cambc::constants::submit_gems_range);
    for(const auto a:w.grid->altars){const auto r=roots(w,a,reach);stands.insert(stands.end(),r.begin(),r.end());
        if(distance(f.body.pos,a)<nearest){nearest=distance(f.body.pos,a);altar=a;}}
    request_field(s,s.bank,w,-2,std::move(stands));
    if(nearest<=reach){
        const auto ticks=std::max(cambc::constants::submit_gem_minimum_duration,
            static_cast<int>(std::ceil(f.gems*cambc::constants::submit_gem_duration_multiplier))+cambc::constants::submit_gem_base_duration);
        const double risk=(s.trail_raw+(ticks+1)*raw(w,f.body.pos))*f.body.incoming_scale+contact_upper(f,f.body.pos);
        if(f.body.hp<=risk){s.mode=Mode::Recover;return;}
        if(funded(f.epoch,28000)){cambc::self().submit_gems({altar.x,altar.y});std::printf("residual bank-start r%d role%u gems%u N%d\n",f.epoch,static_cast<unsigned>(f.role),f.gems,ticks);}return;
    }
    step_towards(s,w,s.bank,altar,-2);prepare_field(s,s.bank);
}
}

void play_draft(){
    constexpr std::array roster={cambc::champion_draft::varg,cambc::champion_draft::marcella,cambc::champion_draft::ryth,
        cambc::champion_draft::vesper_vale,cambc::champion_draft::heart_of_ruin};
    const auto selected=cambc::draft::current_draft(cambc::team::ally);
    for(const auto pick:roster)if(std::find(selected.begin(),selected.end(),pick)==selected.end()){cambc::draft::pick(pick);return;}
}
void play_turn(){
    auto& s=state();const int epoch=static_cast<int>(cambc::round_number());
    if(s.epoch!=epoch){s.epoch=epoch;s.walked=false;s.physical_refused=false;}
    if(s.cache.status()!=ReadStatus::Ready){
        if constexpr(tt::cfg::residual_probe)probe::before(epoch,1,s.cache.stage(),static_cast<unsigned>(s.cache.status()));
        const auto status=s.cache.advance(256);
        if constexpr(tt::cfg::residual_probe)probe::returned(epoch,1,static_cast<unsigned>(status),status==ReadStatus::Stale);
        return;
    }
    const auto* world=s.cache.result();if(!world||!refresh(s))return;const auto& w=*world;auto& f=s.frame;
    if(!f.alive){s.mode=Mode::Acquire;s.group=-1;s.trail_size=0;return;}
    if(f.teleporting||f.recalling||f.submitting||f.body.stunned)return;
    if(f.own_spawn&&(!s.trail_size||s.mode==Mode::Acquire))anchor(s,w,f.body.pos);
    if(s.prior_gems&&f.gems==0){s.receipt=true;s.mode=Mode::Recover;
        std::printf("residual bank-readback r%d role%u prior%u\n",epoch,static_cast<unsigned>(f.role),s.prior_gems);}
    if(f.gems>s.prior_gems){s.mode=Mode::Bank;s.receipt=true;
        std::printf("residual capture-readback r%d role%u bag%u\n",epoch,static_cast<unsigned>(f.role),f.gems);}
    s.prior_gems=f.gems;s.prior_gold=f.gold;
    if(!f.foes.empty()&&s.mode!=Mode::Bank)s.mode=Mode::Recover;
    if(s.mode==Mode::Recover){recover(s,w);return;}
    if(f.gems||s.mode==Mode::Bank){s.mode=Mode::Bank;bank(s,w);return;}
    if(s.group<0||static_cast<std::size_t>(s.group)>=w.groups.size()){
        s.group=choose_group(s,w);s.work.reset();s.capture_epoch=-1;
        if(s.group<0)return;s.mode=Mode::Work;advertise(f,s.group);
    }
    const auto* observed=life(f,s.group);
    if(observed&&observed->life==Life::Respawning){s.group=-1;s.mode=Mode::Acquire;advertise(f,-1);return;}
    if(observed&&observed->life==Life::Reward){
        s.mode=Mode::Capture;const auto& spec=w.groups[static_cast<std::size_t>(s.group)];Point tile=representative(w,s.group);
        for(const auto i:spec.members)if(distance(f.body.pos,w.camps[static_cast<std::size_t>(i)].pos)<distance(f.body.pos,tile))tile=w.camps[static_cast<std::size_t>(i)].pos;
        bool on=false;for(const auto i:spec.members)on|=f.body.pos==w.camps[static_cast<std::size_t>(i)].pos;
        if(on){if(s.capture_epoch<0)s.capture_epoch=epoch;return;}
        request_field(s,s.work,w,10000+s.group,roots(w,tile,0,true));step_towards(s,w,s.work,tile,10000+s.group);prepare_field(s,s.work);return;
    }
    s.mode=Mode::Work;
    if(work(s,w)){after_work(s,w);return;}
    if(s.physical_refused||!s.frame.ready)return;
    if(s.mode==Mode::Recover){recover(s,w);return;}
    int target=-1;int nearest=std::numeric_limits<int>::max();
    for(const auto i:w.groups[static_cast<std::size_t>(s.group)].members){const auto* v=reading(f,i);if(v&&!v->alive)continue;
        const int d=distance(f.body.pos,w.camps[static_cast<std::size_t>(i)].pos);if(d<nearest){nearest=d;target=i;}}
    if(target<0){s.group=-1;s.mode=Mode::Acquire;return;}
    const auto goal=w.camps[static_cast<std::size_t>(target)].pos;
    request_field(s,s.work,w,target,roots(w,goal,std::max(1,f.body.range)));
    if(step_towards(s,w,s.work,goal,target)&&s.frame.ready)work(s,w);
    prepare_field(s,s.work);
}
}
