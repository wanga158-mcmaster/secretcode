#include "residual_words.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace residual {
namespace {
constexpr std::size_t max_members=32,max_camps=256;
constexpr int dx[8]={0,1,1,1,0,-1,-1,-1},dy[8]={-1,-1,0,1,1,1,0,-1};
bool number(float x) noexcept {return std::isfinite(x)&&x>=0;}
std::int64_t dist(Point a,Point b) noexcept {
    const auto x=static_cast<std::int64_t>(a.x)-b.x,y=static_cast<std::int64_t>(a.y)-b.y;
    return std::max(x<0?-x:x,y<0?-y:y);
}
bool inside(const Grid& g,Point p) noexcept {return p.x>=0&&p.y>=0&&p.x<g.width&&p.y<g.height;}
float upper_float(double x) noexcept {
    if(x>std::numeric_limits<float>::max())return std::numeric_limits<float>::infinity();
    float f=static_cast<float>(x);
    if(static_cast<double>(f)<x)f=std::nextafter(f,std::numeric_limits<float>::infinity());
    return f;
}
struct State {
    std::array<float,max_members> hp{},shield{};
    std::array<int,max_members> stun{};
    Point pos;Word word;
    float heal{},own_shield{};int next_stun{};
    bool move{},attack{},cast{},walked{},exclusive{};
};
class Search {
public:
    Search(const Frame& f,const World& w,int group,const Point* final,WordBudget budget) noexcept
        :f_(f),w_(w),group_(group),constrained_(final!=nullptr),final_(final?*final:Point{}),budget_(budget) {}
    WordResult run() noexcept {
        const bool prepared=prepare();
        if(prepared)visit(initial_);
        // Even after a low-fuel Defer, run the final freshness check. Defer is
        // not permission to forget that a completed incumbent may now be old.
        if(budget_.admit){const auto final=budget_.admit(budget_.context,f_.epoch,WordWork::Finish);
            if(final==WordAdmission::Stale)stop_=WordAdmission::Stale;
            else if(final==WordAdmission::Defer&&stop_==WordAdmission::Admit)stop_=WordAdmission::Defer;
            else if(final!=WordAdmission::Admit&&final!=WordAdmission::Defer)stop_=WordAdmission::Stale;
        }
        if(stop_==WordAdmission::Stale)return {{},WordResult::Status::Stale};
        if(stop_==WordAdmission::Defer)return {have_best_?best_:Word{},WordResult::Status::Deferred};
        if(!prepared||!have_best_)return {};
        return {best_,WordResult::Status::Complete};
    }
private:
    bool admit(WordWork work) noexcept {
        if(stop_!=WordAdmission::Admit)return false;
        if(budget_.admit)stop_=budget_.admit(budget_.context,f_.epoch,work);
        if(stop_!=WordAdmission::Admit&&stop_!=WordAdmission::Defer)stop_=WordAdmission::Stale;
        return stop_==WordAdmission::Admit;
    }
    bool absent_safe(int group) const noexcept {
        if(group<0||static_cast<std::size_t>(group)>=w_.groups.size())return false;
        return absent_[static_cast<std::size_t>(group)];
    }
    bool prepare() noexcept {
        if(!f_.ready||!f_.alive||f_.epoch<0||f_.submitting||f_.recalling||f_.channel_remaining>0||
            !w_.grid||group_<0||static_cast<std::size_t>(group_)>=w_.groups.size()||
            w_.camps.size()>max_camps||f_.camps.size()>max_camps||w_.groups.size()>64||f_.groups.size()>64||
            f_.allies.size()>64||f_.foes.size()>64)return false;
        const auto& g=*w_.grid;
        if(g.width<=0||g.height<=0||!inside(g,f_.body.pos)||g.enemy_spawn.size()>4096||g.altars.size()>4096)return false;
        const auto width=static_cast<std::size_t>(g.width),height=static_cast<std::size_t>(g.height);
        if(width>std::numeric_limits<std::size_t>::max()/height||g.blocked.size()!=width*height)return false;
        const auto& b=f_.body;
        if(!number(b.hp)||b.hp<=0||!number(b.max_hp)||b.max_hp<b.hp||!number(b.strength)||
            !number(b.shield)||!number(b.incoming_scale)||!number(b.splash_scale)||b.range<0||b.range>8||
            b.splash_radius<0||b.splash_radius>4||b.next_basic_stun<0)return false;
        reading_.fill(-1);local_.fill(-1);
        for(std::size_t i=0;i<w_.camps.size();++i){const auto& c=w_.camps[i];
            if(i%8==0&&!admit(WordWork::Records))return false;
            if(!inside(g,c.pos)||!number(c.max_hp)||!number(c.strength)||c.range<0||c.range>10000||
                c.group<0||static_cast<std::size_t>(c.group)>=w_.groups.size())return false;
        }
        for(std::size_t i=0;i<f_.camps.size();++i){const auto& c=f_.camps[i];
            if(i%8==0&&!admit(WordWork::Records))return false;
            if(c.index<0||static_cast<std::size_t>(c.index)>=w_.camps.size()||reading_[c.index]>=0||
                c.pos!=w_.camps[c.index].pos||!number(c.hp)||!number(c.shield)||c.stun<0||
                (!c.alive&&c.hp>0)|| (c.alive&&c.hp<=0))return false;
            reading_[c.index]=static_cast<int>(i);
        }
        for(std::size_t i=0;i<f_.groups.size();++i){const auto& r=f_.groups[i];
            if(!admit(WordWork::Records))return false;
            if(r.index<0||static_cast<std::size_t>(r.index)>=w_.groups.size())return false;
            for(std::size_t j=0;j<i;++j)if(f_.groups[j].index==r.index)return false;
            // Identical to the old first-matching scan, after duplicate/shape
            // validation. Makes each later hit/score leaf truly O(members).
            absent_[static_cast<std::size_t>(r.index)]=r.life==Life::Respawning?r.respawn>1:
                (r.life==Life::Living||r.life==Life::Reward)&&w_.groups[static_cast<std::size_t>(r.index)].respawn>1;
        }
        const auto& members=w_.groups[group_].members;n_=members.size();
        if(n_==0||n_>max_members)return false;
        for(std::size_t j=0;j<n_;++j){const int i=members[j];
            if(j%8==0&&!admit(WordWork::Records))return false;
            if(i<0||static_cast<std::size_t>(i)>=w_.camps.size()||local_[i]>=0||w_.camps[i].group!=group_)return false;
            member_[j]=i;local_[i]=static_cast<int>(j);
            if(reading_[i]>=0){const auto& r=f_.camps[reading_[i]];
                visible_[j]=r.alive;initial_.hp[j]=r.hp;initial_.shield[j]=r.shield;initial_.stun[j]=r.stun;
            }
        }
        for(std::size_t i=0;i<2;++i){const auto& s=f_.spells[i];
            if(s.kind!=SpellKind::None&&s.slot>1)return false;
            if(i&&s.kind!=SpellKind::None&&f_.spells[0].kind!=SpellKind::None&&s.slot==f_.spells[0].slot)return false;
        }
        initial_.pos=b.pos;initial_.move=b.can_move;initial_.attack=b.can_attack;initial_.cast=b.can_cast;
        initial_.walked=b.walked;initial_.next_stun=b.enhanced?std::max(1,b.next_basic_stun):b.next_basic_stun;
        initial_.own_shield=b.shield_next_batch?b.shield:0;
        // Preserve every source at each REACHABLE pose. With no second ordinary
        // move, a current-pose constraint admits only the origin; an adjacent
        // constraint admits origin+destination. Unconstrained search retains
        // the old nine-pose order exactly, including external catalogue danger.
        pose_[0]=b.pos;pose_count_=1;
        if(!constrained_){for(unsigned p=0;p<8;++p)pose_[pose_count_++]={b.pos.x+dx[p],b.pos.y+dy[p]};}
        else if(final_!=b.pos){if(dist(final_,b.pos)!=1)return false;pose_[pose_count_++]=final_;}
        for(unsigned p=0;p<pose_count_;++p){
            double total=0;
            for(std::size_t i=0;i<w_.camps.size();++i){
                if(i%8==0&&!admit(WordWork::External))return false;
                if(local_[i]>=0)continue;const auto& c=w_.camps[i];
                if(dist(c.pos,pose_[p])>c.range)continue;
                const int ri=reading_[i];
                if(ri>=0){const auto& r=f_.camps[ri];
                    if((!r.alive||r.hp<=0)&&absent_safe(c.group))continue;
                    if(r.alive&&r.stun>0)continue;
                }
                total+=static_cast<float>(c.strength*b.incoming_scale);
            }
            external_[p]=total;
        }
        return true;
    }
    unsigned pose_index(Point p) const noexcept {for(unsigned i=0;i<pose_count_;++i)if(p==pose_[i])return i;return 0;}
    float incoming(const State& s) const noexcept {
        double total=external_[pose_index(s.pos)];
        for(std::size_t j=0;j<n_;++j){const auto& c=w_.camps[member_[j]];
            if(dist(c.pos,s.pos)>c.range)continue;
            if(visible_[j]&&s.hp[j]>0&&s.stun[j]>0)continue;
            if(reading_[member_[j]]>=0&&s.hp[j]<=0&&absent_safe(c.group))continue;
            total+=static_cast<float>(c.strength*f_.body.incoming_scale);
        }
        return upper_float(total);
    }
    float progress(const State& s) const noexcept {
        float total=0;for(std::size_t j=0;j<n_;++j)if(visible_[j])total+=initial_.hp[j]-s.hp[j];return total;
    }
    float loss(const State& s) const noexcept {return std::max(0.0f,incoming(s)-s.own_shield);}
    double value(const State& s) const noexcept {return static_cast<double>(progress(s))+s.heal-loss(s);}
    void consider(const State& s) noexcept {
        if(constrained_&&s.pos!=final_)return;
        const float hurt=loss(s);const bool survives=hurt<f_.body.hp+s.heal;
        const double v=value(s);const float damage=progress(s),raw=incoming(s);
        if(have_best_){
            if(survives!=best_survives_){if(!survives)return;}
            else if(v!=best_value_){if(v<best_value_)return;}
            else if(damage!=best_.useful_damage){if(damage<best_.useful_damage)return;}
            else if(raw!=best_.incoming_upper){if(raw>best_.incoming_upper)return;}
            else if(s.word.count>=best_.count)return;
        }
        best_=s.word;best_.supported=true;best_.useful_damage=damage;best_.incoming_upper=raw;
        best_.own_healing=s.heal;best_.shield_after=std::max(0.0f,s.own_shield-raw);
        best_.own_damage_upper=hurt;
        best_value_=v;best_survives_=survives;have_best_=true;
    }
    void hit(State& s,std::size_t j,float damage,int stun) const noexcept {
        if(!visible_[j]||s.hp[j]<=0)return;
        const float absorbed=std::min(s.shield[j],damage);s.shield[j]-=absorbed;damage-=absorbed;
        s.hp[j]=std::max(0.0f,s.hp[j]-damage);
        if(s.hp[j]>0)s.stun[j]=std::max(s.stun[j],stun);
    }
    bool better_fragment(const State& s,const State& candidate,double& best_value,float& best_damage) const noexcept {
        const double v=value(candidate);const float d=progress(candidate);
        if(v>best_value||(v==best_value&&d>best_damage)){best_value=v;best_damage=d;return true;}
        (void)s;return false;
    }
    bool basic(State& s) noexcept {
        if(!s.attack||f_.body.strength<=0)return false;
        bool found=false;double best=-std::numeric_limits<double>::infinity();float damage=-1;std::size_t target=0;
        // Greedy PRIMARY selection still computes the complete splash footprint.
        for(std::size_t j=0;j<n_;++j){if(!visible_[j]||s.hp[j]<=0||dist(s.pos,w_.camps[member_[j]].pos)>f_.body.range)continue;
            if(!admit(WordWork::BasicTarget))return false;
            State q=s;hit(q,j,f_.body.strength,s.next_stun);
            if(f_.body.splash&&f_.body.splash_scale>0){const auto center=w_.camps[member_[j]].pos;
                for(std::size_t k=0;k<n_;++k)if(k!=j&&dist(center,w_.camps[member_[k]].pos)<=f_.body.splash_radius)
                    hit(q,k,static_cast<float>(f_.body.strength*f_.body.splash_scale),0);
            }
            if(better_fragment(s,q,best,damage)){target=j;found=true;}
        }
        if(!found)return false;
        hit(s,target,f_.body.strength,s.next_stun);
        if(f_.body.splash&&f_.body.splash_scale>0){const auto center=w_.camps[member_[target]].pos;
            for(std::size_t k=0;k<n_;++k)if(k!=target&&dist(center,w_.camps[member_[k]].pos)<=f_.body.splash_radius)
                hit(s,k,static_cast<float>(f_.body.strength*f_.body.splash_scale),0);
        }
        s.word.actions[s.word.count++]={ActionKind::Basic,0,member_[target],{}};
        s.next_stun=0;s.attack=false;if(f_.body.ranged)s.move=false;return true;
    }
    bool cast(State& s,const Spell& spell) noexcept {
        if(!s.cast||!spell.ready||spell.cooldown||spell.kind==SpellKind::None||spell.kind==SpellKind::Dash||
            (spell.exclusive&&s.walked)||spell.range<0||spell.range>4||spell.radius<0||spell.radius>2||
            spell.stun<0||!number(spell.damage)||!number(spell.shield)||!number(spell.heal))return false;
        Action action;action.slot=spell.slot;action.camp=-1;action.destination=s.pos;
        if(spell.kind==SpellKind::Reset){
            if(!spell.target_self||s.attack)return false;
            s.attack=true;if(spell.enhanced)s.next_stun=std::max(s.next_stun,std::max(1,spell.stun));
            action.kind=ActionKind::Reset;
        }else if(spell.kind==SpellKind::Shield){
            if(!spell.target_self||spell.shield<=0||spell.duration<1)return false;
            s.own_shield+=spell.shield;action.kind=ActionKind::Shield;
        }else if(spell.kind==SpellKind::Heal){
            if(!spell.target_self||spell.heal<=0)return false;
            const float amount=std::min(spell.heal,f_.body.max_hp-f_.body.hp-s.heal);
            if(amount<=0)return false;s.heal+=amount;action.kind=ActionKind::Heal;
        }else if(spell.kind==SpellKind::Targeted||spell.kind==SpellKind::Area){
            if(!spell.target_camps||(spell.kind==SpellKind::Area&&!spell.target_ground))return false;
            bool found=false;double best=-std::numeric_limits<double>::infinity();float damage=-1;Point aim;std::size_t target=0;
            auto test=[&](Point p,std::size_t primary){
                if(!admit(spell.kind==SpellKind::Targeted?WordWork::CastTarget:WordWork::AreaAim))return false;
                State q=s;bool affected=false;
                for(std::size_t j=0;j<n_;++j){const bool covered=spell.kind==SpellKind::Targeted?j==primary:dist(p,w_.camps[member_[j]].pos)<=spell.radius;
                    if(covered&&visible_[j]&&q.hp[j]>0){hit(q,j,spell.damage,spell.stun);affected=true;}}
                if(affected&&better_fragment(s,q,best,damage)){found=true;aim=p;target=primary;}
                return true;
            };
            if(spell.kind==SpellKind::Targeted){
                for(std::size_t j=0;j<n_;++j)if(visible_[j]&&s.hp[j]>0&&dist(s.pos,w_.camps[member_[j]].pos)<=spell.range)
                    if(!test(w_.camps[member_[j]].pos,j))return false;
            }else{
                const auto& g=*w_.grid;
                const int y0=static_cast<int>(std::max<std::int64_t>(0,static_cast<std::int64_t>(s.pos.y)-spell.range));
                const int y1=static_cast<int>(std::min<std::int64_t>(g.height-1,static_cast<std::int64_t>(s.pos.y)+spell.range));
                const int x0=static_cast<int>(std::max<std::int64_t>(0,static_cast<std::int64_t>(s.pos.x)-spell.range));
                const int x1=static_cast<int>(std::min<std::int64_t>(g.width-1,static_cast<std::int64_t>(s.pos.x)+spell.range));
                for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)if(!test({x,y},0))return false;
            }
            if(!found)return false;
            for(std::size_t j=0;j<n_;++j)if(spell.kind==SpellKind::Targeted?j==target:dist(aim,w_.camps[member_[j]].pos)<=spell.radius)
                hit(s,j,spell.damage,spell.stun);
            action.kind=spell.kind==SpellKind::Targeted?ActionKind::CastTargeted:ActionKind::CastArea;
            action.destination=aim;if(spell.kind==SpellKind::Targeted)action.camp=member_[target];
        }else return false;
        s.word.actions[s.word.count++]=action;s.cast=false;
        if(spell.exclusive){s.exclusive=true;s.move=false;}return true;
    }
    bool move(State& s,Point p) noexcept {
        if(!s.move||s.exclusive||dist(s.pos,p)!=1||!inside(*w_.grid,p))return false;
        if(!admit(WordWork::MoveCheck))return false;
        const auto& g=*w_.grid;const auto index=static_cast<std::size_t>(p.y)*g.width+p.x;
        if(g.blocked[index])return false;
        for(std::size_t i=0;i<g.enemy_spawn.size();++i){if(i%8==0&&!admit(WordWork::MoveCheck))return false;if(g.enemy_spawn[i]==p)return false;}
        for(std::size_t i=0;i<g.altars.size();++i){if(i%8==0&&!admit(WordWork::MoveCheck))return false;if(g.altars[i]==p)return false;}
        for(std::size_t i=0;i<f_.allies.size();++i){if(i%8==0&&!admit(WordWork::MoveCheck))return false;if(f_.allies[i]==p)return false;}
        for(std::size_t i=0;i<f_.foes.size();++i){if(i%8==0&&!admit(WordWork::MoveCheck))return false;if(f_.foes[i].pos==p)return false;}
        for(std::size_t i=0;i<w_.camps.size();++i){
            if(i%8==0&&!admit(WordWork::MoveCheck))return false;
            if(w_.camps[i].pos==p){const int j=local_[i];
                if(j>=0){if(reading_[i]<0||s.hp[j]>0)return false;}
                else if(reading_[i]<0||f_.camps[reading_[i]].alive)return false;
            }
        }
        s.word.actions[s.word.count++]={ActionKind::Move,0,-1,p};s.pos=p;s.move=false;s.walked=true;
        if(f_.body.ranged)s.attack=false;return true;
    }
    void visit(const State& s) noexcept {
        if(!admit(WordWork::Consider))return;
        consider(s);if(s.word.count==s.word.actions.size())return;
        State q=s;if(basic(q))visit(q);
        if(stop_!=WordAdmission::Admit)return;
        if(s.cast)for(const auto& spell:f_.spells){if(stop_!=WordAdmission::Admit)return;q=s;if(cast(q,spell))visit(q);}
        if(stop_!=WordAdmission::Admit)return;
        if(s.move&&!s.exclusive)for(unsigned d=0;d<8;++d){q=s;
            const Point p{f_.body.pos.x+dx[d],f_.body.pos.y+dy[d]};
            if(constrained_&&p!=final_)continue;
            if(move(q,p))visit(q);
            if(stop_!=WordAdmission::Admit)return;
        }
    }
    const Frame& f_;const World& w_;int group_;bool constrained_;Point final_;WordBudget budget_;
    WordAdmission stop_{WordAdmission::Admit};
    std::array<int,max_camps> reading_{},local_{};
    std::array<int,max_members> member_{};std::array<bool,max_members> visible_{};
    std::array<bool,64> absent_{};
    std::array<Point,9> pose_{};std::array<double,9> external_{};std::size_t n_{};unsigned pose_count_{};
    State initial_;Word best_;double best_value_{};bool have_best_{},best_survives_{};
};
}
WordResult choose_word_bounded(const Frame& f,const World& w,int group,WordBudget budget) noexcept {
    if(budget.admit){const auto admitted=budget.admit(budget.context,f.epoch,WordWork::Enter);
        if(admitted!=WordAdmission::Admit)return {{},admitted==WordAdmission::Defer?WordResult::Status::Deferred:WordResult::Status::Stale};}
    return Search(f,w,group,nullptr,budget).run();
}
WordResult choose_word_bounded(const Frame& f,const World& w,int group,Point final,WordBudget budget) noexcept {
    if(budget.admit){const auto admitted=budget.admit(budget.context,f.epoch,WordWork::Enter);
        if(admitted!=WordAdmission::Admit)return {{},admitted==WordAdmission::Defer?WordResult::Status::Deferred:WordResult::Status::Stale};}
    return Search(f,w,group,&final,budget).run();
}
Word choose_word(const Frame& f,const World& w,int group) noexcept {return choose_word_bounded(f,w,group,{}).word;}
Word choose_word(const Frame& f,const World& w,int group,Point final) noexcept {return choose_word_bounded(f,w,group,final,{}).word;}
}
