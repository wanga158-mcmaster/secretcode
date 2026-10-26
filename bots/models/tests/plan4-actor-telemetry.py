"""Compile actual actor/observer captures, deferred flush, and bounded writer.

Actor evidence comes from its real collector. Observer telemetry deliberately
uses cached diagnostics and Information; observer-budget owns collector checks.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]


def method(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


PRELUDE = r'''
#include "ACTOR_HEADER"
#define MODEL4_OBSERVER_NO_SDK
#include "OBSERVER_HEADER"
#include "TELEMETRY_HEADER"
#include "PHASE_HEADER"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>
#include <streambuf>
#include <string>
#include <tuple>
namespace cm = cambc::mock;
using namespace plan4::runtime;
#define PLAN4_REFERENCE_VERSION "offline-actor-telemetry"
#define PLAN4_ACTOR_EVIDENCE 1
struct CachedObserver {
    plan4::ObserverDiagnostics cached{};
    const plan4::ObserverDiagnostics& diagnostics() const { return cached; }
};
struct Bot {
    ActorEvidence actor_evidence_;
    std::unique_ptr<plan4::Information> information_storage_;
    CachedObserver observer_;
    unsigned phase_seen_ = 0;
    plan4::DeferredPhases phase_samples_;
'''

TEST = r'''
};
void check(bool value,const char* message) {
    if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
void setup() {
    cm::reset();cm::query_cost=25;
    cm::actors.push_back({7,{3,4},true,true,cambc::team::ally,23,80,120,17,3,9,4,true});
    cm::actors.push_back({9,{4,4},true,false,cambc::team::enemy,777});
}
bool same_actor(const VisibleActor& a,const VisibleActor& b) {
    return std::tie(a.id,a.draft_ordinal,a.champion,a.attack_range,a.position.x,a.position.y,
        a.health,a.maximum_health,a.strength,a.bag,a.current_priority,a.next_priority,
        a.provenance.observed_round,a.provenance.source,a.provenance.status,a.ally,a.alive,a.completed_turn)
        ==std::tie(b.id,b.draft_ordinal,b.champion,b.attack_range,b.position.x,b.position.y,
        b.health,b.maximum_health,b.strength,b.bag,b.current_priority,b.next_priority,
        b.provenance.observed_round,b.provenance.source,b.provenance.status,b.ally,b.alive,b.completed_turn);
}
bool same_frame(const ActorFrame& a,const ActorFrame& b) {
    if(std::tie(a.clock.round,a.clock.own.id,a.allied_roster,a.enemy_roster,a.allied_count,a.enemy_count,
        a.count,a.roster_complete,a.truncated,a.own_observed,a.draft_order_known)
        !=std::tie(b.clock.round,b.clock.own.id,b.allied_roster,b.enemy_roster,b.allied_count,b.enemy_count,
        b.count,b.roster_complete,b.truncated,b.own_observed,b.draft_order_known)||!same_actor(a.own,b.own))return false;
    for(unsigned n=0;n<a.samples.size();++n)if(!same_actor(a.samples[n],b.samples[n]))return false;
    return true;
}
void capture_actor(Bot& bot) {
    const auto before=bot.actor_evidence_.frame();
    const int queries=cm::queries,raw=cm::raw_queries,unauthorized=cm::unauthorized;
    const unsigned seen=bot.phase_seen_;
    std::ostringstream output;auto* previous=std::cout.rdbuf(output.rdbuf());
    bot.macro_actor_phase();std::cout.rdbuf(previous);
    check(cm::queries==queries&&cm::raw_queries==raw&&cm::unauthorized==unauthorized,
        "actor phase adds no SDK data query");
    check(same_frame(before,bot.actor_evidence_.frame()),"actor phase preserves every published actor field");
    check(output.str().empty(),"actor capture produces no immediate output");
    check(bot.phase_seen_==seen,"actor capture cannot consume an emitted sampling bit");
}
struct FlushBuffer final : std::streambuf {
    std::string text;
    int rows=0;
    bool lower_fuel=false,jump_round=false;
    std::streamsize xsputn(const char* data,std::streamsize count) override {
        text.append(data,std::size_t(count));return count;
    }
    int_type overflow(int_type ch) override {
        if(traits_type::eq_int_type(ch,traits_type::eof()))return traits_type::not_eof(ch);
        text.push_back(traits_type::to_char_type(ch));
        if(ch=='\n'&&++rows==1){if(lower_fuel)cm::fuel=15999;if(jump_round)++cm::round;}
        return ch;
    }
};
std::string flush(Bot& bot,bool lower_fuel=false,bool jump_round=false) {
    const int queries=cm::queries,raw=cm::raw_queries,unauthorized=cm::unauthorized;
    FlushBuffer output;output.lower_fuel=lower_fuel;output.jump_round=jump_round;
    auto* previous=std::cout.rdbuf(&output);
    bot.macro_flush_phases();std::cout.rdbuf(previous);
    check(cm::queries==queries&&cm::raw_queries==raw&&cm::unauthorized==unauthorized,
        "deferred flush adds no SDK data query");
    check(bot.phase_samples_.pending_mask==0&&bot.phase_samples_.count==0,
        "flush always discards pending optional rows");
    return output.text;
}
std::string emit(Bot& bot) {
    capture_actor(bot);return flush(bot);
}
bool same_diagnostics(const plan4::ObserverDiagnostics& a,const plan4::ObserverDiagnostics& b) {
    return std::tie(a.stop_stage,a.stop_group,a.stop_member,a.groups,a.visited,
        a.collection_fuel,a.stop_floor,a.stop_fuel,a.stop_margin)
        ==std::tie(b.stop_stage,b.stop_group,b.stop_member,b.groups,b.visited,
        b.collection_fuel,b.stop_floor,b.stop_fuel,b.stop_margin);
}
void capture_observer(Bot& bot,unsigned before=100000) {
    const auto diagnostics=bot.observer_.cached;
    const auto information=bot.information_storage_.get();
    // Compare the unchanged original object's representation, including all
    // unused slots, without copying a large Information onto the WASM stack.
    auto bytes=std::make_unique<std::array<unsigned char,sizeof(plan4::Information)>>();
    if(information)std::memcpy(bytes->data(),information,bytes->size());
    const int queries=cm::queries,raw=cm::raw_queries,unauthorized=cm::unauthorized;
    const int fuel=cm::fuel,round=cm::round;
    const unsigned seen=bot.phase_seen_;
    std::ostringstream output;auto* previous=std::cout.rdbuf(output.rdbuf());
    bot.macro_observer_phase(before);std::cout.rdbuf(previous);
    check(cm::queries==queries&&cm::raw_queries==raw&&cm::unauthorized==unauthorized
        &&cm::fuel==fuel&&cm::round==round,"observer phase adds no SDK data query or synthetic state change");
    check(bot.information_storage_.get()==information
        &&(!information||!std::memcmp(bytes->data(),information,bytes->size())),
        "observer phase preserves every cached Information byte");
    check(same_diagnostics(diagnostics,bot.observer_.cached),"observer phase preserves cached diagnostics");
    check(output.str().empty(),"observer capture produces no immediate output");
    check(bot.phase_seen_==seen,"observer capture cannot consume an emitted sampling bit");
}
std::string emit_observer(Bot& bot,unsigned before=100000) {
    capture_observer(bot,before);return flush(bot);
}
void repeat_captures(Bot& bot) {
    const unsigned seen=bot.phase_seen_;
    std::ostringstream output;auto* previous=std::cout.rdbuf(output.rdbuf());
    for(unsigned n=0;n<1000;++n){bot.macro_observer_phase(100000);bot.macro_actor_phase();}
    std::cout.rdbuf(previous);
    check(output.str().empty()&&bot.phase_seen_==seen,"repeated captures cannot write rows or consume sampling bits");
}
void observer_setup(Bot& bot) {
    bot.information_storage_=std::make_unique<plan4::Information>();
    auto& info=*bot.information_storage_;
    info.round=cm::round;info.own.id=0;
    info.observation_truncated=true;info.public_layout_truncated=false;
    auto& diagnostics=bot.observer_.cached;
    diagnostics.stop_stage=4;diagnostics.stop_group=7;diagnostics.stop_member=2;
    diagnostics.groups=9;diagnostics.visited=4;diagnostics.collection_fuel=90000;
    diagnostics.stop_fuel=85000;diagnostics.stop_floor=78000;diagnostics.stop_margin=8000;
}
void observer_cases() {
    setup();Bot absent;
    check(emit_observer(absent).empty()&&absent.phase_seen_==0,"missing observer Information cannot consume first occurrence");
    setup();cm::fuel=80000;Bot normal;observer_setup(normal);
    const auto normal_row=emit_observer(normal);
    check(normal_row=="p4 phase v=4 build=offline-actor-telemetry actor=0 round=10 name=observer before=100000 after=80000 truncated=1 layoutTruncated=0 stopStage=4 stopGroup=7 stopMember=2 groups=9 visited=4 collectionFuel=90000 stopFuel=85000 stopFloor=78000 stopMargin=8000\n",
        "actual observer capture, flush and writer produce exact v4 cached diagnostics");
    std::cout<<"schema-row "<<normal_row;
    check(normal.phase_seen_==1,"observer owns sampling bit zero");
    cm::round=11;normal.information_storage_->round=11;
    check(emit_observer(normal).empty(),"seen observer phase skips ordinary later rounds");
    cm::round=32;normal.information_storage_->round=32;
    normal.information_storage_->observation_truncated=false;
    normal.information_storage_->public_layout_truncated=true;
    normal.observer_.cached={};
    check(emit_observer(normal)=="p4 phase v=4 build=offline-actor-telemetry actor=0 round=32 name=observer before=100000 after=80000 truncated=0 layoutTruncated=1 stopStage=0 stopGroup=-1 stopMember=-1 groups=0 visited=0 collectionFuel=0 stopFuel=-1 stopFloor=0 stopMargin=0\n",
        "round32 samples reset diagnostics without retaining an earlier stop");
    setup();cm::round=11;Bot first;observer_setup(first);first.phase_seen_=62;
    check(!emit_observer(first).empty()&&first.phase_seen_==63,
        "first observer occurrence ignores other phase bits and ordinary sampling round");
    setup();Bot stale;observer_setup(stale);++cm::round;
    check(emit_observer(stale).empty()&&stale.phase_seen_==0,"stale observer clock preserves first occurrence");
    --cm::round;check(!emit_observer(stale).empty(),"stale omission remains eligible after clock agreement");
    setup();Bot low;observer_setup(low);cm::fuel=15999;
    check(emit_observer(low).empty()&&low.phase_seen_==0,"observer telemetry skips below16000");
    ++cm::round;low.information_storage_->round=cm::round;cm::fuel=16000;
    check(emit_observer(low).find("before=100000 after=16000 ")!=std::string::npos,
        "observer telemetry admits exactly16000");
    setup();Bot refilled;observer_setup(refilled);cm::fuel=100001;
    check(emit_observer(refilled).empty()&&refilled.phase_seen_==0,
        "observer telemetry skips a post-refill current fuel above its supplied start");
    ++cm::round;refilled.information_storage_->round=cm::round;
    cm::fuel=100000;check(!emit_observer(refilled).empty(),"equal observer start/current fuel is admitted");
    setup();Bot bounds;observer_setup(bounds);cm::fuel=std::numeric_limits<int>::max();
    auto& diagnostics=bounds.observer_.cached;
    diagnostics.collection_fuel=std::numeric_limits<std::uint64_t>::max();
    diagnostics.stop_floor=std::numeric_limits<std::uint64_t>::max();
    diagnostics.stop_fuel=std::numeric_limits<std::int64_t>::max();
    diagnostics.stop_margin=std::numeric_limits<unsigned>::max();
    const auto wide_row=emit_observer(bounds,std::numeric_limits<unsigned>::max());
    check(wide_row=="p4 phase v=4 build=offline-actor-telemetry actor=0 round=10 name=observer before=4294967295 after=2147483647 truncated=1 layoutTruncated=0 stopStage=4 stopGroup=7 stopMember=2 groups=9 visited=4 collectionFuel=18446744073709551615 stopFuel=9223372036854775807 stopFloor=18446744073709551615 stopMargin=4294967295\n",
        "observer writer preserves unsigned32/64 and signed64 boundaries");
    std::cout<<"wide-row "<<wide_row;
    setup();Bot negative;observer_setup(negative);cm::fuel=80000;
    negative.observer_.cached.stop_group=std::numeric_limits<int>::min();
    negative.observer_.cached.stop_member=std::numeric_limits<int>::min();
    negative.observer_.cached.stop_fuel=std::numeric_limits<std::int64_t>::min();
    const auto negative_row=emit_observer(negative);
    check(negative_row=="p4 phase v=4 build=offline-actor-telemetry actor=0 round=10 name=observer before=100000 after=80000 truncated=1 layoutTruncated=0 stopStage=4 stopGroup=-2147483648 stopMember=-2147483648 groups=9 visited=4 collectionFuel=90000 stopFuel=-9223372036854775808 stopFloor=78000 stopMargin=8000\n",
        "observer snapshot preserves signed group/member and stop-fuel minima");
    std::cout<<"wide-row "<<negative_row;
    setup();Bot zero;observer_setup(zero);cm::fuel=16000;
    check(emit_observer(zero,0).empty()&&zero.phase_seen_==0,"zero observer start cannot wrap the refill comparison");
    check(emit_observer(zero,15999).empty()&&zero.phase_seen_==0,"observer start below current fuel is omitted at reserve boundary");
    check(!emit_observer(zero,16000).empty(),"equal reserve/start boundary is admitted after omissions");
    std::puts("observer telemetry: actual v4 capture/flush/writer checks passed against cached stubs; collector tested separately");
}
void deferred_cases() {
    static_assert(std::tuple_size_v<decltype(plan4::PhaseSample{}.values)> == 12);
    static_assert(std::tuple_size_v<decltype(plan4::DeferredPhases{}.slots)> == 6);
    static_assert(std::tuple_size_v<decltype(plan4::DeferredPhases{}.order)> == 6);
    constexpr const char* actor_row="p4 phase v=3 build=offline-actor-telemetry actor=0 round=10 name=actors before=100000 after=99625 actorCount=1 ownObserved=1 rosterComplete=1 truncated=0 stopReason=0 minimumFuel=95200 stopFuel=-1\n";
    constexpr const char* observer_row="p4 phase v=4 build=offline-actor-telemetry actor=0 round=10 name=observer before=100000 after=80000 truncated=1 layoutTruncated=0 stopStage=4 stopGroup=7 stopMember=2 groups=9 visited=4 collectionFuel=90000 stopFuel=85000 stopFloor=78000 stopMargin=8000\n";
    setup();Bot owned;owned.actor_evidence_.start();capture_actor(owned);
    cm::fuel=80000;observer_setup(owned);capture_observer(owned);
    std::destroy_at(&owned.actor_evidence_);std::construct_at(&owned.actor_evidence_);
    owned.information_storage_.reset();owned.observer_.cached={};cm::fuel=20000;
    check(flush(owned)==std::string(actor_row)+observer_row,
        "owned actor/observer snapshots survive source mutation/destruction and later fuel change");
    check(owned.phase_seen_==33,"only emitted owned snapshots consume their first-occurrence bits");
    check(flush(owned).empty(),"a second flush cannot duplicate a row");

    setup();cm::round=32;Bot bounded;bounded.phase_seen_=63;
    bounded.actor_evidence_.start();observer_setup(bounded);
    capture_observer(bounded);capture_actor(bounded);repeat_captures(bounded);
    check(bounded.phase_samples_.count==2&&bounded.phase_samples_.pending_mask==33,
        "a thousand captures retain one bounded slot per actor/observer kind on round32");
    const auto duplicates=flush(bounded);
    check(duplicates.find("name=observer")<duplicates.find("name=actors")
        &&std::count(duplicates.begin(),duplicates.end(),'\n')==2,
        "deferred rows preserve first capture order");
    repeat_captures(bounded);
    check(flush(bounded).empty(),"sampling round32 still captures and emits each kind at most once");

    setup();Bot low;low.actor_evidence_.start();observer_setup(low);
    capture_actor(low);capture_observer(low);cm::fuel=15999;
    check(flush(low).empty()&&low.phase_seen_==0,"low-fuel flush omits every pending row without consuming first bits");
    ++cm::round;cm::fuel=100000;low.actor_evidence_.start();observer_setup(low);
    capture_actor(low);capture_observer(low);cm::fuel=16000;
    const auto recovered=flush(low);
    check(std::count(recovered.begin(),recovered.end(),'\n')==2&&low.phase_seen_==33,
        "exact16000 flush admits cached rows after earlier low-fuel omissions");

    setup();Bot reserve;reserve.actor_evidence_.start();observer_setup(reserve);
    capture_actor(reserve);capture_observer(reserve);
    const auto first_only=flush(reserve,true);
    check(first_only==actor_row&&reserve.phase_seen_==32,
        "flush checks16000 before every row and consumes only the emitted actor bit");
    ++cm::round;cm::fuel=80000;observer_setup(reserve);capture_observer(reserve);
    check(!flush(reserve).empty()&&reserve.phase_seen_==33,
        "a row omitted after earlier output retains its first-occurrence eligibility");

    setup();Bot stale;stale.actor_evidence_.start();observer_setup(stale);
    capture_actor(stale);capture_observer(stale);++cm::round;
    check(flush(stale).empty()&&stale.phase_seen_==0,"flush discards stale snapshots without consuming sampling bits");
    cm::fuel=100000;stale.actor_evidence_.start();observer_setup(stale);
    capture_actor(stale);capture_observer(stale);
    check(!flush(stale).empty()&&stale.phase_seen_==33,"stale omissions remain eligible in the new round");

    setup();Bot jump;jump.actor_evidence_.start();observer_setup(jump);
    capture_actor(jump);capture_observer(jump);
    check(flush(jump,false,true)==actor_row&&jump.phase_seen_==32,
        "round jump during flush suppresses remaining snapshots");
    cm::fuel=80000;observer_setup(jump);capture_observer(jump);
    check(!flush(jump).empty()&&jump.phase_seen_==33,"round-jump omission does not consume observer first occurrence");

    setup();Bot recapture;observer_setup(recapture);capture_observer(recapture);
    ++cm::round;recapture.actor_evidence_.start();capture_actor(recapture);
    const auto fresh=flush(recapture);
    check(fresh.find("round=11 name=actors")!=std::string::npos
        &&fresh.find("name=observer")==std::string::npos&&recapture.phase_seen_==32,
        "new-round capture discards the previous round's pending sample");
    std::puts("deferred telemetry: owned snapshots, bounded duplicates, reserve, stale clocks and per-row guards passed");
}
int main() {
    setup();Bot normal;normal.actor_evidence_.start();
    check(cm::queries==15&&cm::raw_queries==3,"actual collector keeps its cold data query count");
    const auto normal_row=emit(normal);
    check(normal_row=="p4 phase v=3 build=offline-actor-telemetry actor=0 round=10 name=actors before=100000 after=99625 actorCount=1 ownObserved=1 rosterComplete=1 truncated=0 stopReason=0 minimumFuel=95200 stopFuel=-1\n",
        "actual capture, flush and writer produce the exact v3 actor schema");
    std::cout<<"schema-row "<<normal_row;
    check(normal.phase_seen_==(1u<<5),"actors own sampling bit five");
    cm::round=11;cm::fuel=100000;normal.actor_evidence_.start();
    check(emit(normal).empty(),"seen actor phase skips ordinary later rounds");
    cm::round=32;cm::fuel=100000;normal.actor_evidence_.start();
    check(emit(normal)=="p4 phase v=3 build=offline-actor-telemetry actor=0 round=32 name=actors before=100000 after=99675 actorCount=1 ownObserved=1 rosterComplete=1 truncated=0 stopReason=0 minimumFuel=95200 stopFuel=-1\n",
        "seen actor phase samples round32 with the warm collection fuel");
    setup();cm::round=11;Bot first;first.phase_seen_=31;first.actor_evidence_.start();
    check(!emit(first).empty()&&first.phase_seen_==63,"first actor occurrence is independent of other phase bits and sample round");
    setup();Bot stale;stale.actor_evidence_.start();++cm::round;
    check(emit(stale).empty()&&stale.phase_seen_==0,"changed referee round skips actor telemetry without consuming first occurrence");
    --cm::round;check(!emit(stale).empty(),"a skipped first occurrence remains eligible");
    setup();Bot low;low.actor_evidence_.start();cm::fuel=15999;
    check(emit(low).empty()&&low.phase_seen_==0,"actor telemetry skips below16000");
    ++cm::round;cm::fuel=100000;low.actor_evidence_.start();cm::fuel=16000;
    check(emit(low).find("before=100000 after=16000 actorCount=1 ")!=std::string::npos,
        "16000 is admitted and before uses the cached collection fuel");
    setup();Bot topped;topped.actor_evidence_.start();cm::fuel=100001;
    check(emit(topped).empty()&&topped.phase_seen_==0,"fuel above cached start skips the row");
    ++cm::round;cm::fuel=100000;topped.actor_evidence_.start();cm::fuel=100000;
    check(!emit(topped).empty(),"equal start and current fuel remains admissible");
    setup();cm::fuel=70000;Bot exhausted;exhausted.actor_evidence_.start();
    check(emit(exhausted)=="p4 phase v=3 build=offline-actor-telemetry actor=0 round=10 name=actors before=70000 after=70000 actorCount=0 ownObserved=0 rosterComplete=0 truncated=1 stopReason=1 minimumFuel=71200 stopFuel=70000\n",
        "pre-roster budget stop keeps its cached cause and fuel");
    setup();cm::stop_after=9;Bot partial;partial.actor_evidence_.start();
    check(emit(partial)=="p4 phase v=3 build=offline-actor-telemetry actor=0 round=10 name=actors before=100000 after=60000 actorCount=0 ownObserved=1 rosterComplete=1 truncated=1 stopReason=1 minimumFuel=95200 stopFuel=60000\n",
        "partial collection row reports actual published frame and first budget stop");
    cm::reset();cm::query_cost=25;Bot unsupported;unsupported.actor_evidence_.start();
    check(emit(unsupported)=="p4 phase v=3 build=offline-actor-telemetry actor=0 round=10 name=actors before=100000 after=99950 actorCount=0 ownObserved=0 rosterComplete=0 truncated=1 stopReason=3 minimumFuel=95200 stopFuel=-1\n",
        "unsupported roster has no fabricated stop fuel");
    setup();cm::actors[2].id=7;Bot duplicate;duplicate.actor_evidence_.start();
    check(emit(duplicate).find("truncated=1 stopReason=4 minimumFuel=95200 stopFuel=-1\n")!=std::string::npos,
        "duplicate roster cause reaches the actual log writer");
    setup();cm::shift_after=9;Bot interrupted;interrupted.actor_evidence_.start();
    check(interrupted.actor_evidence_.stop_reason()==2&&emit(interrupted).empty(),
        "a collector round stop is not published under the new referee round");
    setup();cm::actors[0].alive=false;cm::shift_after=3;Bot dead;dead.actor_evidence_.start();
    check(dead.actor_evidence_.stop_reason()==0&&!dead.actor_evidence_.frame().truncated&&emit(dead).empty(),
        "dead-own early return retains its frame while stale telemetry is refused");
    observer_cases();
    deferred_cases();
    std::puts("actor telemetry: source-bound capture/flush checks passed; synthetic SDK data queries only");
}
'''


def parser_checks(output):
    sys.path.insert(0, str(ROOT / "bots/models/tools"))
    from telemetry import parse_telemetry

    version = "offline-actor-telemetry"
    stats = {"roster": {"champions": [{"id": 0, "team": "teamA", "name": "Ryth"}]},
             "match": {"roundsCompleted": 10, "width": 50, "height": 50}}
    prefix = (
        "p4 draft requested=0 completed=0\n"
        "p4 summary v=2 build=offline-actor-telemetry actor=0 round=1 fuel=90000 bag=0 score=0 active=0\n"
        "p4 decision v=2 build=offline-actor-telemetry model=m0 actor=0 round=1 job=1 kind=0 group=-1 "
        "second=-1 altar=-1 info=1 menu=1 candidates=1 evaluated=1 first=0 secured=0 z=0 terminalUnknown=0 "
        "fuelBefore=100000 fuelAfter=90000 fallback=none\n"
        "p4 input v=2 build=offline-actor-telemetry actor=0 round=1 job=1 x=0 y=0 bag=0 hp=1 maxHp=1 "
        "strength=1 focus=0 champion=0 monsterShards=0 jungleShards=0 groupId=-1 visible=0 healthStatus=0 "
        "groupHp=0 incoming=0 gems=0 nominal=0 abort=0\n"
    ).encode()
    prefix += b"".join(
        f"p4 phase v=2 build={version} actor=0 round=1 name={name} before=100000 after=90000\n".encode()
        for name in ("observer", "navigation", "menu", "evaluation"))

    def reject(row):
        try:
            parse_telemetry(prefix + row, stats, "teamA", version)
        except ValueError as exc:
            return str(exc)
        raise AssertionError("Out-of-schema diagnostic row was accepted")

    rows = [line.removeprefix(b"schema-row ") + b"\n" for line in output.splitlines()
            if line.startswith(b"schema-row ")]
    assert len(rows) == 2, "Actual writer must supply both actor and observer parser rows"
    for row in rows:
        parsed = parse_telemetry(prefix + row, stats, "teamA", version)
        expected = dict(token.split("=", 1) for token in row.decode().strip().split()[2:])
        expected = {key: value if key in ("build", "name") else int(value)
                    for key, value in expected.items()}
        assert parsed["rows"][-1] == {**expected, "rowType": "phase"}
        reject(row.replace(b" build=offline-actor-telemetry", b""))
        reject(row.rstrip(b"\n"))
        reject(row.rstrip(b"\n") + b" extra=1\n")
    wide = [line.removeprefix(b"wide-row ") + b"\n" for line in output.splitlines()
            if line.startswith(b"wide-row ")]
    assert len(wide) == 2, "Actual writer must supply unsigned64 and signed64 boundary rows"
    for row in wide:
        assert reject(row) == "Invalid observer diagnostics", "Parser retains its existing diagnostic bounds"
    print("actor/observer parser: actual v3/v4 schemas accepted; preserved wide values rejected by existing bounds")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--node", required=True)
    args = parser.parse_args()
    parent = (ROOT / ".cache/models-tests/actor-telemetry").resolve()
    parent.mkdir(parents=True, exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix="fixture-", dir=parent)).resolve()
    if scratch.parent != parent:
        raise RuntimeError("scratch escaped actor telemetry cache")
    print(f"Scratch: {scratch}", flush=True)
    try:
        actor_header = ROOT / "bots/models/common/actor-evidence.h"
        observer_header = ROOT / "bots/models/common/observer.h"
        telemetry_header = ROOT / "bots/models/common/telemetry.h"
        phase_header = ROOT / "bots/models/common/phase-diagnostics.h"
        macro_header = ROOT / "bots/models/common/macro-methods.h"
        parser_source = ROOT / "bots/models/tools/telemetry.py"
        sdk = ROOT / "bots/models/tests/fixtures/plan4-online-sdk"
        inputs = {path: path.read_bytes() for path in {
            Path(__file__).resolve(), actor_header, observer_header, telemetry_header, phase_header,
            macro_header, parser_source, sdk / "cambc.h",
        }}
        emitter = method(inputs[macro_header].decode(), "void macro_actor_phase(")
        assert emitter.count("cambc::fuel_remaining()") == 1, "Actor capture must read current fuel once"
        observer_emitter = method(inputs[macro_header].decode(), "void macro_observer_phase(")
        assert observer_emitter.count("cambc::fuel_remaining()") == 1, "Observer capture must read current fuel once"
        assert observer_emitter.count("cambc::round_number()") == 2, "Observer capture must recheck its source round"
        flush_emitter = method(inputs[macro_header].decode(), "void macro_flush_phases(")
        assert "LogRow" not in emitter + observer_emitter, "Capture paths cannot format optional rows"
        assert "phase_seen_ |=" not in emitter + observer_emitter, "Capture cannot consume first-occurrence bits"
        cpp, wasm = scratch / "actor.cpp", scratch / "actor.wasm"
        compile_log, stdout_log, stderr_log = (scratch / name for name in
            ("compile.log", "stdout.log", "stderr.log"))
        cpp.write_text(PRELUDE.replace("ACTOR_HEADER", actor_header.as_posix())
            .replace("OBSERVER_HEADER", observer_header.as_posix())
            .replace("TELEMETRY_HEADER", telemetry_header.as_posix())
            .replace("PHASE_HEADER", phase_header.as_posix())
            + emitter + "\n" + observer_emitter + "\n" + flush_emitter + TEST)
        eh = Path(args.compiler).resolve().parents[1] / "share/wasi-sysroot/lib/wasm32-wasip1/eh"
        command = [args.compiler, "-std=c++20", "-O3", "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
            "-fwasm-exceptions", "-mllvm", "-wasm-use-legacy-eh=false", "--no-wasm-opt",
            f"-L{eh}", "-lunwind", f"-I{sdk}", str(cpp), "-o", str(wasm)]
        compiled = subprocess.run(command, capture_output=True, cwd=ROOT)
        compile_log.write_bytes(compiled.stdout + compiled.stderr)
        if compiled.returncode:
            print((compiled.stdout + compiled.stderr).decode(errors="replace"), file=sys.stderr, end="")
        compiled.check_returncode()
        run = subprocess.run([args.node, "--no-warnings", str(ROOT / "tools/arena/wasi-run.mjs"), str(wasm)],
            capture_output=True, cwd=ROOT)
        stdout_log.write_bytes(run.stdout)
        stderr_log.write_bytes(run.stderr)
        print(run.stdout.decode(errors="replace"), end="")
        print(run.stderr.decode(errors="replace"), file=sys.stderr, end="")
        run.check_returncode()
        parser_checks(run.stdout)
        assert all(path.read_bytes() == raw for path, raw in inputs.items()), "Fixture input changed during check"
        expected_files = {cpp, wasm, compile_log, stdout_log, stderr_log}
        assert scratch.parent == parent and scratch.name.startswith("fixture-")
        assert set(scratch.iterdir()) == expected_files and all(path.is_file() for path in expected_files)
        shutil.rmtree(scratch)
        if scratch.exists():
            raise RuntimeError("scratch cleanup failed")
        print("Verified actor telemetry scratch cleanup", flush=True)
    except BaseException:
        print(f"FAILED: retained scratch and logs at {scratch}", flush=True)
        raise


if __name__ == "__main__":
    main()
