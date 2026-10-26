#include "../active-surveyor/src/survey.h"
#include "../active-surveyor/src/capture.h"
#include "../active-surveyor/src/observation.h"
#include "../active-surveyor/src/frontier.h"
#include "../active-surveyor/src/route-cycle.h"
#include "../active-surveyor/src/route-step.h"
#include <cassert>
#include <string>
#include <vector>

namespace survey=h12_active_surveyor;

// Mutable private-perspective host. Predictions expose terrain coverage only;
// place changes the owned ward and refresh obtains a new live observation.
// Source choice always comes from the production Survey::tick and Policy.
struct World {
    struct Source {
        int id=0,gems=10,gold=0,travel=2,public_work=8,hp=80;
        bool visible=true,ready=false,respawning=false,affordable=true;
    };
    std::vector<Source> sources{{0,10,0,2,8,80,true,false,false,true},
                                {1,12,0,3,30,10,false,false,false,true}};
    int turn=20,ward_x=-4,ward_y=0,cooldown=0,available_fuel=100000;
    int predicted=0,placements=0,refreshes=0,walks=0,hits=0,holds=0,captures=0;
    int cargo=0,points=0,gold=0,submits=0,channel_ticks=0,bank_travel=3;
    int worked=-1,reveal_id=-1,capture_hold=0;
    int stale_delta=0,after_prediction_fuel=-1;
    int prediction_shift_round=0;
    bool living=true,external_channel=false,blocked=false,reveal=true;
    bool replace_old=false,charge=true,predicts=true,new_fact=true;
    bool incomplete_sample=false;
    double lost_cost=0,probe_cost=.01;
    std::vector<std::string> events;

    bool alive()const{return living;}
    int current_round()const{return turn;}
    bool channeling()const{return external_channel||channel_ticks>0;}
    bool unknown(int id)const {
        for(const auto&s:sources)if(s.id==id)return !s.visible;
        return false;
    }
    bool ready()const{return charge&&cooldown==0;}
    int fuel()const{return available_fuel;}
    heuristics::State state()const {
        heuristics::State s;
        s.round=turn;s.identity=7;s.bag=cargo;s.gold=gold;s.altar=bank_travel;
        s.mature=true;s.monster=s.jungle=5;
        return s;
    }
    std::vector<heuristics::Job> observations()const {
        std::vector<heuristics::Job> jobs;
        for(const auto&s:sources) {
            if(s.visible&&s.respawning)continue;
            heuristics::Job j;
            j.id=s.id;j.gems=s.gems;j.gold=s.gold;j.travel=s.travel;
            j.known=s.visible;j.ready=s.visible&&s.ready;
            j.work=j.ready?0:(s.visible?s.hp/10.f:float(s.public_work));
            j.remaining_hp=s.visible?float(s.hp):float(s.public_work*10);
            j.original_hp=float(s.public_work*10);j.affordable=s.affordable;
            jobs.push_back(j);
        }
        return jobs;
    }
    std::array<survey::Probe,2> probes(int candidate,int before,int other) {
        assert(before!=other);
        ++predicted;reveal_id=candidate;
        turn+=prediction_shift_round;
        if(after_prediction_fuel>=0)available_fuel=after_prediction_fuel;
        return {{{0,2,1,lost_cost,probe_cost,predicts},
                  {1,1,2,lost_cost+.1,probe_cost,predicts}}};
    }
    bool place(const survey::Probe&p) {
        ++placements;
        if(blocked)return false;
        assert(ready());ward_x=p.x;ward_y=p.y;cooldown=10;
        if(replace_old)for(auto&s:sources)s.visible=false;
        if(reveal)for(auto&s:sources)if(s.id==reveal_id)s.visible=true;
        return true;
    }
    survey::Fresh refresh(int before,int other) {
        assert(before!=other);++refreshes;
        return {observations(),turn+stale_delta,new_fact&&reveal,!incomplete_sample};
    }
    void event(const char*name,int before,int after,int,int) {
        events.push_back(std::string(name)+":"+std::to_string(before)+":"+std::to_string(after));
    }
    bool saw(const std::string&prefix)const {
        for(const auto&e:events)if(e.starts_with(prefix))return true;
        return false;
    }
    void work(int id) {
        assert(living&&!channeling());
        for(auto&s:sources)if(s.id==id) {
            assert(!s.respawning);
            worked=id;
            if(s.travel>0){--s.travel;++walks;capture_hold=0;return;}
            if(s.hp>0) {
                s.hp=std::max(0,s.hp-10);++hits;capture_hold=0;
                if(s.hp==0)s.ready=true;
                return;
            }
            ++holds;
            if(++capture_hold>=5) {
                cargo+=s.gems;gold+=s.gold;s.respawning=true;s.ready=false;
                ++captures;capture_hold=0;
            }
            return;
        }
        assert(id<0);
    }
    void next() {
        ++turn;
        if(cooldown>0)--cooldown;
        if(channel_ticks>0&&--channel_ticks==0) {
            points+=cargo;
            if(state().identity==7)gold+=2*cargo;
            cargo=0;
        }
    }
    void step(survey::Survey&controller,survey::Policy&policy) {
        if(!channeling()) {
            auto s=state();
            if(policy.bank(s)) {
                if(bank_travel>0){--bank_travel;++walks;}
                else {channel_ticks=2;++submits;}
            } else {
                auto jobs=observations();
                const int choice=controller.tick(*this,policy,s,jobs);
                if(choice>=0)work(choice);
            }
        }
        next();
    }
};

