#define PLAN4_ARCHIPELAGO_FIXTURE_NO_SDK
#include "fixtures/archipelago-fixture/src/setup.h"
#include <cstdio>
#include <cstdlib>
using namespace plan4_archipelago_fixture;
namespace {
int checks=0;
void check(bool value,const char* text){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
Frame body(int actor=3,int partner=5){Frame f;f.round=10;f.actor=actor;f.partner=partner;f.fresh=f.alive=true;return f;}
void subject_stages() {
    const auto config=configuration(true,Condition::positive);Staging s(Role::subject,3,5,config);auto f=body();
    auto unknown=f;unknown.bag=24;unknown.fresh=false;
    check(s.next(unknown).kind==Kind::wait&&!s.acquired(),"missing fresh own source cannot acquire a bag or advance staging");
    check(s.next(f).kind==Kind::warmup,"zero bag delegates legal acquisition and capture to identical common warmup");
    f.channel=true;check(s.next(f).kind==Kind::warmup,"warmup preserves active capture and channel bookkeeping");
    f.bag=24;check(s.next(f).kind==Kind::wait,"positive bag cannot interrupt a real active channel");
    f.channel=false;auto c=s.next(f);check(c.kind==Kind::home,"unverified recall safety walks toward public own spawn");
    f.recall_safe=true;check(s.next(f).kind==Kind::recall,"supported current safe recall retains actual acquired bag");
    f.channel=true;check(s.next(f).kind==Kind::wait,"real recall is preserved");
    f.channel=false;f.on_spawn=true;check(s.next(f).kind==Kind::wait,"wounded recall arrival waits for actual healing");
    f.full_health=true;check(s.next(f).kind==Kind::wait,"first full-health spawn observation holds owner turn");
    check(s.next(f).kind==Kind::wait,"duplicate same-round observation cannot skip full-health owner-turn hold");
    ++f.round;
    f.on_spawn=false;c=s.next(f);check(c.kind==Kind::move&&c.goal==config.blocker,"only restored subject begins actual route to ward checkpoint");
    f.position=config.blocker;c=s.next(f);check(c.kind==Kind::ward,"subject places own ward at future blocker tile");
    check(!s.acknowledge(c,false)&&s.next(f).kind==Kind::ward,"failed ward attempt does not fabricate vision support");
    check(s.acknowledge(c,true),"successful own ward action is acknowledged");
    c=s.next(f);check(c.kind==Kind::move&&c.goal==config.actor,"subject enters spur only after ward action");
    f.position=config.actor;check(s.next(f).kind==Kind::wait,"no release based on unseen ally position");
    f.partner_personally_visible=true;f.partner_round=f.round;f.partner_position=config.blocker;
    f.own_ward_present=f.partner_tile_visible=f.partner_tile_occupied=true;
    f.partner=9;check(s.next(f).kind==Kind::wait,"wrong visible ally cannot satisfy blocker role");
    f.partner=5;--f.partner_round;check(s.next(f).kind==Kind::wait,"stale visible blocker remains unknown");
    f.partner_round=f.round;f.adjacent_enemy=true;check(s.next(f).kind==Kind::wait,"adjacent enemy preemption blocks release");
    f.adjacent_enemy=false;f.own_ward_present=false;check(s.next(f).kind==Kind::wait,"historical ward placement without current self ward source cannot release");
    f.own_ward_present=true;f.partner_tile_visible=false;check(s.next(f).kind==Kind::wait,"missing current personal tile visibility cannot release");
    f.partner_tile_visible=true;f.partner_tile_occupied=false;check(s.next(f).kind==Kind::wait,"missing current occupied connector mask cannot release");
    f.partner_tile_occupied=true;
    f.adjacent_enemy=false;f.bag=0;check(s.next(f).kind==Kind::wait,"lost bag never becomes a fabricated bank trigger");
    f.bag=24;c=s.next(f);check(c.kind==Kind::model&&!s.released(),"fresh positive own bag and blocker propose release only");
    check(!s.acknowledge(c,false)&&!s.released(),"failed final fresh-source guard cannot release next turn");
    f.partner_personally_visible=false;check(s.next(f).kind==Kind::wait&&!s.acknowledge(c,true),"replacement unknown observation invalidates a prior release proposal");
    f.partner_personally_visible=true;
    c=s.next(f);check(c.kind==Kind::model&&s.commit_release(f,true)&&s.released(),"fresh synchronous commit makes release irreversible");
    f.alive=f.fresh=false;f.bag=0;check(s.next(f).kind==Kind::model,"released subject remains under common model after death or changed observations");
}
void tentative_release_requires_renewed_fresh_admission() {
    const auto config=configuration(true,Condition::positive);
    const auto candidate=[&](Staging& staging) {
        auto f=body();f.bag=120;f.full_health=f.on_spawn=f.own_ward_present=true;
        check(staging.next(f).kind==Kind::wait,"release preparation retains full-health owner-turn hold");
        ++f.round;f.on_spawn=false;f.position=config.actor;
        f.partner_position=config.blocker;f.partner_round=f.round;
        f.partner_personally_visible=f.partner_tile_visible=f.partner_tile_occupied=true;
        check(staging.next(f).kind==Kind::model&&!staging.released()&&staging.pending_release_round()==f.round,
            "model candidate is a tentative same-round ticket and never automatically releases");
        return f;
    };
    for(int fault=0;fault<19;++fault) {
        Staging staging(Role::subject,3,5,config);const auto fresh=candidate(staging);auto bad=fresh;
        switch(fault) {
        case 0:++bad.round;break;
        case 1:++bad.actor;break;
        case 2:bad.fresh=false;break;
        case 3:bad.alive=false;break;
        case 4:bad.full_health=false;break;
        case 5:bad.bag=0;break;
        case 6:bad.channel=true;break;
        case 7:++bad.position.x;break;
        case 8:++bad.partner;break;
        case 9:--bad.partner_round;break;
        case 10:bad.partner_personally_visible=false;break;
        case 11:++bad.partner_position.y;break;
        case 12:bad.own_ward_present=false;break;
        case 13:bad.partner_tile_visible=false;break;
        case 14:bad.partner_tile_occupied=false;break;
        case 15:bad.adjacent_enemy=true;break;
        case 16:bad.enemy_within_eight=true;break;
        case 17:bad.awake_camp_in_range=true;break;
        default:break; // A refused/budget-stopped common preparation.
        }
        check(!staging.commit_release(bad,fault!=18)&&!staging.released()&&staging.pending_release_round()==-1,
            "stale, unsafe, mismatched or refused preparation cannot commit and consumes its ticket");
        check(!staging.commit_release(fresh,true)&&!staging.released(),
            "repairing a failed receipt alone cannot reuse the old preparation candidate");
        check(staging.next(fresh).kind==Kind::model&&staging.commit_release(fresh,true)&&staging.released(),
            "a renewed fresh candidate may commit synchronously after the failed attempt");
    }
    Staging staging(Role::subject,3,5,config);const auto fresh=candidate(staging);
    const Command old{Kind::model,{},fresh.round};
    check(!staging.acknowledge(old,false)&&!staging.commit_release(fresh,true),
        "false generic action acknowledgement cannot retain a model-release ticket");
    check(staging.next(fresh).kind==Kind::model&&!staging.acknowledge(old,true)&&!staging.released(),
        "ordinary successful action acknowledgement cannot bypass fresh release admission");
    check(!staging.commit_release(fresh,true),"bypass attempt requires another renewed candidate");
}
void blocker_stages() {
    const auto config=configuration(false,Condition::connected_negative);Staging s(Role::blocker,5,3,config);auto f=body(5,3);
    Staging preexisting(Role::blocker,5,3,config);auto owned=f;owned.own_ward_present=true;
    check(preexisting.next(owned).goal==config.waiting,"fresh existing OWN ward skips a duplicate placement and cannot deadlock on team ward mask");
    Staging other_ward(Role::blocker,5,3,config);
    check(other_ward.next(f).goal==config.actor,"another ally ward without self ownership cannot supply personal ward checkpoint");
    auto c=s.next(f);check(c.kind==Kind::move&&c.goal==config.actor,"blocker first visits spur for own personal ward");
    f.position=config.actor;c=s.next(f);check(c.kind==Kind::ward,"blocker cannot borrow subject vision");check(s.acknowledge(c,true),"own ward placed");
    c=s.next(f);check(c.kind==Kind::move&&c.goal==config.waiting,"blocker vacates entry chain before subject arrives");
    f.position=config.waiting;check(s.next(f).kind==Kind::wait,"unknown subject position cannot close connector");
    f.partner_personally_visible=true;f.partner_round=f.round-1;f.partner_position=config.actor;
    f.own_ward_present=f.partner_tile_visible=f.partner_tile_occupied=true;
    check(s.next(f).kind==Kind::wait,"stale subject sight cannot close connector");
    f.partner_round=f.round;c=s.next(f);check(c.kind==Kind::move&&c.goal==config.blocker,"fresh personally observed subject inside permits blocker approach");
    f.position=config.blocker;f.partner_personally_visible=false;
    check(s.next(f).kind==Kind::wait,"fresh own arrival latches the blocker even if subject moved after release");
    check(s.next(f).kind==Kind::wait,"established fixture blocker does not invent a new chase or model action");
}
void public_roles_and_parking() {
    Role role;int partner=-1;const std::array roster{9,3,7,5};
    check(roles(roster,3,role,partner)&&role==Role::subject&&partner==5,"subject and blocker depend on sorted public IDs, not roster order");
    check(roles(roster,5,role,partner)&&role==Role::blocker&&partner==3,"second public actor is blocker");
    check(roles(roster,9,role,partner)&&role==Role::idle,"remaining ally remains inert");
    const std::array duplicate{3,3};check(!roles(duplicate,3,role,partner),"duplicate roster rejects role inference");
    std::array<ParkingChoice,5> choices{};
    for(int y=0;y<20;++y)for(int x=0;x<20;++x)consider_parking(choices,{x,y},{0,0},{1,1},true,x==9,false);
    check(!choices[0].valid,"unsafe parking candidates never publish");
    for(int y=0;y<20;++y)for(int x=0;x<20;++x)consider_parking(choices,{x,y},{0,0},{1,1},true,x==9,true);
    for(int n=0;n<5;++n){check(choices[n].valid&&distance(choices[n].point,{1,1})>8&&choices[n].point.x!=9,"parking excludes nearby subject and public camp tiles");
        for(int k=0;k<n;++k)check(choices[k].point!=choices[n].point,"public rank parking goals are distinct");}
    Staging s(Role::idle,9,3,configuration(true,Condition::positive));auto f=body(9,3);f.bag=24;
    check(s.next(f).kind==Kind::wait,"idle actor cannot release because it has a bag");
}
}
int main(){subject_stages();blocker_stages();public_roles_and_parking();tentative_release_requires_renewed_fresh_admission();
    std::printf("plan4 archipelago fixture: 4 scenarios, %d checks passed\n",checks);}
