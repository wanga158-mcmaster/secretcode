#include <cassert>
#include <tuple>
#include "../capital/src/policy.h"

namespace {
using namespace p11_capital;
using Key=std::tuple<int,int,std::int64_t,int>;
int scalar_hits(float hp,float damage) {
    if(hp<=0)return 0;
    if(!std::isfinite(hp)||!(damage>0))return 10000;
    if(!std::isfinite(damage))return 1;
    volatile float remaining=hp;
    for(int hits=1;hits<=10000;++hits) {
        const float next=remaining-damage;
        if(next<=0)return hits;
        if(next==remaining)return 10000;
        remaining=next;
    }
    return 10000;
}
Key key(const detail::Value&value) {return {-value.feasible,value.work,value.deficit,value.cost};}
bool same(Purchase a,Purchase b) {return a.project==b.project&&a.count==b.count;}

// Reconstruct the pre-repair evaluation independently: scalar float32 hits,
// stable full sort, sequential weighted exposure and a separate raw work sum.
detail::Value legacy_value(const Input&in,std::span<const Job>jobs,Purchase option,int cost) {
    const int nm=option.project==Project::monster?option.count:0;
    const int nj=option.project==Project::jungle?option.count:0;
    const float damage=(in.strength+nm*in.strength_growth)
        *(in.monster_count+nm>=in.monster_required?1+in.monster_bonus:1);
    const float maximum=in.maximum+nj*in.health_growth;
    const float mitigation=in.jungle_count+nj>=in.jungle_required?1-in.jungle_reduction:1;
    detail::Value result;result.cost=cost;
    for(std::size_t j=0;j<std::min(jobs.size(),std::size_t(3));++j) {
        const auto&job=jobs[j];if(job.count<=0||job.count>8)continue;
        struct Task {p11::Monster monster;int hits;};
        std::vector<Task> tasks;float incoming=0;
        for(int k=0;k<job.count;++k) {
            const auto&m=job.monsters[k];result.work+=scalar_hits(m.hp,damage);
            if(m.hp>0){tasks.push_back({m,scalar_hits(m.hp,damage)});incoming+=m.strength;}
        }
        std::stable_sort(tasks.begin(),tasks.end(),[](const Task&a,const Task&b) {
            return a.monster.strength*b.hits>b.monster.strength*a.hits;
        });
        int elapsed=0;float loss=0;
        for(const auto&task:tasks) {
            elapsed+=task.hits+task.monster.approach;
            loss+=task.monster.strength*std::max(0,elapsed-1+2)*mitigation;
        }
        loss+=incoming*job.count*mitigation;
        result.feasible+=loss<=.75f*maximum;
        const double missing=std::ceil(std::max(0.,double(loss)/.75-double(maximum)));
        constexpr auto cap=std::numeric_limits<std::int64_t>::max()/4;
        result.deficit+=missing>=double(cap)?cap:std::int64_t(missing);
    }
    return result;
}
std::vector<std::pair<Purchase,int>> options(const Input&in) {
    std::vector<std::pair<Purchase,int>> result{{{},0}};
    for(const auto project:{Project::monster,Project::jungle}) {
        const int owned=project==Project::monster?in.monster_count:in.jungle_count;
        const int required=project==Project::monster?in.monster_required:in.jungle_required;
        const int price=project==Project::monster?in.monster_price:in.jungle_price;
        for(int count=1;count<=5;++count)
            if(price>=0&&in.gold>=0&&owned+count<=required&&count*price<=in.gold)
                result.push_back({{project,count},count*price});
    }
    return result;
}
Purchase compare(const Input&in,std::span<const Job>jobs) {
    Purchase chosen;auto best=key(legacy_value(in,jobs,{},0));
    for(const auto&[option,cost]:options(in)) {
        const auto old=legacy_value(in,jobs,option,cost);
        const auto actual=detail::value(in,jobs,option,cost);
        assert(key(actual)==key(old)); // Every complete Value, not only winner.
        if(key(old)<best){best=key(old);chosen=option;}
    }
    assert(same(Policy::choose_capital(in,jobs),chosen));
    return chosen;
}
void original_cases() {
    // Same generator and consumption order as the existing8192-case fixture.
    std::uint32_t state=0x1c4a761bu;
    auto random=[&] {state^=state<<13;state^=state>>17;state^=state<<5;return state;};
    for(int trial=0;trial<8192;++trial) {
        Input in;in.strength=30+random()%171;in.maximum=50+random()%1451;
        in.strength_growth=random()%31;in.health_growth=random()%121;
        in.monster_count=random()%8;in.jungle_count=random()%8;
        in.monster_required=1+random()%7;in.jungle_required=1+random()%7;
        in.monster_price=10*(random()%6);in.jungle_price=10*(random()%6);
        in.monster_bonus=float(random()%5)/8;in.jungle_reduction=float(random()%5)/8;
        in.gold=random()%331;
        std::array<Job,3> jobs{};const int size=1+random()%3;
        for(int j=0;j<size;++j) {
            jobs[j].count=1+random()%8;
            for(int k=0;k<jobs[j].count;++k)
                jobs[j].monsters[k]={float(1+random()%1000),float(random()%61),int(random()%4)};
        }
        const auto view=std::span<const Job>(jobs.data(),size);
        const auto chosen=compare(in,view);
        for(const auto&[option,cost]:options(in)) {
            (void)cost;
            const int count=option.project==Project::monster?option.count:0;
            const float damage=(in.strength+count*in.strength_growth)
                *(in.monster_count+count>=in.monster_required?1+in.monster_bonus:1);
            for(const auto&job:view) {
                const auto monsters=std::span<const p11::Monster>(job.monsters.data(),job.count);
                const auto work=detail::work_from_clear(monsters,damage,p11::clear_schedule(monsters,damage));
                assert(work.reused&&work.attack_calls==0);
            }
        }
        std::reverse(jobs.begin(),jobs.begin()+size);
        assert(same(chosen,Policy::choose_capital(in,view)));
        // Existing capital.cpp consumes these for unchanged financial checks.
        (void)random();(void)random();
    }
}

void edges_and_fallback() {
    constexpr float damage=118.f*1.1f;
    std::array<p11::Monster,4> monsters{{{649,20,3},{600,10,1},{0,99,123},{-5,99,-123}}};
    auto clear=p11::clear_schedule(monsters,damage);
    auto work=detail::work_from_clear(monsters,damage,clear);
    assert(work.reused&&work.work==11&&work.attack_calls==0&&clear.turns==15);
    for(float value:{0.f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        work=detail::work_from_clear(monsters,value,p11::clear_schedule(monsters,value));
        int expected=0;for(const auto&m:monsters)expected+=scalar_hits(m.hp,value);
        assert(!work.reused&&work.work==expected&&work.attack_calls==4);
    }
    std::array<p11::Monster,1> nan{{{std::numeric_limits<float>::quiet_NaN(),20,0}}};
    work=detail::work_from_clear(nan,100,p11::clear_schedule(nan,100));
    assert(!work.reused&&work.work==10000&&work.attack_calls==1);
    auto negative=std::array<p11::Monster,1>{{{100,20,-1}}};
    work=detail::work_from_clear(negative,50,p11::clear_schedule(negative,50));
    assert(!work.reused&&work.work==2&&work.attack_calls==1);
    auto unsafe=negative;unsafe[0].approach=std::numeric_limits<int>::max();
    // Do not ask clear_schedule to perform an already-unsafe integer sum.
    work=detail::work_from_clear(unsafe,50,p11::Clear{});
    assert(!work.reused&&work.work==2&&work.attack_calls==1);
    auto unsupported=clear;unsupported.bounded=false;
    assert(!detail::work_from_clear(monsters,damage,unsupported).reused);
    auto malformed=clear;malformed.turns=0;
    assert(!detail::work_from_clear(monsters,damage,malformed).reused);

    Input in;in.strength=118;in.maximum=1000;in.monster_count=5;in.gold=150;
    in.strength_growth=6;in.health_growth=100;
    Job job;job.count=4;job.monsters[0]=monsters[0];job.monsters[1]=monsters[1];
    job.monsters[2]=monsters[2];job.monsters[3]=monsters[3];
    compare(in,std::span(&job,1));
    job.count=1;job.monsters[0]=nan[0];compare(in,std::span(&job,1));
    job.monsters[0]=negative[0];compare(in,std::span(&job,1));
    job.count=0;compare(in,std::span(&job,1));job.count=9;compare(in,std::span(&job,1));
}

void structural_reduction() {
    Input in;in.strength=100;in.maximum=1000;in.strength_growth=6;
    in.health_growth=100;in.gold=150;
    std::array<Job,3> jobs{};
    for(auto&job:jobs){job.count=8;for(auto&m:job.monsters)m={649,20,0};}
    int variants=0,legacy_calls=0,new_calls=0;
    for(const auto&[option,cost]:options(in)) {
        (void)cost;++variants;
        const int count=option.project==Project::monster?option.count:0;
        const float d=(in.strength+count*in.strength_growth)
            *(in.monster_count+count>=in.monster_required?1+in.monster_bonus:1);
        for(const auto&job:jobs) {
            const auto monsters=std::span<const p11::Monster>(job.monsters.data(),job.count);
            const auto work=detail::work_from_clear(monsters,d,p11::clear_schedule(monsters,d));
            assert(work.reused);
            legacy_calls+=2*job.count;new_calls+=job.count+work.attack_calls;
        }
    }
    assert(variants==11&&legacy_calls==528&&new_calls==264);
    compare(in,jobs);
}
}

int main() {
    original_cases();edges_and_fallback();structural_reduction();
}
