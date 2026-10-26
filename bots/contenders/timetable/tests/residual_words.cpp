#include "../src/residual_words.cpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

namespace {
using namespace residual;
unsigned checks{},failures{};
void check(bool v,const char* why){++checks;if(!v){++failures;std::cerr<<"FAIL "<<why<<'\n';}}
struct Case {World world;Frame frame;};
Case base(){
    Case c;auto grid=std::make_shared<Grid>();grid->width=12;grid->height=8;grid->blocked.assign(96,0);c.world.grid=grid;
    c.world.groups.resize(1);c.world.groups[0].respawn=50;
    c.frame.epoch=10;c.frame.role=Role::Varg;c.frame.ready=true;c.frame.alive=true;
    c.frame.body.pos={1,1};c.frame.body.hp=1000;c.frame.body.max_hp=1000;c.frame.body.strength=40;
    c.frame.body.incoming_scale=1;c.frame.body.range=1;c.frame.body.can_move=true;
    c.frame.body.can_attack=true;c.frame.body.can_cast=true;
    c.frame.groups.push_back({0,Life::Living});return c;
}
int camp(Case& c,Point p,float hp,float strength=0,int range=3,int group=0,bool visible=true){
    while(c.world.groups.size()<=static_cast<std::size_t>(group))c.world.groups.emplace_back();
    const int i=static_cast<int>(c.world.camps.size());
    c.world.camps.push_back({static_cast<unsigned>(i+10),group,p,hp,strength,range});
    c.world.groups[group].members.push_back(i);
    if(visible)c.frame.camps.push_back({i,p,hp,0,0,true});return i;
}
Spell reset(unsigned slot=0,bool enhanced=false){Spell s;s.kind=SpellKind::Reset;s.slot=slot;s.ready=true;s.period=8;s.target_self=true;s.enhanced=enhanced;return s;}
Spell targeted(unsigned slot,float damage,int range=4){Spell s;s.kind=SpellKind::Targeted;s.slot=slot;s.ready=true;s.period=8;s.damage=damage;s.range=range;s.target_camps=true;return s;}
unsigned count(const Word& w,ActionKind k){unsigned n=0;for(unsigned i=0;i<w.count;++i)n+=w.actions[i].kind==k;return n;}
bool same(const Word& a,const Word& b){
    if(a.count!=b.count||a.supported!=b.supported||a.useful_damage!=b.useful_damage||a.incoming_upper!=b.incoming_upper||
        a.own_healing!=b.own_healing||a.shield_after!=b.shield_after||a.own_damage_upper!=b.own_damage_upper)return false;
    for(unsigned i=0;i<a.count;++i)if(a.actions[i].kind!=b.actions[i].kind||a.actions[i].slot!=b.actions[i].slot||
        a.actions[i].camp!=b.actions[i].camp||a.actions[i].destination!=b.actions[i].destination)return false;
    return true;
}
struct Gate {
    std::array<unsigned,9> calls{};WordWork deny{WordWork::Finish};unsigned deny_on{};
    WordAdmission denial{WordAdmission::Defer},finish{WordAdmission::Admit};int expected{10};
    static WordAdmission allow(void* context,int epoch,WordWork work) noexcept {
        auto& g=*static_cast<Gate*>(context);const auto n=++g.calls[static_cast<unsigned>(work)];
        if(epoch!=g.expected)return WordAdmission::Stale;
        if(work==WordWork::Finish)return g.finish;
        if(work==g.deny&&n==g.deny_on)return g.denial;
        return WordAdmission::Admit;
    }
    WordBudget budget(){return {this,&allow};}
    unsigned used(WordWork work)const{return calls[static_cast<unsigned>(work)];}
};
// An independent allowance/visibility/range checker consumes the ACTUAL output
// word. It does not predict receipts or simulate future engine commands.
void compatible(const Case& c,const Word& w){
    check(w.supported&&w.count<=4,"bounded supported word");
    bool m=c.frame.body.can_move,a=c.frame.body.can_attack,cast=c.frame.body.can_cast,walked=c.frame.body.walked,exclusive=false;
    Point pos=c.frame.body.pos;unsigned moves=0,casts=0;
    for(unsigned i=0;i<w.count;++i){const auto& op=w.actions[i];
        if(op.kind==ActionKind::Move){check(m&&!exclusive&&distance(pos,op.destination)==1,"ordinary edge needs retained M and no exclusive cast");
            pos=op.destination;m=false;walked=true;++moves;if(c.frame.body.ranged)a=false;
        }else if(op.kind==ActionKind::Basic){
            check(a,"basic needs actual A");bool seen=false;
            for(const auto& r:c.frame.camps)if(r.index==op.camp&&r.alive){seen=true;check(distance(pos,r.pos)<=c.frame.body.range,"basic uses its actual reach");}
            check(seen,"basic primary is personally visible");a=false;if(c.frame.body.ranged)m=false;
        }else{
            check(cast,"global one-cast allowance");const Spell* spell=nullptr;
            for(const auto& s:c.frame.spells)if(s.slot==op.slot&&s.kind!=SpellKind::None)spell=&s;
            check(spell&&spell->ready&&spell->cooldown==0,"actual slot identity and ready clock");
            if(!spell)continue;check(!spell->exclusive||!walked,"exclusive guard retains actual ordinary walk provenance");
            if(op.kind==ActionKind::Reset){check(!a&&spell->target_self,"reset requires spent A and self mask");a=true;}
            if(op.kind==ActionKind::CastArea)check(spell->target_ground&&spell->target_camps&&distance(pos,op.destination)<=spell->range,"ground cast has actual mask and cast reach");
            if(op.kind==ActionKind::CastTargeted){bool seen=false;
                for(const auto& r:c.frame.camps)if(r.index==op.camp&&r.alive){seen=true;check(distance(pos,r.pos)<=spell->range,"targeted spell uses own range");}
                check(seen&&spell->target_camps,"unit spell primary visible with camp mask");}
            if(spell->exclusive){exclusive=true;m=false;}cast=false;++casts;
        }
    }
    check(moves<=1&&casts<=1,"one ordinary Move and one global Cast");
}
}

