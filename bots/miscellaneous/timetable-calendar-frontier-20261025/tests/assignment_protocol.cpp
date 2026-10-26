#include "../src/assignment_protocol.h"
#include <cassert>
#include <cstdint>
#include <iostream>

namespace ap=tt::assignment_protocol;
ap::Input input(unsigned round,unsigned role,ap::Key key,int delta,std::uint64_t premise=7) {
    return {{8,2000,20,true},round,role,true,ap::Prepared{key,premise,delta,true,true,ap::Scenario::FullGenerationAtCommonStart,static_cast<std::uint8_t>(role)}};
}
int main() {
    const std::array<std::uint8_t,5> types{10,7,0,1,12};
    const auto contract=ap::roster_contract(types,true,5);
    assert(ap::roster_contract_valid(contract,types));
    assert(!ap::roster_contract_valid(ap::roster_contract(types,false,5),types));
    assert(!ap::roster_contract_valid(ap::roster_contract(types,true,3),types));
    auto bad_contract=contract;bad_contract[10]=std::byte{12};
    assert(!ap::roster_contract_valid(bad_contract,types));
    // The same gate drives the real local-fallback publication and NoShop seam.
    // A choice priced for14 cannot act at10 or shift its start to15 at11.
    ap::ProgrammeGate local;
    assert(local.stage(14,2000));
    assert(local.admit(10,true,true)==ap::ProgrammeAdmission::Waiting && !local.no_shop());
    assert(local.admit(11,true,true)==ap::ProgrammeAdmission::Waiting && local.start()==14);
    assert(local.admit(14,true,true)==ap::ProgrammeAdmission::Started && local.no_shop());
    local.handoff(ap::ProgrammeHandoff::SelectedHomeHeal);
    assert(local.selected_home_heal()); // Only this explicit owner may retain on recovery.
    local.handoff(ap::ProgrammeHandoff::OrdinaryFallback);
    assert(!local.no_shop() && !local.selected_home_heal() && !local.pending());
    assert(local.stage(18,2000));
    assert(local.admit(19,true,true)==ap::ProgrammeAdmission::Withdrawn && !local.no_shop());
    assert(local.stage(22,2000));
    assert(local.admit(22,false,true)==ap::ProgrammeAdmission::Withdrawn && !local.no_shop());
    assert(local.stage(26,2000));
    assert(local.admit(26,true,false)==ap::ProgrammeAdmission::Withdrawn && !local.no_shop());
    const ap::Key key{14,2000,0,1,2,3};
    assert(!ap::decode(0));
    for(int delta:{-1024,-1,0,1,1023})for(unsigned phase=0;phase<4;++phase) {
        ap::Packet p{key,delta,phase==0?0:1023,static_cast<std::uint8_t>(phase==0?1:phase==3?0:3),static_cast<ap::Phase>(phase)};
        const auto encoded=ap::encode(p);assert(encoded);
        assert(ap::decode(*encoded)==p);
        assert(ap::word(ap::bytes(*encoded))==*encoded);
    }
    assert(!ap::encode({key,1024,0,1,ap::Phase::Propose}));
    assert(!ap::encode({key,0,-1025,3,ap::Phase::Reply}));
    auto malformed=key;malformed.owner_b=0;assert(!ap::valid(malformed));
    malformed=key;malformed.group_h=31;assert(!ap::valid(malformed));
    assert(!ap::supported({9,2000,20,true}));
    assert(!ap::supported({8,2048,20,true}));
    assert(!ap::supported({8,2000,32,true}));
    assert(!ap::supported({8,2000,20,false}));
    ap::State unspecified;
    auto missing_scenario=input(10,0,key,8);missing_scenario.own->scenario=ap::Scenario::Unspecified;
    assert(!ap::advance(unspecified,missing_scenario,std::nullopt).write);
    auto wrong_actor=input(10,0,key,8);wrong_actor.own->actor=1;
    assert(!ap::advance(unspecified,wrong_actor,std::nullopt).write);

    // Worst healthy alternating order: B acts before A in round10; B's reply
    // arrives after A in round11; commit12, observed byB13, commonStart14.
    ap::State a,b;std::optional<ap::Packet> mailbox;
    auto out=ap::advance(a,input(10,0,key,8),mailbox);assert(out.write);mailbox=out.write;
    out=ap::advance(a,input(11,0,key,8),mailbox);assert(out.status==ap::Status::Waiting && !out.write);
    out=ap::advance(b,input(11,1,key,3),mailbox);assert(out.write && out.write->phase==ap::Phase::Reply);mailbox=out.write;
    out=ap::advance(a,input(12,0,key,8),mailbox);assert(out.write && out.write->phase==ap::Phase::Commit);mailbox=out.write;
    auto altered=*mailbox;altered.delta_a=-8;auto tampered_b=b;
    out=ap::advance(tampered_b,input(13,1,key,3),altered);
    assert(out.status==ap::Status::Withdrawn && !out.nomination && out.write && out.write->phase==ap::Phase::Withdraw);
    out=ap::advance(b,input(13,1,key,3),mailbox);assert(out.status==ap::Status::Waiting && !out.nomination);
    out=ap::advance(a,input(14,0,key,8),mailbox);assert(out.nomination && out.nomination->group==2);
    out=ap::advance(b,input(14,1,key,3),mailbox);assert(out.nomination && out.nomination->group==3);
    assert(!ap::advance(a,input(14,0,key,8),mailbox).nomination); // consume once

    // A changed own premise withdraws instead of promoting a foreign scalar.
    ap::State changed;
    auto proposal=ap::advance(changed,input(10,0,key,8),std::nullopt).write;assert(proposal);
    out=ap::advance(changed,input(11,0,key,8,8),proposal);
    assert(out.status==ap::Status::Withdrawn && out.write && out.write->phase==ap::Phase::Withdraw);

    // Missing/incomplete peer quotes cause no fabricated reply.
    ap::State peer;
    auto absent=input(11,1,key,3);absent.own.reset();
    out=ap::advance(peer,absent,proposal);assert(out.status==ap::Status::NeedQuotes && !out.write && out.needed==key);
    absent.quiet=false;
    out=ap::advance(peer,absent,proposal);assert(!out.write && !out.nomination);

    // Failure consumes this semantic premise, even when a new start is offered.
    ap::State timed;
    proposal=ap::advance(timed,input(10,0,key,8),std::nullopt).write;
    out=ap::advance(timed,input(13,0,key,8),proposal);
    assert(out.status==ap::Status::Expired && out.write);
    auto later=key;later.start=18;
    out=ap::advance(timed,input(14,0,later,8),std::nullopt);assert(!out.write);
    out=ap::advance(timed,input(14,0,later,8,8),std::nullopt);assert(out.write);
    auto not_quiet=input(15,0,later,8,8);not_quiet.quiet=false;
    out=ap::advance(timed,not_quiet,out.write);assert(out.status==ap::Status::Withdrawn);
    auto first_again=key;first_again.start=20;
    out=ap::advance(timed,input(16,0,first_again,8),std::nullopt);assert(!out.write); // no ABA retry

    // Nonparticipants cannot overwrite an active tuple or obtain a nomination.
    ap::State other;
    out=ap::advance(other,input(11,2,key,99),proposal);
    assert(out.status==ap::Status::Busy && !out.write && !out.nomination);
    std::cout<<"assignment protocol checks passed\n";
}
