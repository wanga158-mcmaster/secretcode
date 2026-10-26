// Reuse the independent thirty-member observation oracle and its SDK/error host.
// Extra cases exercise only the current visible-list join, never live-state reuse.
#define main identity_join_fixture_main
#pragma clang diagnostic push
// The included fixture's implicit main return is special only before renaming;
// this unused entry point is not called below. Reuse its independent helpers.
#pragma clang diagnostic ignored "-Wreturn-type"
#include "plan3-identity-join-current.cpp"
#pragma clang diagnostic pop
#undef main

namespace {
unsigned visible_cases=0;

void check_visible_world(World& world,unsigned scenario) {
    world.begin(0);
    const bool accepted=world.read();
    verify(accepted);
    // Independent reference: preserve the original first exact pair in the
    // visible sequence, then account for fresh replies in stored-member order.
    GroupObservation wanted;
    wanted.round=32;wanted.lifecycle=Lifecycle::alive;wanted.all_members_visible=true;
    std::vector<std::pair<unsigned,unsigned>> queries{{4,0},{0,0}};
    for(unsigned row=0;row<member_count;++row) {
        wanted.remaining_health+=100.+row;wanted.incoming+=10.+.5*row;
        const cambc::camp* first=nullptr;
        for(const auto& camp:world.visible_camps) {
            if(static_cast<unsigned>(static_cast<cambc::handle_camp_t>(camp))==row &&
               camp.spawn_position()==identity_spawn(row)){first=&camp;break;}
        }
        queries.emplace_back(1,row);
        if(!identity_states[row]) {
            wanted.remaining_health-=100.+row;wanted.incoming-=10.+.5*row;
            continue;
        }
        if(!first){wanted.all_members_visible=false;continue;}
        queries.emplace_back(2,row);
        wanted.remaining_health-=100.+row;wanted.incoming-=10.+.5*row;
        wanted.remaining_health+=identity_health[row];wanted.incoming+=identity_strength[row];
    }
    verify(identity_equal(world.p.observations[0],wanted) && identity_live==queries);
    emit(100+scenario,accepted,world.p.observations[0]);
    ++visible_cases;
}

void visible_sequences() {
    for(unsigned scenario=0;scenario<9;++scenario) {
        World world(0);world.warm();
        if(scenario==0)world.visible_camps.clear();
        if(scenario==1)world.visible_camps={identity_camp(13,13)};
        if(scenario==2 || scenario==3) {
            while(world.visible_camps.size()<(scenario==2?64:65))
                world.visible_camps.push_back(identity_camp(1,1));
        }
        if(scenario==4) {
            // Same strong handle at a different spawn precedes the true pair.
            world.visible_camps.insert(world.visible_camps.begin(),identity_camp(1,10));
        }
        if(scenario==5) {
            // A different strong handle at the right spawn is not a match.
            world.visible_camps.insert(world.visible_camps.begin(),identity_camp(32,1));
            world.visible_camps.erase(world.visible_camps.begin()+2);
        }
        if(scenario==6)std::reverse(world.visible_camps.begin(),world.visible_camps.end());
        if(scenario==7) {
            world.visible_camps.insert(world.visible_camps.begin(),identity_camp(1,1));
            world.visible_camps.insert(world.visible_camps.begin()+2,identity_camp(1,1));
        }
        if(scenario==8) {
            // Replace in place after warming: a prior vector address/size is no
            // evidence that its current identities or order stayed unchanged.
            const auto* storage=world.visible_camps.data();
            auto replacement=world.visible_camps;
            std::rotate(replacement.begin(),replacement.begin()+13,replacement.end());
            replacement[7]=identity_camp(32,7);
            world.visible_camps=replacement;verify(world.visible_camps.data()==storage);
        }
        check_visible_world(world,scenario);
    }
}

void visible_pointer_and_bounds() {
#if PLAN3_OBSERVATION_VISIBLE_JOIN
    using Join=PersonalInformation::VisibleJoin;
    World world(0);world.begin(0);
    world.visible_camps.insert(world.visible_camps.begin(),identity_camp(1,10));
    world.visible_camps.insert(world.visible_camps.begin()+1,identity_camp(1,1));
    Join join;verify(PersonalInformation::prepare_visible_join(join,world.visible_camps,30000));
    bool finished=true;
    const auto* first=PersonalInformation::visible_join_first(join,world.visible_camps,
        identity_camp(1,1),30000,finished);
    verify(finished && first==&world.visible_camps[1]);
    finished=true;
    verify(!PersonalInformation::visible_join_first(join,world.visible_camps,
        identity_camp(64,1),30000,finished) && finished);
    // Sparse, nonmonotone strong handles and deliberate collisions are legal
    // metadata. The table cannot assume dense IDs or confuse a probe collision
    // with an equal pair; duplicate pairs still point to their first ordinal.
    std::vector<cambc::camp> collisions;
    for(unsigned value=100000;value<200000 && collisions.size()<20;++value) {
        const auto camp=identity_camp(value,3);
        if(PersonalInformation::visible_identity_hash(camp)==17)collisions.push_back(camp);
    }
    verify(collisions.size()==20);
    std::reverse(collisions.begin(),collisions.end());
    collisions.push_back(collisions[7]);
    verify(PersonalInformation::prepare_visible_join(join,collisions,30000));
    finished=true;
    verify(PersonalInformation::visible_join_first(join,collisions,collisions[7],30000,finished)==
        &collisions[7] && finished);
    for(std::size_t i=0;i<20;++i) {
        finished=true;
        verify(PersonalInformation::visible_join_first(join,collisions,collisions[i],30000,finished)==
            &collisions[i] && finished);
    }
    world.visible_camps.resize(65,identity_camp(1,1));
    verify(!PersonalInformation::prepare_visible_join(join,world.visible_camps,30000));
    world.visible_camps.erase(world.visible_camps.begin()+64,world.visible_camps.end());
    verify(PersonalInformation::prepare_visible_join(join,world.visible_camps,30000));
    // Interrupt table construction. That table cannot be used as an absence
    // oracle; a subsequent callback builds from scratch, including live changes.
    fuel_reads=0;identity_drop_fuel_at=4;
    verify(!PersonalInformation::prepare_visible_join(join,world.visible_camps,30000));
    identity_drop_fuel_at=0;fuel_reads=0;
    verify(PersonalInformation::prepare_visible_join(join,world.visible_camps,30000));
    identity_health[2]=18.5f;identity_strength[2]=91.75f;
    check_visible_world(world,9);
    // Lookup exhaustion reports unfinished rather than inventing absence.
    identity_drop_fuel_at=1;fuel_reads=0;finished=true;
    verify(!PersonalInformation::visible_join_first(join,world.visible_camps,
        identity_camp(1,1),30000,finished) && !finished);
    identity_drop_fuel_at=0;
#else
    World world(0);world.warm();identity_health[2]=18.5f;identity_strength[2]=91.75f;
    check_visible_world(world,9);
#endif
}
}