int main(){
    {
        auto c=base();camp(c,{2,1},200);c.frame.spells[0]=reset();
        auto w=choose_word(c.frame,c.world,0,c.frame.body.pos);compatible(c,w);
        check(w.count==3&&w.actions[0].kind==ActionKind::Basic&&w.actions[1].kind==ActionKind::Reset&&w.actions[2].kind==ActionKind::Basic,"Basic Reset Basic is a real three-primitive word");
        check(w.useful_damage==80,"two basics apply actual effective camp damage");
        w=choose_word(c.frame,c.world,0,Point{2,2});compatible(c,w);
        check(w.count==4&&w.actions[0].kind==ActionKind::Basic&&w.actions[1].kind==ActionKind::Reset&&w.actions[2].kind==ActionKind::Basic&&w.actions[3].kind==ActionKind::Move,"melee complete Basic Reset Basic Move retains actual egress edge");
    }
    {
        auto c=base();c.frame.role=Role::Vesper;c.frame.body.ranged=true;c.frame.body.range=3;camp(c,{5,1},200);
        c.frame.spells[0]=reset();const auto before=c.frame;
        auto w=choose_word(c.frame,c.world,0,Point{2,1});compatible(c,w);
        check(w.count==3&&w.actions[0].kind==ActionKind::Move&&w.actions[1].kind==ActionKind::Reset&&w.actions[2].kind==ActionKind::Basic,"ranged Move Reset Basic restores only A");
        check(count(w,ActionKind::Move)==1&&w.useful_damage==40,"reset never restores a second ordinary move");
        check(c.frame.body.pos==before.body.pos&&c.frame.body.can_move==before.body.can_move&&c.frame.camps[0].hp==before.camps[0].hp,"nomination does not mutate actual observations");
        c.frame.spells[0].cooldown=1;w=choose_word(c.frame,c.world,0,Point{2,1});
        check(w.supported&&w.useful_damage==0&&count(w,ActionKind::Reset)==0,"not-ready reset cannot manufacture a moving ranged attack");
    }
    {
        auto c=base();camp(c,{2,1},300);c.frame.spells[1]=targeted(1,120);c.frame.spells[1].exclusive=true;
        c.frame.body.walked=true;auto w=choose_word(c.frame,c.world,0,c.frame.body.pos);compatible(c,w);
        check(count(w,ActionKind::CastTargeted)==0&&w.useful_damage==40,"actual prior walk blocks exclusive cast even when this word contains no Move");
        c.frame.body.walked=false;c.frame.body.ranged=true;c.frame.body.range=3;
        w=choose_word(c.frame,c.world,0,c.frame.body.pos);compatible(c,w);
        check(count(w,ActionKind::Basic)==1&&count(w,ActionKind::CastTargeted)==1&&w.useful_damage==160,"ranged Basic consuming M is not ordinary walk provenance");
        c.frame.spells[0]=reset();w=choose_word(c.frame,c.world,0,c.frame.body.pos);compatible(c,w);
        check(count(w,ActionKind::Reset)+count(w,ActionKind::CastTargeted)==1,"two ready slots cannot supply two casts");
        c.frame.spells[1].target_camps=false;c.frame.spells[0]={};w=choose_word(c.frame,c.world,0,c.frame.body.pos);
        check(count(w,ActionKind::CastTargeted)==0,"enemy-only damage cannot target a camp");
    }
    {
        auto c=base();c.frame.role=Role::Marcella;camp(c,{2,1},300,30);camp(c,{2,2},300,30);
        c.frame.spells[0]=reset(0,true);auto w=choose_word(c.frame,c.world,0,c.frame.body.pos);compatible(c,w);
        check(w.useful_damage==80&&w.incoming_upper==30,"enhanced reset stuns only its later primary basic, not every camp");
        c.frame.spells[0].enhanced=false;w=choose_word(c.frame,c.world,0,c.frame.body.pos);
        check(w.useful_damage==80&&w.incoming_upper==60,"ordinary reset has no invented stun");
        c.frame.spells[0]={};c.frame.body.enhanced=true;c.frame.body.next_basic_stun=1;
        w=choose_word(c.frame,c.world,0,c.frame.body.pos);check(w.useful_damage==40&&w.incoming_upper==30,"fresh existing enhancement applies to one actual basic");
    }
    {
        auto c=base();c.frame.body.splash=true;c.frame.body.splash_scale=1;c.frame.body.splash_radius=1;
        camp(c,{2,1},100);camp(c,{3,1},100);camp(c,{4,1},100);
        auto w=choose_word(c.frame,c.world,0,c.frame.body.pos);compatible(c,w);
        check(w.useful_damage==80&&count(w,ActionKind::Basic)==1,"splash centered on primary reaches beyond caster basic range and does not hit primary twice");
        c.frame.body.splash_scale=0.5f;w=choose_word(c.frame,c.world,0,c.frame.body.pos);
        check(w.useful_damage==60,"actual supplied splash coefficient is applied once");
    }
    {
        auto c=base();c.frame.role=Role::Ryth;c.frame.body.strength=0;camp(c,{3,1},100,20);camp(c,{5,1},100,20);
        camp(c,{8,1},100,11,10,1,false);
        auto& s=c.frame.spells[0];s.kind=SpellKind::Area;s.slot=0;s.ready=true;s.damage=70;s.range=4;s.radius=1;
        s.stun=1;s.duration=0;s.exclusive=true;s.target_ground=true;s.target_camps=true;
        auto w=choose_word(c.frame,c.world,0,c.frame.body.pos);compatible(c,w);
        check(w.count==1&&w.actions[0].kind==ActionKind::CastArea&&w.actions[0].camp==-1,"Ryth emits an actual ground action, not a unit/CC action");
        const auto aim=w.actions[0].destination;
        check(distance(aim,Point{3,1})<=1&&distance(aim,Point{5,1})<=1&&aim!=Point{3,1}&&aim!=Point{5,1},"all legal ground centres include the between-member coverage solution");
        check(w.useful_damage==140&&w.incoming_upper==11,"explicit Area stun protects against one next batch while hidden external camp remains");
        c.frame.role=Role::Other;s.stun=0;s.duration=1;w=choose_word(c.frame,c.world,0,c.frame.body.pos);
        check(w.incoming_upper==31,"generic AoE duration is not invented crowd control; the second camp is outside attack range");
        s.target_ground=false;w=choose_word(c.frame,c.world,0,c.frame.body.pos);
        check(w.count==0&&w.useful_damage==0,"ground target mask is mandatory");
    }
    {
        auto c=base();camp(c,{2,1},100,20,3,0,false);auto w=choose_word(c.frame,c.world,0,c.frame.body.pos);
        check(w.supported&&w.count==0&&w.useful_damage==0&&w.incoming_upper==20,"hidden camp retains catalogue hazard and never becomes a target");
        c.frame.camps.push_back({0,{2,1},0,0,0,false});c.frame.groups[0].life=Life::Respawning;
        for(int timer=0;timer<=2;++timer){c.frame.groups[0].respawn=timer;w=choose_word(c.frame,c.world,0,c.frame.body.pos);
            check(w.incoming_upper==(timer>1?0:20),"dead reading alone does not erase an imminent birth");}
        c.frame.groups[0].life=Life::Unknown;w=choose_word(c.frame,c.world,0,c.frame.body.pos);check(w.incoming_upper==20,"unknown lifecycle retains dead-reading catalogue support");
        c.frame.groups[0].life=Life::Reward;w=choose_word(c.frame,c.world,0,c.frame.body.pos);check(w.incoming_upper==0,"actual Reward plus non-imminent group cooldown proves next-batch absence");
        c.world.groups[0].respawn=1;w=choose_word(c.frame,c.world,0,c.frame.body.pos);check(w.incoming_upper==20,"unproved short respawn retains upper");
    }
    {
        auto c=base();camp(c,{2,1},100);const int other=camp(c,{4,1},100,20,4,1,false);
        c.frame.camps.push_back({other,{4,1},0,0,0,false});c.frame.groups.push_back({1,Life::Respawning});
        for(int timer=0;timer<=2;++timer){c.frame.groups[1].respawn=timer;const auto w=choose_word(c.frame,c.world,0,c.frame.body.pos);
            check(w.incoming_upper==(timer>1?0:20),"external dead source uses its own dated lifecycle, not selected-group clearance");}
        c.frame.groups[1].life=Life::Unknown;const auto w=choose_word(c.frame,c.world,0,c.frame.body.pos);
        check(w.incoming_upper==20,"external hidden-generation support survives uncertain absence");
    }
    {
        auto c=base();camp(c,{2,1},20,30);auto w=choose_word(c.frame,c.world,0,c.frame.body.pos);
        check(w.useful_damage==20&&w.incoming_upper==0,"fresh actual kill in a supported living generation removes its next camp attack");
        c.world.groups[0].respawn=0;w=choose_word(c.frame,c.world,0,c.frame.body.pos);
        check(w.useful_damage==20&&w.incoming_upper==30,"unproven next-generation interval does not erase killed source potential");
        c.world.groups[0].respawn=50;c.frame.body.strength=0;c.frame.camps[0].stun=0;
        w=choose_word(c.frame,c.world,0,c.frame.body.pos);check(w.incoming_upper==30,"stun timer zero expires before the camp acts");
        c.frame.camps[0].stun=1;w=choose_word(c.frame,c.world,0,c.frame.body.pos);check(w.incoming_upper==0,"fresh positive camp stun covers one camp opportunity");
    }
    {
        auto c=base();camp(c,{2,1},200,20);c.frame.body.strength=0;c.frame.body.shield=30;
        auto w=choose_word(c.frame,c.world,0,c.frame.body.pos);check(w.incoming_upper==20&&w.shield_after==0,"old shield without phase expiry proof grants no next-batch credit");
        c.frame.body.shield_next_batch=true;w=choose_word(c.frame,c.world,0,c.frame.body.pos);
        check(w.incoming_upper==20&&w.shield_after==10,"explicit fresh shield coverage preserves raw incoming and prices absorption once");
        c.frame.body.shield_next_batch=false;c.frame.body.hp=25;c.frame.body.max_hp=1000;
        auto& s=c.frame.spells[1];s.kind=SpellKind::Shield;s.slot=1;s.ready=true;s.target_self=true;s.shield=100;s.duration=1;
        w=choose_word(c.frame,c.world,0,c.frame.body.pos);compatible(c,w);
        check(count(w,ActionKind::Shield)==1&&w.incoming_upper==20&&w.shield_after==80,"new lasting self shield prices one actual next batch");
        s.duration=0;w=choose_word(c.frame,c.world,0,c.frame.body.pos);check(count(w,ActionKind::Shield)==0,"zero-duration shield has no future credit");
        s={};s.kind=SpellKind::Heal;s.slot=1;s.ready=true;s.target_self=false;s.heal=2000;
        w=choose_word(c.frame,c.world,0,c.frame.body.pos);check(count(w,ActionKind::Heal)==0,"ally-only heal never heals self");
        s.target_self=true;w=choose_word(c.frame,c.world,0,c.frame.body.pos);compatible(c,w);
        check(w.own_healing==975&&count(w,ActionKind::Heal)==1,"explicit self heal caps at actual missing HP");
    }
    {
        auto c=base();camp(c,{2,1},100);c.frame.body.can_attack=false;c.frame.body.can_cast=false;
        const auto blocked=choose_word(c.frame,c.world,0,Point{2,1});check(!blocked.supported,"live camp occupancy rejects requested ordinary edge");
        c.frame.camps[0].alive=false;c.frame.camps[0].hp=0;c.frame.groups[0].life=Life::Reward;
        auto w=choose_word(c.frame,c.world,0,Point{2,1});check(w.supported&&count(w,ActionKind::Move)==1,"fresh actual dead camp can permit an ordinary edge nomination");
        c.frame.allies.push_back({2,1});check(!choose_word(c.frame,c.world,0,Point{2,1}).supported,"known ally blocks edge");
        c.frame.allies.clear();c.frame.foes.push_back({});c.frame.foes[0].pos={2,1};
        check(!choose_word(c.frame,c.world,0,Point{2,1}).supported,"known enemy blocks edge without inventing a contact action");
        c.frame.foes.clear();auto g=std::make_shared<Grid>(*c.world.grid);g->blocked[14]=1;c.world.grid=g;
        check(!choose_word(c.frame,c.world,0,Point{2,1}).supported,"terrain blocking remains mandatory");
        check(!choose_word(c.frame,c.world,0,Point{4,1}).supported,"candidate final pose cannot teleport the body");
    }
    {
        auto c=base();camp(c,{2,1},649);c.frame.body.strength=129.8f;c.frame.spells[0]=reset();
        const float one=649.0f-129.8f,two=one-129.8f;auto w=choose_word(c.frame,c.world,0,c.frame.body.pos);
        check(w.useful_damage==649.0f-two,"actual HP progress follows repeated float32 subtraction");
        c.frame.camps[0].shield=200;w=choose_word(c.frame,c.world,0,c.frame.body.pos);
        check(w.useful_damage<60&&w.useful_damage>59,"target shield is not counted as useful HP damage");
        c.frame.spells[0].kind=SpellKind::Dash;w=choose_word(c.frame,c.world,0,c.frame.body.pos);
        check(count(w,ActionKind::Dash)==0,"dash omitted without a legal-path representation");
    }
    {
        auto c=base();for(unsigned i=0;i<33;++i)camp(c,{static_cast<int>(i%10),static_cast<int>(i/10)+2},100);
        check(!choose_word(c.frame,c.world,0).supported,"capacity is typed unsupported rather than tiny-camp truncation");
        auto d=base();camp(d,{2,1},100);d.frame.camps.push_back(d.frame.camps[0]);
        check(!choose_word(d.frame,d.world,0).supported,"duplicate personal observations cannot double physical HP");
        d=base();camp(d,{2,1},100);d.frame.body.hp=std::numeric_limits<float>::quiet_NaN();
        check(!choose_word(d.frame,d.world,0).supported,"nonfinite own state fails closed");
        d=base();camp(d,{2,1},100);d.frame.camps[0].pos={9,7};
        check(!choose_word(d.frame,d.world,0).supported,"reading must bind exact immutable member coordinates");
    }
    {
        auto c=base();camp(c,{2,1},200,5);camp(c,{4,1},100,20,2,1,false);camp(c,{1,3},100,11,2,2,false);
        Gate current,adjacent,all;
        const auto a=choose_word_bounded(c.frame,c.world,0,c.frame.body.pos,current.budget());
        const auto b=choose_word_bounded(c.frame,c.world,0,Point{2,2},adjacent.budget());
        const auto u=choose_word_bounded(c.frame,c.world,0,all.budget());
        check(a.status==WordResult::Status::Complete&&b.status==WordResult::Status::Complete&&u.status==WordResult::Status::Complete,"admitting every bounded leaf completes all three pose domains");
        check(current.used(WordWork::External)==1&&adjacent.used(WordWork::External)==2&&all.used(WordWork::External)==9,"current-only, origin+adjacent, and nine-pose domains charge exactly their reachable source tables");
        check(a.word.incoming_upper==16&&b.word.incoming_upper==36,"no hidden external hazard disappears from either retained pose");
        check(same(a.word,choose_word(c.frame,c.world,0,c.frame.body.pos))&&same(b.word,choose_word(c.frame,c.world,0,Point{2,2}))&&same(u.word,choose_word(c.frame,c.world,0)),"unlimited wrapper and all-admitted bounded search preserve words, metrics, order and ties");
    }
    {
        auto c=base();camp(c,{2,1},200);Gate enter;enter.deny=WordWork::Enter;enter.deny_on=1;
        auto r=choose_word_bounded(c.frame,c.world,0,c.frame.body.pos,enter.budget());
        check(r.status==WordResult::Status::Deferred&&!r.word.supported&&enter.used(WordWork::Records)==0,"entry admission precedes fixed search storage and record work");
        Gate external;external.deny=WordWork::External;external.deny_on=1;
        r=choose_word_bounded(c.frame,c.world,0,c.frame.body.pos,external.budget());
        check(r.status==WordResult::Status::Deferred&&!r.word.supported&&external.used(WordWork::Consider)==0&&external.used(WordWork::Finish)==1,"unfinished all-source envelope cannot become a feasible incumbent");
        Gate evaluation;evaluation.deny=WordWork::Consider;evaluation.deny_on=2;
        r=choose_word_bounded(c.frame,c.world,0,c.frame.body.pos,evaluation.budget());
        check(r.status==WordResult::Status::Deferred&&r.word.supported&&r.word.count==0&&r.word.useful_damage==0,"a completed empty incumbent survives interruption before the first damaging prefix is evaluated");
    }
    {
        auto c=base();camp(c,{1,0},300);auto& s=c.frame.spells[0];s.kind=SpellKind::Area;s.ready=true;s.slot=0;
        s.target_camps=true;s.target_ground=true;s.range=4;s.radius=1;s.damage=80;s.exclusive=true;
        Gate gate;gate.deny=WordWork::AreaAim;gate.deny_on=2;
        const auto r=choose_word_bounded(c.frame,c.world,0,c.frame.body.pos,gate.budget());
        check(r.status==WordResult::Status::Deferred&&r.word.supported&&r.word.count==1&&r.word.useful_damage==40,"already completed useful Basic survives an interrupted stronger Area fragment");
        check(count(r.word,ActionKind::CastArea)==0&&gate.used(WordWork::AreaAim)==2,"a partially scanned aim optimiser is not published as a completed cast fragment");
        compatible(c,r.word);
        Gate stale;stale.finish=WordAdmission::Stale;
        const auto old=choose_word_bounded(c.frame,c.world,0,c.frame.body.pos,stale.budget());
        check(old.status==WordResult::Status::Stale&&!old.word.supported&&old.word.count==0,"Finish stale discards every earlier complete incumbent");
        Gate deferred;deferred.finish=WordAdmission::Defer;
        const auto ready=choose_word_bounded(c.frame,c.world,0,c.frame.body.pos,deferred.budget());
        check(ready.status==WordResult::Status::Deferred&&same(ready.word,choose_word(c.frame,c.world,0,c.frame.body.pos)),"same-epoch Finish Defer retains the fully completed word");
    }
    {
        auto c=base();c.frame.body.strength=0;c.frame.body.can_attack=false;c.frame.body.can_move=false;
        for(int i=0;i<32;++i)camp(c,{2+i%8,2+i/8},100);
        auto& s=c.frame.spells[0];s.kind=SpellKind::Area;s.ready=true;s.slot=0;s.target_camps=true;s.target_ground=true;
        s.range=4;s.radius=2;s.damage=70;s.exclusive=true;
        Gate complete;const auto full=choose_word_bounded(c.frame,c.world,0,c.frame.body.pos,complete.budget());
        check(full.status==WordResult::Status::Complete&&full.word.useful_damage==1400&&count(full.word,ActionKind::CastArea)==1,"maximum32-member Area leaf retains complete geometric coverage");
        check(complete.used(WordWork::AreaAim)==36,"each clipped in-map aim is separately admitted before its bounded32-member copy/hit/score leaf");
        Gate interrupted;interrupted.deny=WordWork::AreaAim;interrupted.deny_on=3;
        const auto partial=choose_word_bounded(c.frame,c.world,0,c.frame.body.pos,interrupted.budget());
        check(partial.status==WordResult::Status::Deferred&&partial.word.supported&&partial.word.count==0&&interrupted.used(WordWork::AreaAim)==3,"max-member interruption retains only the earlier complete empty prefix");
        c.frame.groups.push_back({0,Life::Reward});Gate malformed;
        const auto invalid=choose_word_bounded(c.frame,c.world,0,c.frame.body.pos,malformed.budget());
        check(invalid.status==WordResult::Status::Unsupported&&!invalid.word.supported&&malformed.used(WordWork::Consider)==0,"duplicate lifecycle records fail before cached absence can grant candidate authority");
    }
    {
        auto c=base();camp(c,{2,1},300);auto grid=std::make_shared<Grid>(*c.world.grid);
        grid->enemy_spawn.assign(24,Point{11,7});c.world.grid=grid;
        Gate occupancy;occupancy.deny=WordWork::MoveCheck;occupancy.deny_on=3;
        const auto result=choose_word_bounded(c.frame,c.world,0,occupancy.budget());
        check(result.status==WordResult::Status::Deferred&&result.word.supported&&result.word.useful_damage==40,"completed Basic incumbent survives a later partially checked movement candidate");
        check(count(result.word,ActionKind::Move)==0&&occupancy.used(WordWork::MoveCheck)==3,"flat occupancy scans re-admit each8-point chunk and never publish a partial move");
        Gate shifted;shifted.expected=11;
        const auto stale=choose_word_bounded(c.frame,c.world,0,c.frame.body.pos,shifted.budget());
        check(stale.status==WordResult::Status::Stale&&!stale.word.supported&&shifted.used(WordWork::Records)==0,"changed epoch at entry begins no search work");
    }
    std::cout<<"residual_words "<<checks<<" checks "<<failures<<" failures\n";
    return failures?1:0;
}
