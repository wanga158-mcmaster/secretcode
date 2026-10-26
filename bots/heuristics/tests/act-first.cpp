#include "../act-first/src/act.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace act=h12_act_first;

// A mutable scalar-only production adapter. The fixture deliberately makes
// audit inputs inaccessible until an actual prefix has returned and audit
// fuel admission has passed. This tests ActFirst::tick, not a second selector.
struct World {
    int turn=20,life=1,finance=3;
    std::uint64_t remaining=60000;
    int prefix_cost=1000,audit_shift=0;
    bool refill=false,actual=true,work=false,admissible=true;
    bool living=true,channel=false,urgent_bank=false,capture=false;
    bool source_captured=false,unsafe=false,lost_admission=false;
    bool prefix_done=false,audit_started=false,first_fuel=false;
    int prefix_calls=0,veto_calls=0,bootstraps=0,fuel_calls=0;
    int counts=0,state_reads=0,availability_reads=0;
    int acted_source=-1;
    std::array<bool,3> available_sources{true,true,true};
    std::array<heuristics::Job,3> jobs{};
    std::vector<int> job_reads;
    std::vector<std::string> events;

    World() {
        for(int id=0;id<3;++id) {
            auto&j=jobs[id];j.id=id;j.affordable=true;j.known=true;
            j.travel=4;j.work=4;j.altar=2;j.gems=id==0?1:60;
        }
    }
    act::Stamp stamp()const{return {turn,life,finance};}
    bool veto(int id) {
        assert(!prefix_done);++veto_calls;events.push_back("veto");
        return id>=0&&available_sources[id]&&living&&!channel&&!urgent_bank
            &&!capture&&!source_captured&&!unsafe&&!lost_admission;
    }
    bool available(int id)const{return id>=0&&id<3&&available_sources[id];}
    int bootstrap(){++bootstraps;events.push_back("bootstrap");return 0;}
    act::Receipt prefix(int id) {
        assert(!prefix_done);++prefix_calls;events.push_back("prefix");
        acted_source=id;
        remaining=remaining>std::uint64_t(prefix_cost)?remaining-prefix_cost:0;
        prefix_done=true;
        return {actual,work,admissible,id,stamp()};
    }
    std::uint64_t fuel() {
        assert(prefix_done);++fuel_calls;
        if(!first_fuel){first_fuel=true;return remaining;}
        if(refill)return remaining+10000;
        return remaining;
    }
    int current_round()const{return turn;}
    void audit_input() {
        assert(prefix_done&&first_fuel&&remaining>=28000);
        audit_started=true;
    }
    int count(){audit_input();++counts;events.push_back("count");return 3;}
    heuristics::State cached_state() {
        audit_input();++state_reads;events.push_back("state");
        turn+=audit_shift;remaining-=500;
        heuristics::State s;s.round=turn;s.mature=true;s.hp=s.maximum=1000;
        return s;
    }
    heuristics::Job cached_job(int id) {
        assert(audit_started);job_reads.push_back(id);events.push_back("job");
        remaining-=100;return jobs[id];
    }
    bool cached_available(int id) {
        assert(audit_started);++availability_reads;remaining-=20;
        return available_sources[id];
    }
    void next() {
        ++turn;remaining=60000;prefix_done=false;audit_started=false;
        first_fuel=false;fuel_calls=0;job_reads.clear();events.clear();
        audit_shift=0;refill=false;
    }
    void suppress_audit(){remaining=27000;prefix_cost=0;}
};

static void proposal(act::ActFirst&controller,World&world,act::Policy&policy) {
    const auto receipt=controller.tick(world,policy);
    assert(receipt.actual&&world.acted_source==0);
    assert(controller.incumbent==0&&controller.adopted==0);
    assert(controller.proposal().complete&&controller.proposal().source==1);
    assert(controller.proposal().owner==20&&controller.proposal().life==1
        &&controller.proposal().finance==3);
}