int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if(const char* error=std::getenv("P3_IDENTITY_JOIN_ERROR")) {
        World world(0);world.warm();world.begin(0);
        const bool info=error[0]=='i';identity_code=std::atoi(error+2);
        if(error[0]=='v'){identity_states[1]=3;identity_code=0;}
        else if(info)identity_info_error=1;else identity_state_error=1;
        std::fprintf(stderr,"IDENTITY_JOIN_EXPECTED_ERROR %s\n",error);world.read();verify(false);
    }
    for(unsigned scenario=0;scenario<30;++scenario)normal(scenario);
    budget_recovery();cache_limits();
    std::printf("IDENTITY_JOIN_RESULT {\"status\":\"pass\",\"side\":%u,\"arm\":%u,\"cases\":%u,\"boundaryCases\":%u,\"checks\":%u,\"nativeGames\":0}\n",
        bp_side(),unsigned(PLAN3_OBSERVATION_IDENTITY_JOIN),identity_cases,identity_boundary_cases,checks);
    visible_sequences();visible_pointer_and_bounds();
    std::printf("VISIBLE_JOIN_RESULT {\"status\":\"pass\",\"side\":%u,\"arm\":%u,\"cases\":%u,\"nativeGames\":0}\n",
        bp_side(),unsigned(PLAN3_OBSERVATION_VISIBLE_JOIN),visible_cases);
}
