#include <cassert>
#include <tuple>
#include <type_traits>
#include "../capital/src/policy.h"

namespace {
using namespace p11_capital;
using Key=std::tuple<int,int,std::int64_t,int>;
Job single(float hp,float strength=0,int approach=0) {
    Job job;job.count=1;job.monsters[0]={hp,strength,approach};return job;
}
bool same(Purchase a,Purchase b) {return a.project==b.project&&a.count==b.count;}
Purchase choose(const Input&in,std::span<const Job> jobs) {return Policy::choose_capital(in,jobs);}

// Independent enumeration and schedule reconstruction. The oracle neither
// calls the production evaluator nor the common clear/exposure functions.
Key oracle_key(const Input&in,std::span<const Job> jobs,Purchase option) {
    const int nm=option.project==Project::monster?option.count:0;
    const int nj=option.project==Project::jungle?option.count:0;
    float damage=in.strength+nm*in.strength_growth;
    if(in.monster_count+nm>=in.monster_required)damage*=1+in.monster_bonus;
    const float maximum=in.maximum+nj*in.health_growth;
    const float reduction=in.jungle_count+nj>=in.jungle_required?1-in.jungle_reduction:1;
    int feasible=0,work=0;std::int64_t deficit=0;
    for(std::size_t j=0;j<std::min(jobs.size(),std::size_t(3));++j) {
        const auto&job=jobs[j];if(job.count<=0||job.count>8)continue;
        struct Task {p11::Monster monster;int hits;};
        std::vector<Task> tasks;
        float incoming=0;
        for(int k=0;k<job.count;++k) {
            const auto&m=job.monsters[k];if(m.hp<=0)continue;
            int hits=0;volatile float hp=m.hp;
            while(hits<10000&&hp>0) {
                const float next=hp-damage;++hits;
                if(next==hp) {hits=10000;break;}
                hp=next;
            }
            tasks.push_back({m,hits});work+=hits;incoming+=m.strength;
        }
        std::stable_sort(tasks.begin(),tasks.end(),[](const Task&a,const Task&b) {
            return a.monster.strength*b.hits>b.monster.strength*a.hits;
        });
        float loss=0;int elapsed=0;
        for(const auto&t:tasks) {
            elapsed+=t.hits+t.monster.approach;
            loss+=t.monster.strength*std::max(0,elapsed+1)*reduction;
        }
        loss+=incoming*job.count*reduction;
        feasible+=loss<=.75f*maximum;
        deficit+=std::int64_t(std::ceil(std::max(0.,double(loss)/.75-double(maximum))));
    }
    return {-feasible,work,deficit,nm*in.monster_price+nj*in.jungle_price};
}
Purchase oracle(const Input&in,std::span<const Job> jobs) {
    std::vector<Purchase> options{{Project::none,0}};
    for(int n=1;n<=5;++n)
        if(in.monster_count+n<=in.monster_required&&in.monster_price>=0
            &&n*in.monster_price<=in.gold)options.push_back({Project::monster,n});
    for(int n=1;n<=5;++n)
        if(in.jungle_count+n<=in.jungle_required&&in.jungle_price>=0
            &&n*in.jungle_price<=in.gold)options.push_back({Project::jungle,n});
    auto best=options.front();auto key=oracle_key(in,jobs,best);
    for(const auto option:options) {
        const auto next=oracle_key(in,jobs,option);
        if(next<key){best=option;key=next;}
    }
    return best;
}
template<class T> constexpr bool has_current_health=requires(T value){value.hp;};
}