int main() {
    act::Policy policy;
    // Production finance epochs include gold-only payout and spending, even
    // when the carried gem bag stays empty.
    assert(!act::finance_changed(0,0,0,0));
    assert(!act::finance_changed(12,90,12,90));
    assert(act::finance_changed(0,35,0,0));
    assert(act::finance_changed(0,60,0,90));
    assert(act::finance_changed(8,90,0,90));
    assert(act::finance_changed(0,90,8,90));
    {
        World w;act::ActFirst c;proposal(c,w,policy);
        assert(w.bootstraps==1&&w.prefix_calls==1&&w.veto_calls==0);
        assert(w.events.size()>=5&&w.events[0]=="bootstrap"&&w.events[1]=="prefix"
            &&w.events[2]=="count"&&w.events[3]=="state");
        // The incumbent and exactly one rotating challenger are read. Merely
        // publishing never redirects the action which has already happened.
        assert((w.job_reads==std::vector<int>{0,1}));
        assert(w.counts==1&&w.state_reads==1&&w.availability_reads==1);
        assert(c.audits==1&&c.published==1&&c.maximum_audit==720);
        w.next();w.suppress_audit();c.tick(w,policy);
        assert(c.adopted==1&&c.incumbent==1&&w.acted_source==1);
        assert(w.events[0]=="veto"&&w.events[1]=="prefix");
        assert(!c.proposal().complete);
    }
    // A second call in the same owner round cannot use a proposal. Nor can an
    // intervening owner turn be skipped, or a respawn/cargo/gear/gold epoch survive.
    for(int condition=0;condition<6;++condition) {
        World w;act::ActFirst c;proposal(c,w,policy);
        w.next();w.suppress_audit();
        if(condition==0)--w.turn;
        if(condition==1)++w.turn;
        if(condition==2)++w.life;
        if(condition==3)++w.finance; // cargo epoch
        if(condition==4)w.finance+=2; // purchased gear epoch
        if(condition==5&&act::finance_changed(0,35,0,0))++w.finance; // gold-only payout, empty bag
        c.tick(w,policy);
        assert(c.adopted==0&&c.vetoed==1&&w.acted_source==0);
        assert(w.veto_calls==0&&!c.proposal().complete);
    }
    // The fresh host veto owns current safety and executable admission. A
    // teammate capture is a lifecycle change even if this actor did no work.
    for(int condition=0;condition<8;++condition) {
        World w;act::ActFirst c;proposal(c,w,policy);
        w.next();w.suppress_audit();
        if(condition==0)w.living=false;
        if(condition==1)w.channel=true;
        if(condition==2)w.urgent_bank=true;
        if(condition==3)w.capture=true;
        if(condition==4)w.source_captured=true;
        if(condition==5)w.unsafe=true;
        if(condition==6)w.lost_admission=true;
        if(condition==7)w.available_sources[1]=false;
        c.tick(w,policy);
        assert(c.adopted==0&&c.vetoed==1&&w.veto_calls==1);
        assert(w.acted_source==0&&!c.proposal().complete);
    }
    // False movement (for example a hidden occupant) is not an action receipt.
    // Unadmitted recovery/channel work also cannot fund an audit/proposal.
    for(int condition=0;condition<3;++condition) {
        World w;act::ActFirst c;
        if(condition==0)w.actual=false;
        if(condition==1)w.admissible=false;
        if(condition==2)c.incumbent=-1,w.available_sources[0]=false,w.actual=false;
        c.tick(w,policy);
        assert(w.prefix_calls==1&&c.audits==0&&!c.proposal().complete);
        assert(w.fuel_calls==0&&w.counts==0&&w.state_reads==0&&w.job_reads.empty());
    }
    // Admission happens before every audit input, including count. At 29--31K
    // a cheap mandatory move may succeed and consume the audit reserve; the
    // controller then returns to the host's minimal yield path immediately.
    for(int initial=29000;initial<=31000;initial+=1000) {
        World w;act::ActFirst c;w.remaining=initial;w.prefix_cost=4000;
        const auto receipt=c.tick(w,policy);
        assert(receipt.actual&&w.acted_source==0&&c.low==1);
        assert(c.audits==0&&w.counts==0&&w.state_reads==0&&w.job_reads.empty());
        assert(w.fuel_calls==1&&!c.proposal().complete);
    }
    {
        World w;act::ActFirst c;w.remaining=28000;w.prefix_cost=1;
        c.tick(w,policy);
        assert(c.low==1&&w.counts==0&&w.state_reads==0&&w.job_reads.empty());
    }
    // A changed round or a fuel refill proves suspension; unsigned subtraction
    // must never turn that into a huge audit measurement or publish a choice.
    for(int condition=0;condition<2;++condition) {
        World w;act::ActFirst c;
        if(condition==0)w.audit_shift=1;else w.refill=true;
        c.tick(w,policy);
        assert(c.invalid==1&&c.maximum_audit==0&&!c.proposal().complete);
        assert(c.published==0&&c.audits==1);
    }
    // Rotation evaluates one candidate each turn, without building a menu or
    // resetting its cursor when the incumbent remains productive.
    {
        World w;act::ActFirst c;w.jobs[1].gems=w.jobs[2].gems=1;
        c.tick(w,policy);assert((w.job_reads==std::vector<int>{0,1}));
        w.next();c.tick(w,policy);assert((w.job_reads==std::vector<int>{0,2}));
        w.next();c.tick(w,policy);assert((w.job_reads==std::vector<int>{0,1}));
        assert(c.audits==3&&c.published==0);
    }
    // Physical no-work age survives movement, proposal publication/adoption,
    // and source alternation. Only actual camp work resets this clock here.
    {
        World w;act::ActFirst c;proposal(c,w,policy);
        assert(c.no_work_age==1);
        w.next();w.suppress_audit();c.tick(w,policy);
        assert(c.incumbent==1&&c.no_work_age==2);
        w.next();w.suppress_audit();c.incumbent=0;c.tick(w,policy);
        assert(c.no_work_age==3);
        w.next();w.suppress_audit();w.work=true;c.tick(w,policy);
        assert(c.no_work_age==0);
        w.next();w.suppress_audit();w.work=false;c.incumbent=2;c.tick(w,policy);
        assert(c.no_work_age==1);
    }
    // A long productive route accumulates physical no-work age while it is
    // still making movement progress. The production reconsideration gate
    // requires separate route stagnation (or repeated failed steps).
    {
        World w;act::ActFirst c;c.incumbent=0;c.no_work_age=998;
        w.suppress_audit();c.tick(w,policy);
        assert(c.no_work_age==999&&!act::reconsider(c.no_work_age,0,0));
        w.next();w.suppress_audit();c.incumbent=2;c.tick(w,policy);
        assert(c.no_work_age==1000&&!act::reconsider(c.no_work_age,0,0));
        assert(!act::reconsider(1000,7,0));
        assert(act::reconsider(1000,8,0));
        assert(!act::reconsider(59,100,0));
        assert(act::reconsider(60,8,0));
        assert(!act::reconsider(0,0,5));
        assert(act::reconsider(0,0,6));
    }
    // Negative caches and the fixed switching toll both refuse an attractive
    // scalar challenger without broadening into another audit.
    {
        World w;act::ActFirst c;w.available_sources[1]=false;
        c.tick(w,policy);
        assert(c.audits==1&&c.published==0&&w.job_reads.size()==2);
    }
    {
        World w;act::ActFirst c;w.jobs[1]=w.jobs[0];w.jobs[1].id=1;
        c.tick(w,policy);assert(c.audits==1&&c.published==0);
    }
}