int main() {
    survey::Policy policy;
    World w;survey::Survey controller;
    auto jobs=w.observations();const auto stale=jobs;
    assert(heuristics::choose(policy,w.state(),jobs)==0);
    const int selected=controller.tick(w,policy,w.state(),jobs);
    assert(selected==1&&controller.changed==1&&controller.attempts==1);
    assert(w.placements==1&&w.refreshes==1&&w.predicted==1);
    assert(w.ward_x==2&&w.ward_y==1&&w.cooldown==10);
    assert(!stale[1].known&&!stale[1].ready&&stale[1].work==30);
    bool fresh_injury=false;
    for(const auto&j:jobs)if(j.id==1)fresh_injury=j.known&&!j.ready&&j.work==1;
    assert(fresh_injury&&w.saw("probe:0:1")&&w.saw("changed:0:1"));
    w.work(selected);w.next();
    assert(w.worked==1&&w.walks==1&&w.cargo==0&&w.points==0);
    for(int i=0;i<30&&w.points==0;++i)w.step(controller,policy);
    assert(w.hits==1&&w.captures==1&&w.holds==5&&w.points==12&&w.cargo==0);
    assert(w.submits==1&&w.gold==24&&w.channel_ticks==0);
    assert(controller.attempts==1); // Work and conversion do not buy scouts.

    // A ward is refused when even an optimistic reveal cannot alter work.
    World no_value;no_value.sources[1].gems=0;
    survey::Survey no_probe;jobs=no_value.observations();
    const int fallback=no_probe.tick(no_value,policy,no_value.state(),jobs);
    no_value.work(fallback);
    assert(fallback==0&&no_value.walks==1&&no_value.placements==0);
    assert(no_probe.refused==1&&no_value.predicted==0);

    // Replacement loses an old useful live fact. Its authoritative fresh
    // shortlist must replace the owning cache, including jobs outside pair.
    World replacement;
    replacement.sources.push_back({2,1,0,1,90,0,true,true,false,true});
    replacement.replace_old=true;replacement.sources[0].hp=0;
    replacement.sources[0].ready=true;
    replacement.sources[1].gems=30;
    survey::Survey replaced;jobs=replacement.observations();const auto old_view=jobs;
    assert(heuristics::choose(policy,replacement.state(),jobs)==0);
    assert(replaced.tick(replacement,policy,replacement.state(),jobs)==1);
    assert(old_view[0].known&&old_view[0].ready&&old_view[2].known);
    bool old0_hidden=false,old2_hidden=false;
    for(const auto&j:jobs) {
        if(j.id==0)old0_hidden=!j.known&&!j.ready&&j.work==8;
        if(j.id==2)old2_hidden=!j.known&&!j.ready&&j.work==90;
    }
    assert(old0_hidden&&old2_hidden&&replacement.cooldown==10);

    World costly;costly.lost_cost=100;
    survey::Survey cost_guard;jobs=costly.observations();
    assert(cost_guard.tick(costly,policy,costly.state(),jobs)==0);
    assert(costly.placements==0&&costly.predicted==1&&cost_guard.refused==1);
    World cooling;cooling.cooldown=3;
    survey::Survey cooling_guard;jobs=cooling.observations();
    assert(cooling_guard.tick(cooling,policy,cooling.state(),jobs)==0);
    assert(cooling.predicted==0&&cooling.placements==0);

    // Geometric coverage neither guarantees legal placement nor live facts.
    World hidden_block;hidden_block.blocked=true;
    survey::Survey blocked;jobs=hidden_block.observations();
    assert(blocked.tick(hidden_block,policy,hidden_block.state(),jobs)==0);
    hidden_block.work(0);
    assert(blocked.failed==1&&blocked.changed==0&&hidden_block.refreshes==0);
    assert(hidden_block.ward_x==-4&&hidden_block.cooldown==0&&hidden_block.walks==1);
    hidden_block.next();jobs=hidden_block.observations();
    assert(blocked.tick(hidden_block,policy,hidden_block.state(),jobs)==0);
    assert(hidden_block.placements==1); // One attempt, then useful fallback.

    World same;same.sources[1].ready=false;same.sources[1].hp=300;
    survey::Survey same_choice;jobs=same.observations();
    assert(same_choice.tick(same,policy,same.state(),jobs)==0);
    assert(same_choice.attempts==1&&same_choice.inconclusive==1&&same_choice.changed==0);
    assert(same.saw("same:0:0"));same.work(0);assert(same.walks==1);

    World no_fact;no_fact.new_fact=false;no_fact.reveal=false;
    survey::Survey inconclusive;jobs=no_fact.observations();
    assert(inconclusive.tick(no_fact,policy,no_fact.state(),jobs)==0);
    assert(inconclusive.inconclusive==1&&inconclusive.changed==0);
    World inconclusive_loss;inconclusive_loss.new_fact=false;
    inconclusive_loss.replace_old=true;inconclusive_loss.reveal=false;
    inconclusive_loss.sources[0].ready=true;inconclusive_loss.sources[0].hp=0;
    inconclusive_loss.sources[1].gems=30;
    survey::Survey lost_guard;jobs=inconclusive_loss.observations();
    assert(heuristics::choose(policy,inconclusive_loss.state(),jobs)==0);
    assert(lost_guard.tick(inconclusive_loss,policy,inconclusive_loss.state(),jobs)==1);
    assert(lost_guard.changed==0&&lost_guard.inconclusive==1);
    for(const auto&j:jobs)assert(!j.known&&!j.ready);
    inconclusive_loss.work(1);assert(inconclusive_loss.worked==1);
    World stale_world;stale_world.stale_delta=-1;
    survey::Survey stale_guard;jobs=stale_world.observations();
    assert(stale_guard.tick(stale_world,policy,stale_world.state(),jobs)==-1);
    assert(stale_guard.inconclusive==1&&stale_guard.changed==0);
    World unfinished;unfinished.incomplete_sample=true;
    survey::Survey finish_guard;jobs=unfinished.observations();
    assert(finish_guard.tick(unfinished,policy,unfinished.state(),jobs)==-1);
    assert(unfinished.placements==1&&unfinished.refreshes==1&&unfinished.walks==0);
    assert(finish_guard.incomplete==1&&finish_guard.stale==0&&finish_guard.changed==0);

    World low;low.available_fuel=75000;
    survey::Survey fuel_guard;jobs=low.observations();
    assert(fuel_guard.tick(low,policy,low.state(),jobs)==0);
    assert(low.predicted==0&&low.placements==0);
    World post_prediction;post_prediction.after_prediction_fuel=58000;
    survey::Survey reserve_guard;jobs=post_prediction.observations();
    assert(reserve_guard.tick(post_prediction,policy,post_prediction.state(),jobs)==0);
    assert(post_prediction.predicted==1&&post_prediction.placements==0);
    World geometry;geometry.predicts=false;
    survey::Survey geometry_guard;jobs=geometry.observations();
    assert(geometry_guard.tick(geometry,policy,geometry.state(),jobs)==0);
    assert(geometry.placements==0);

    World moved_epoch;moved_epoch.prediction_shift_round=1;
    survey::Survey epoch_guard;jobs=moved_epoch.observations();
    assert(epoch_guard.tick(moved_epoch,policy,moved_epoch.state(),jobs)==-1);
    assert(moved_epoch.placements==0);

    World channel;channel.external_channel=true;
    survey::Survey preserve;jobs=channel.observations();
    assert(preserve.tick(channel,policy,channel.state(),jobs)==-1);
    assert(channel.external_channel&&channel.placements==0&&channel.walks==0);
    World dead;dead.living=false;survey::Survey death;
    death.epoch_round=19;death.last_pair_a=0;death.last_pair_b=1;jobs=dead.observations();
    assert(death.tick(dead,policy,dead.state(),jobs)==-1);
    assert(death.epoch_round==-100&&death.last_pair_a==-1&&death.last_pair_b==-1);
    assert(dead.placements==0&&dead.cargo==0&&!policy.bank(dead.state()));

    // Zero cargo and gold-only jobs retain immediate productive execution.
    World empty;for(auto&s:empty.sources){s.gems=0;s.gold=30;s.visible=true;}
    survey::Survey empty_guard;jobs=empty.observations();
    const int productive=empty_guard.tick(empty,policy,empty.state(),jobs);
    assert(productive>=0&&!policy.bank(empty.state()));
    empty.work(productive);assert(empty.walks==1&&empty.placements==0);
    using P=heuristics::Purchase;
    assert(policy.purchase(11,0,0)==P::monster_strength);
    assert(policy.purchase(1,0,0)==P::monster_focus);
    assert(policy.purchase(5,0,0)==P::monster_focus);
    assert(policy.purchase(5,5,0)==P::jungle_focus);
    assert(policy.purchase(10,5,0)==P::jungle_strength);
    assert(policy.purchase(7,5,0)==P::jungle_health);
    assert(policy.purchase(7,5,5)==P::power_focus);
    assert(policy.purchase(10,5,5)==P::power_health);
    assert((policy.roster==std::array{0,3,8,4,1}));
    auto bank_state=empty.state();bank_state.bag=8;bank_state.altar=10;
    assert(policy.bank(bank_state));bank_state.bag=0;assert(!policy.bank(bank_state));
    assert(policy.retreat(400,1000,20,10));
    assert(!policy.retreat(400,1000,20,0));

    // Empty enemy vision and a cancelled policy selection do not reset hold.
    heuristics::CaptureWatch watch;
    assert(!survey::leave_capture(watch,4,30,5,false));
    survey::observe_capture(watch,true,true);
    assert(!survey::leave_capture(watch,4,35,5,false));
    const int cancelled_target=-1;(void)cancelled_target;
    survey::observe_capture(watch,true,true);
    assert(watch.source()==4&&survey::leave_capture(watch,4,37,5,false));
    int steps=0,useful=0;
    assert(survey::leave_or_work([&]{++steps;return false;},[&]{++useful;return true;}));
    assert(steps==1&&useful==1&&watch.source()==4);
    assert(survey::leave_capture(watch,4,38,5,true));
    survey::observe_capture(watch,false,true);assert(watch.source()==-1);
    assert(!survey::leave_capture(watch,4,40,5,false));
    survey::observe_capture(watch,true,false);assert(watch.source()==-1);
    assert(!survey::leave_capture(watch,4,41,5,false));
    watch.reset();assert(watch.source()==-1); // Actual death resets lifecycle.
    // Native Yellow Brick Road regression: thirty-member partial sources must
    // keep public estimates without traversing discarded live properties.
    struct Camp {int group;bool alive;float hp,strength;};
    std::vector<Camp> long_rows;
    for(int group=0;group<2;++group)for(int member=0;member<30;++member)
        long_rows.push_back({group,member<24,120.f,0.f});
    int membership_reads=0,live_reads=0;
    const auto totals=[&](bool fully,int group) {
        return survey::source_totals(fully,survey::SourceTotals{3600,0},long_rows,
            [&](const auto&c){++membership_reads;return c.group==group;},
            [&](const auto&c){++live_reads;return c.alive;},
            [&](const auto&c){++live_reads;return c.hp;},
            [&](const auto&c){++live_reads;return c.strength;});
    };
    const auto partial=totals(false,1);
    assert(partial.health==3600&&partial.incoming==0&&membership_reads==0&&live_reads==0);
    const auto full=totals(true,1);
    assert(full.health==24*120&&full.incoming==0&&membership_reads==60&&live_reads==30+24*2);

    // Production records all selected-source facts before choosing live work
    // or discovery. A remote living target must not hide witnessed empty pads
    // from the next turn's frontier after private vision is lost.
    survey::FrontierMemory rail_memory;
    std::vector<survey::Member> rail{{5,17,false,false},{17,17,true,false},
                                   {18,17,true,false},{24,17,true,true},{25,17,true,true}};
    const auto live_view=survey::goal_frontier(rail_memory,rail,true,20,60);
    assert(live_view.discover&&live_view.goals.size()==1&&live_view.goals[0].x==5);
    assert(rail_memory.dead[1]&&rail_memory.dead[2]&&!rail_memory.dead[3]&&!rail_memory.dead[4]);
    for(auto&m:rail)m.visible=m.alive=false;
    const auto lost_view=survey::goal_frontier(rail_memory,rail,false,21,60);
    assert(lost_view.discover&&lost_view.goals.size()==3);
    for(const auto&m:lost_view.goals)assert(m.x!=17&&m.x!=18);

    // Normal Yellow Brick Road regression: changing selected sources while
    // physically walking A/B cannot restart a source-independent watchdog.
    survey::RouteCycle cycle;
    int selected_source=-1;
    for(int n=0;n<5;++n) {
        selected_source=n%2;
        assert(cycle.observe(100+n,18+selected_source,17)==(n==4));
        if(n<4)assert(!cycle.claim_escape());
    }
    assert(selected_source==0&&cycle.stalled()&&cycle.claim_escape());
    assert(!cycle.claim_escape());
    for(int n=5;n<12;++n) {
        selected_source=n%2;
        assert(cycle.observe(100+n,18+selected_source,17));
        assert(!cycle.claim_escape());
    }
    // A real third tile releases the claim; it needs its own four intervals
    // before a new source can claim another witnessed physical cycle.
    assert(!cycle.observe(112,20,17)&&!cycle.claim_escape());
    for(int n=1;n<5;++n)assert(cycle.observe(112+n,20,17)==(n==4));
    assert(cycle.claim_escape()&&!cycle.claim_escape());

    survey::RouteCycle stationary;
    for(int n=0;n<5;++n)assert(stationary.observe(200+n,4,9)==(n==4));
    assert(stationary.claim_escape());
    assert(!stationary.observe(205,5,9)&&!stationary.claim_escape());
    for(int n=1;n<5;++n)stationary.observe(205+n,5,9);
    assert(stationary.stalled()&&stationary.claim_escape());

    survey::RouteCycle travelling;
    for(int n=0;n<8;++n) {
        assert(!travelling.observe(300+n,n,6));
        assert(!travelling.claim_escape());
    }
    survey::RouteCycle detour;
    assert(!detour.observe(400,1,1));
    assert(!detour.observe(401,2,1));
    assert(!detour.observe(402,1,1));
    assert(!detour.observe(403,3,1)&&!detour.claim_escape());

    // Successful work, actual payout and lifecycle reset each clear both the
    // history and consumed claim. A selector or policy cancellation does not.
    cycle.work();assert(!cycle.stalled()&&!cycle.claim_escape());
    for(int n=0;n<5;++n)cycle.observe(500+n,8+n%2,6);
    assert(cycle.stalled()&&cycle.claim_escape());
    cycle.payout();assert(!cycle.stalled()&&!cycle.claim_escape());
    for(int n=0;n<5;++n)cycle.observe(510+n,8+n%2,6);
    assert(cycle.claim_escape());
    cycle.reset();assert(!cycle.stalled()&&!cycle.claim_escape());

    // A skipped owner round cannot be counted as unseen nonprogress. Nor can
    // duplicate same-round observations manufacture four elapsed intervals.
    survey::RouteCycle timing;
    assert(!timing.observe(600,1,1));
    for(int n=0;n<8;++n)assert(!timing.observe(600,1+n%2,1));
    assert(!timing.observe(602,1,1));
    for(int n=1;n<4;++n)assert(!timing.observe(602+n,1,1));
    assert(timing.observe(606,1,1)&&timing.claim_escape());

    // Mandatory movement can still perform one useful, strictly closer step
    // after the native field budget refuses. It uses only fresh cached masks.
    using RP=survey::RoutePoint;
    const RP step_origin{5,5};
    const std::array step_goals{RP{9,5}};
    const std::array step_legal{RP{5,5},RP{6,5},RP{5,6},RP{4,5}};
    const auto cheap=survey::choose_step(18000,step_origin,step_goals,step_legal);
    assert(cheap&&*cheap==RP(6,5));
    assert(std::max(std::abs(cheap->x-9),std::abs(cheap->y-5))<4);
    assert(survey::choose_step(12000,step_origin,step_goals,step_legal)==cheap);
    assert(!survey::choose_step(11000,step_origin,step_goals,step_legal));

    const std::array blocked_legal{RP{5,5},RP{5,6},RP{4,5},RP{4,6}};
    assert(!survey::choose_step(18000,step_origin,step_goals,blocked_legal));
    const std::array<RP,0> no_points{};
    assert(!survey::choose_step(18000,step_origin,no_points,step_legal));
    assert(!survey::choose_step(18000,step_origin,step_goals,no_points));
    const std::array arrival_goals{step_origin,RP{9,5}};
    assert(!survey::choose_step(18000,step_origin,arrival_goals,step_legal));
    const std::array invalid_neighbors{RP{7,5},RP{5,5}};
    assert(!survey::choose_step(18000,step_origin,step_goals,invalid_neighbors));

    // A blocked nearest goal does not license walking toward another goal
    // farther away, nor an equal-distance movement that can restart a cycle.
    const std::array nearest_goals{RP{0,3},RP{10,0}};
    const std::array farther_step{RP{1,0}};
    assert(!survey::choose_step(18000,RP{0,0},nearest_goals,farther_step));
    std::array<RP,129> too_many_goals{};too_many_goals.fill(RP{9,5});
    assert(!survey::choose_step(18000,step_origin,too_many_goals,step_legal));
    std::array<RP,10> too_many_neighbors{};too_many_neighbors.fill(RP{6,5});
    assert(!survey::choose_step(18000,step_origin,step_goals,too_many_neighbors));
    std::array<RP,128> bounded_goals{};bounded_goals.fill(RP{30,30});
    bounded_goals.back()=RP{9,5};
    assert(survey::choose_step(18000,step_origin,bounded_goals,step_legal)==cheap);
    return 0;
}