int main() {
    using namespace p11_capital;
    static_assert(std::is_base_of_v<p11_tempo::Policy,Policy>);
    static_assert(!has_current_health<Input>);
    static_assert(Policy::bounded_scan&&Policy::trace_scan&&Policy::capital_shop&&Policy::window_size==3);
    Input in;in.strength=100;in.maximum=1000;in.strength_growth=10;in.health_growth=100;in.gold=30;
    std::array<Job,1> jobs{single(200)};
    assert(same(choose(in,jobs),{})); // More raw damage on the same integer-hit plateau buys nothing.
    jobs[0]=single(240);in.gold=60;
    assert(same(choose(in,jobs),{Project::monster,2})); // The cheapest bundle crossing a hit threshold.
    in.gold=59;assert(same(choose(in,jobs),{}));
    in.gold=60;in.monster_price=20;assert(same(choose(in,jobs),{Project::monster,2}));

    in=Input{};in.strength=100;in.maximum=100;in.strength_growth=25;in.health_growth=100;in.gold=30;
    jobs[0]=single(250,20);
    assert(same(choose(in,jobs),{Project::jungle,1})); // Health unlocks this job; faster damage does not.
    const auto health_choice=choose(in,jobs);
    p11::State field;field.hp=25;field.maximum=in.maximum+health_choice.count*in.health_growth;
    p11::Candidate camp;camp.loss=100;camp.clear=3;
    assert(!p11::feasible(field,camp)&&Policy::retreat(field.hp,field.maximum,20));
    field.hp=field.maximum;assert(p11::feasible(field,camp)); // Capacity is for a future full-health trip.

    in=Input{};in.strength=100;in.maximum=1000;in.monster_count=4;in.gold=30;
    jobs[0]=single(110);
    assert(same(choose(in,jobs),{Project::monster,1})); // Fifth shard's queried passive crosses the hit plateau.
    in.monster_count=3;assert(same(choose(in,jobs),{}));
    in.gold=60;assert(same(choose(in,jobs),{Project::monster,2}));
    in.monster_required=4;in.gold=30;assert(same(choose(in,jobs),{Project::monster,1}));
    in.monster_bonus=0;assert(same(choose(in,jobs),{}));

    in=Input{};in.strength=100;in.maximum=100;in.jungle_count=4;in.gold=30;
    jobs[0]=single(200,25);
    assert(same(choose(in,jobs),{Project::jungle,1})); // Jungle completion, without raw Health growth, unlocks safety.
    in.jungle_count=3;assert(same(choose(in,jobs),{}));
    in.gold=60;assert(same(choose(in,jobs),{Project::jungle,2}));
    in.jungle_reduction=0;assert(same(choose(in,jobs),{}));
    in.gold=0;assert(same(choose(in,jobs),{}));
    in.gold=300;in.monster_count=in.jungle_count=5;assert(same(choose(in,jobs),{}));
    assert(same(choose(in,{}),{}));

    // No hidden scalar preference: feasible count outranks DPS, then saved hits
    // outrank HP deficit; cost breaks otherwise identical project improvements.
    in=Input{};in.strength=100;in.maximum=100;in.strength_growth=50;in.health_growth=100;in.gold=30;
    std::array<Job,2> tradeoff{single(250,20),single(100,30)};
    assert(same(choose(in,tradeoff),{Project::jungle,1}));
    in.maximum=1000;in.gold=150;tradeoff={single(250),single(120)};
    assert(same(choose(in,tradeoff),{Project::monster,3}));

    // Work, safety and selection are unchanged by a representative order swap.
    std::uint32_t state=0x1c4a761bu;
    auto random=[&]() {state^=state<<13;state^=state>>17;state^=state<<5;return state;};
    for(int trial=0;trial<8192;++trial) {
        Input sample;sample.strength=30+random()%171;sample.maximum=50+random()%1451;
        sample.strength_growth=random()%31;sample.health_growth=random()%121;
        sample.monster_count=random()%8;sample.jungle_count=random()%8;
        sample.monster_required=1+random()%7;sample.jungle_required=1+random()%7;
        sample.monster_price=10*(random()%6);sample.jungle_price=10*(random()%6);
        sample.monster_bonus=float(random()%5)/8;sample.jungle_reduction=float(random()%5)/8;
        sample.gold=random()%331;
        std::array<Job,3> representatives{};const int size=1+random()%3;
        for(int j=0;j<size;++j) {
            representatives[j].count=1+random()%8;
            for(int k=0;k<representatives[j].count;++k)
                representatives[j].monsters[k]={float(1+random()%1000),float(random()%61),int(random()%4)};
        }
        const auto view=std::span<const Job>(representatives.data(),size);
        const auto actual=choose(sample,view);
        assert(same(actual,oracle(sample,view)));
        assert(actual.count>=0&&actual.count<=5);
        const int price=actual.project==Project::monster?sample.monster_price:sample.jungle_price;
        assert(actual.count*price<=sample.gold);
        if(actual.project!=Project::none)assert(oracle_key(sample,view,actual)<oracle_key(sample,view,{}));
        std::reverse(representatives.begin(),representatives.begin()+size);
        assert(same(actual,choose(sample,view)));
        p11::State s;s.hp=50;s.maximum=1000;s.round=int(random()%2000);s.bag=random()%50;
        p11::Candidate c;c.id=trial%7;c.gold=30;c.gems=20;c.travel=3;c.clear=5;c.altar=8;
        assert(Policy::score(s,c)==p11_tempo::Policy::score(s,c));
        assert(Policy::bank(s)==p11_tempo::Policy::bank(s));
        assert(Policy::retreat(s.hp,s.maximum,20)==p11_tempo::Policy::retreat(s.hp,s.maximum,20));
    }
    // API bounds are enforced even if a caller supplies extra or malformed jobs.
    in=Input{};in.strength=100;in.maximum=1000;in.strength_growth=10;in.gold=60;
    std::array<Job,4> bounded{single(200),single(200),single(200),single(240)};
    assert(same(choose(in,bounded),{}));
    bounded[0].count=9;bounded[1].count=0;bounded[2].count=-1;
    assert(same(choose(in,bounded),{}));
}
