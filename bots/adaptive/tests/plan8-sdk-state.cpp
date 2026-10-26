#include "../common/src/context.h"
#include "../common/src/episode.h"
#define P8_SDK_STATE_ONLY
#include "../common/src/sdk.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>

// These are synthetic, pure C++ records of SDK-shaped float values. They do
// not import the SDK, run a native game, or claim native receipt attribution.
using namespace p8;
namespace {
int checks = 0;
void check(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
constexpr double infinity = std::numeric_limits<double>::infinity();
constexpr double nan = std::numeric_limits<double>::quiet_NaN();
const double positive_minimum = std::nextafter(0.0,infinity);
const std::array<double,3> nonfinite{nan,infinity,-infinity};
constexpr std::array<double EntryBuild::*,4> build_fields{
    &EntryBuild::max_health,&EntryBuild::strength,&EntryBuild::focus,&EntryBuild::growth};
constexpr std::array<double NumericState::*,5> state_fields{
    &NumericState::health,&NumericState::max_health,&NumericState::strength,
    &NumericState::focus,&NumericState::growth};
constexpr std::array<NumericRange TransitionEnvelope::*,6> envelope_fields{
    &TransitionEnvelope::health,&TransitionEnvelope::max_health,&TransitionEnvelope::strength,
    &TransitionEnvelope::focus,&TransitionEnvelope::growth,&TransitionEnvelope::bag};

static_assert(std::is_same_v<decltype(EntryBuild::max_health),double>);
static_assert(std::is_same_v<decltype(EntryBuild::strength),double>);
static_assert(std::is_same_v<decltype(EntryBuild::focus),double>);
static_assert(std::is_same_v<decltype(EntryBuild::growth),double>);
static_assert(std::is_same_v<decltype(NumericRange::lower),double>);
static_assert(std::is_same_v<decltype(NumericRange::upper),double>);
static_assert(std::is_same_v<decltype(NumericState::health),double>);
static_assert(std::is_same_v<decltype(NumericState::max_health),double>);
static_assert(std::is_same_v<decltype(NumericState::strength),double>);
static_assert(std::is_same_v<decltype(NumericState::focus),double>);
static_assert(std::is_same_v<decltype(NumericState::growth),double>);
static_assert(std::is_same_v<decltype(NumericState::bag),std::int64_t>);

ContextDeclaration declaration(HealthBand band = HealthBand::below_half) {
    ContextDeclaration d;
    d.key.settings_id = 17; d.key.route_hash = 1234;
    d.key.actor_id = 1; d.key.champion = 0; d.key.role = 0;
    d.key.source_group = 7; d.key.route_template = 2;
    d.key.endpoint = 8; d.key.threat_regime = 0;
    d.key.participants[0] = 1; d.key.participant_count = 1;
    // float -> double is exact; no integer conversion belongs at this boundary.
    d.key.build = {99,double(100.5f),double(12.25f),double(6.125f),double(1.5f)};
    d.key.hp_band = band;
    const double maximum = d.key.build.max_health;
    const double half = maximum * .5, three_quarters = maximum * .75;
    d.envelope.health = band == HealthBand::three_quarters_to_full ? NumericRange{three_quarters,maximum} :
        band == HealthBand::half_to_three_quarters ? NumericRange{half,std::nextafter(three_quarters,0.0)} :
        NumericRange{positive_minimum,std::nextafter(half,0.0)};
    d.envelope.max_health = {maximum,maximum};
    d.envelope.strength = {12.25,12.25}; d.envelope.focus = {6.125,6.125};
    d.envelope.growth = {1.5,1.5}; d.envelope.bag = {0,100};
    return d;
}
Premises premises(const ContextDeclaration& d, double health = double(37.5f)) {
    const auto& b = d.key.build;
    return {d.key,{health,b.max_health,b.strength,b.focus,b.growth,0,b.equipment_hash},all_premises};
}
bool predeclares(const ContextDeclaration& d) {
    auto ledger = std::make_unique<EvidenceLedger>(d.key.actor_id);
    return ledger->predeclare(d) == 0;
}
EpisodeRequest request(int slot, const ContextDeclaration& d) {
    EpisodeRequest r;
    r.context = slot; r.issue_round = 1; r.deadline = 31;
    r.action = 9; r.nominal_duration = 10; r.premises = premises(d);
    return r;
}

void exact_fractional_values() {
    const auto d = declaration();
    const auto p = premises(d);
    check(valid(d.key) && valid_numeric_state(p.state),"finite fractional key and numeric snapshot are valid");
    check(p.state.health == double(37.5f) && p.state.max_health == double(100.5f),
        "health and maximum preserve exact SDK float values");
    check(p.state.strength == double(12.25f) && p.state.focus == double(6.125f) &&
        p.state.growth == double(1.5f),"strength, focus and growth preserve exact SDK float values");
    check(p.state.health != std::trunc(p.state.health) &&
        p.state.max_health != std::trunc(p.state.max_health) &&
        p.state.strength != std::trunc(p.state.strength) &&
        p.state.focus != std::trunc(p.state.focus) && p.state.growth != std::trunc(p.state.growth),
        "fractional records are never truncated to integer stats");
    check(d.envelope.contains(p.state,d.key.build),"fractional entry fits its frozen envelope");
    check(applicability(d.key,d.envelope,p,true) == Applicability::applicable,
        "exact fractional entry matches its context");
}

void continuous_health_bands() {
    const double maximum = 100.5, half = maximum * .5, three_quarters = maximum * .75;
    check(health_band(positive_minimum,maximum) == HealthBand::below_half,
        "smallest positive double health remains below half");
    check(health_band(std::nextafter(half,0.0),maximum) == HealthBand::below_half,
        "one double below half stays below half");
    check(health_band(half,maximum) == HealthBand::half_to_three_quarters,
        "exact fractional half starts middle band");
    check(health_band(std::nextafter(half,infinity),maximum) == HealthBand::half_to_three_quarters,
        "one double above half stays in middle band");
    check(health_band(std::nextafter(three_quarters,0.0),maximum) == HealthBand::half_to_three_quarters,
        "one double below three quarters stays in middle band");
    check(health_band(three_quarters,maximum) == HealthBand::three_quarters_to_full,
        "exact fractional three quarters starts upper band");
    check(health_band(std::nextafter(three_quarters,infinity),maximum) == HealthBand::three_quarters_to_full,
        "one double above three quarters stays in upper band");
    check(health_band(maximum,maximum) == HealthBand::three_quarters_to_full,
        "fractional full health stays in upper band");
}

void range_validation() {
    const NumericRange range{6.125,12.25};
    check(range.valid() && range.contains(6.125) && range.contains(9.375) && range.contains(12.25),
        "finite fractional ranges include both exact endpoints");
    check(!range.contains(std::nextafter(range.lower,0.0)) &&
        !range.contains(std::nextafter(range.upper,infinity)),"fractional ranges exclude adjacent outside values");
    check(NumericRange{0,0}.valid() && NumericRange{0,0}.contains(0),"zero range is valid for non-health stats");
    for (double value : nonfinite) {
        check(!range.contains(value),"finite range rejects nonfinite observed value");
        const NumericRange bad_lower{value,12.25}, bad_upper{6.125,value};
        check(!bad_lower.valid() && !bad_lower.contains(9.375),"nonfinite lower bound invalidates containment");
        check(!bad_upper.valid() && !bad_upper.contains(9.375),"nonfinite upper bound invalidates containment");
    }
    check(!NumericRange{-0.125,12.25}.valid() && !NumericRange{-0.125,12.25}.contains(9.375),
        "negative lower bound cannot admit a finite value");
    check(!NumericRange{12.25,6.125}.valid() && !NumericRange{12.25,6.125}.contains(9.375),
        "reversed fractional range is invalid");
    check(!range.contains(-0.125),"range rejects negative observed value");
}

void key_and_state_validation() {
    const auto d = declaration();
    const auto p = premises(d);
    for (auto field : build_fields) {
        for (double value : nonfinite) {
            auto key = d.key; key.build.*field = value;
            check(!valid(key),"key rejects each nonfinite build component");
            auto bad_declaration = d; bad_declaration.key = key;
            check(!predeclares(bad_declaration),"nonfinite build cannot be predeclared");
        }
        auto key = d.key; key.build.*field = -0.125;
        check(!valid(key),"key rejects negative fractional build component");
    }
    auto key = d.key; key.build.max_health = 0;
    check(!valid(key),"zero maximum health invalidates key");
    key = d.key; key.build.strength = key.build.focus = key.build.growth = 0;
    check(valid(key),"zero non-health build stats remain legal");
    for (auto field : state_fields) {
        for (double value : nonfinite) {
            auto bad = p; bad.state.*field = value;
            check(!valid_numeric_state(bad.state),"snapshot rejects each nonfinite numeric component");
            check(!d.envelope.contains(bad.state,d.key.build),"envelope rejects nonfinite numeric snapshot");
            check(applicability(d.key,d.envelope,bad,false) == Applicability::changed,
                "nonfinite numeric snapshot cannot remain applicable");
        }
        auto bad = p; bad.state.*field = -0.125;
        check(!valid_numeric_state(bad.state) && !d.envelope.contains(bad.state,d.key.build),
            "snapshot and envelope reject negative fractional numeric component");
    }
    auto state = p.state; state.max_health = 0;
    check(!valid_numeric_state(state),"zero snapshot maximum health is invalid");
    state = p.state; state.health = std::nextafter(state.max_health,infinity);
    check(!valid_numeric_state(state),"health above fractional maximum is invalid");
    state = p.state; state.bag = -1;
    check(!valid_numeric_state(state) && !d.envelope.contains(state,d.key.build),"negative integer bag is invalid");
    state = p.state; state.health = 0;
    check(valid_numeric_state(state) && !d.envelope.contains(state,d.key.build),
        "dead snapshot is numeric data but cannot fit a positive-health episode");
    state = p.state; state.strength = state.focus = state.growth = 0;
    check(valid_numeric_state(state),"zero non-health snapshot stats remain valid");
}

void envelope_validation() {
    const auto d = declaration();
    const auto p = premises(d);
    for (auto field : envelope_fields) {
        for (double value : nonfinite) {
            auto e = d.envelope; (e.*field).lower = value;
            check(!e.valid() && !e.contains(p.state,d.key.build),"nonfinite envelope lower bound cannot admit state");
            e = d.envelope; (e.*field).upper = value;
            check(!e.valid() && !e.contains(p.state,d.key.build),"nonfinite envelope upper bound cannot admit state");
        }
        auto bad = d; (bad.envelope.*field).lower = -0.125;
        check(!bad.envelope.valid() && !predeclares(bad),"negative envelope bound cannot be predeclared");
    }
    auto bad = d; bad.envelope.health.lower = 0;
    check(!bad.envelope.valid() && !predeclares(bad),"episode health envelope is strictly positive");
    bad = d; bad.envelope.max_health.lower = 0;
    check(!bad.envelope.valid() && !predeclares(bad),"episode maximum-health envelope is strictly positive");
}

void entry_and_transition_matching() {
    auto d = declaration(HealthBand::three_quarters_to_full);
    d.envelope.health.lower = positive_minimum;
    d.envelope.strength.upper = 13.75;
    const auto entry = premises(d,87.625);
    check(predeclares(d),"declared fractional envelope includes damage and planned stat change");
    check(applicability(d.key,d.envelope,entry,true) == Applicability::applicable,
        "fractional high-health entry is exact");
    auto changed = entry; changed.state.health = 37.5;
    check(applicability(d.key,d.envelope,changed,false) == Applicability::applicable,
        "in-envelope fractional damage may cross entry band during episode");
    check(applicability(d.key,d.envelope,changed,true) == Applicability::changed,
        "cross-band damage cannot silently count as original entry");
    changed = entry; changed.state.strength = 12.5;
    check(applicability(d.key,d.envelope,changed,false) == Applicability::applicable,
        "predeclared fractional stat transition is usable within episode");
    check(applicability(d.key,d.envelope,changed,true) == Applicability::changed,
        "fractional stat transition cannot be rounded into original entry");
    for (auto field : build_fields) {
        changed = entry; changed.key.build.*field = std::nextafter(d.key.build.*field,infinity);
        check(applicability(d.key,d.envelope,changed,true) == Applicability::changed,
            "one-double build identity change cannot be rounded away");
    }
    changed = entry; changed.state.strength = std::nextafter(13.75,infinity);
    check(applicability(d.key,d.envelope,changed,false) == Applicability::changed,
        "one-double transition beyond declared bound invalidates episode");
}

void complete_band_declarations() {
    for (auto band : {HealthBand::below_half,HealthBand::half_to_three_quarters,HealthBand::three_quarters_to_full}) {
        const auto d = declaration(band);
        check(predeclares(d),"exact continuous entry-band envelope can be predeclared");
        auto missing = d;
        missing.envelope.health.lower = std::nextafter(d.envelope.health.lower,infinity);
        check(!predeclares(missing),"missing one lower endpoint double rejects declaration");
        missing = d;
        missing.envelope.health.upper = std::nextafter(d.envelope.health.upper,0.0);
        check(!predeclares(missing),"missing one upper endpoint double rejects declaration");
    }
    auto rounded = declaration(HealthBand::half_to_three_quarters);
    rounded.envelope.health = {51,75};
    check(!predeclares(rounded),"integer-rounded envelope cannot cover fractional middle-band entry");
    rounded = declaration(HealthBand::three_quarters_to_full);
    rounded.envelope.health = {76,100.5};
    check(!predeclares(rounded),"integer-rounded envelope cannot cover fractional upper-band entry");
}

void synthetic_episode() {
    auto d = declaration(); d.require_spawn_shop = true;
    auto ledger = std::make_unique<EvidenceLedger>(1);
    const int slot = ledger->predeclare(d);
    check(slot == 0,"fractional complete-cycle context predeclares");
    auto invalid = request(slot,d); invalid.premises.state.focus = nan;
    check(ledger->issue(invalid) == 0,"nonfinite entry cannot issue an episode");
    const auto serial = ledger->issue(request(slot,d));
    check(serial != 0,"finite fractional entry issues an episode");
    check(ledger->active() && ledger->active()->premises.state.health == double(37.5f) &&
        ledger->active()->premises.state.focus == double(6.125f),"issued episode retains fractional entry without rounding");
    Observation o;
    o.round = 11; o.kind = ObservationKind::completed;
    o.premises = premises(d,30.125);
    o.duration = {{10,10},DurationSource::personally_exact};
    auto& receipts = o.receipts;
    receipts.source = ReceiptSource::own_observation;
    receipts.production = receipts.payout_owned = receipts.conversion = receipts.renewed = true;
    receipts.attribution_excludes_other_bag_loss = true;
    receipts.payout_event = 1; receipts.conversion_event = 2; receipts.renewal_event = 3;
    receipts.production_round = 2; receipts.payout_round = 3;
    receipts.conversion_round = 5; receipts.spawn_round = 8; receipts.renewal_round = 11;
    receipts.positive_reward_lower = 10;
    receipts.spawn_visited = receipts.spawn_shop_legal = true;
    const auto result = ledger->resolve(serial,o);
    check(result.accepted && result.applicable && result.eligible && result.full_positive &&
        result.status == Status::completed,"typed synthetic own-cycle receipts resolve fractional episode");
    const auto* state = ledger->state(slot);
    check(state && state->completed == 1 && state->positive_completions == 1 && state->failures == 0,
        "fractional completion updates positive evidence without a numeric failure");
}

void exact_integer_bags() {
    constexpr std::int64_t exact=9007199254740992LL;
    const NumericRange upper{0,double(exact)};
    check(upper.contains_bag(exact) && !upper.contains_bag(exact+1),
        "bag above 2^53 cannot round down through a double upper bound");
    check(!NumericRange{double(exact+2),double(exact+4)}.contains_bag(exact+1),
        "bag below a large lower bound cannot round into its integer interval");
    check(NumericRange{0,9223372036854775808.}.contains_bag(std::numeric_limits<std::int64_t>::max()),
        "finite 2^63 upper bound includes int64 maximum without an overflowing cast");
    check(!NumericRange{9223372036854775808.,9223372036854775808.}.contains_bag(std::numeric_limits<std::int64_t>::max()),
        "2^63 lower bound rejects every signed int64 bag");
    const NumericRange fractional{1.25,3.75};
    check(!fractional.contains_bag(1) && fractional.contains_bag(2) && fractional.contains_bag(3) && !fractional.contains_bag(4),
        "fractional bag bounds use exact integer ceiling and floor");
    check(!NumericRange{nan,10}.contains_bag(5) && !NumericRange{0,infinity}.contains_bag(5) && !upper.contains_bag(-1),
        "invalid bounds and negative bags cannot pass discrete containment");
    auto d=declaration(); auto state=premises(d).state;
    d.envelope.bag=upper; state.bag=exact+1;
    check(!d.envelope.contains(state,d.key.build),"transition applicability uses the exact discrete bag comparison");
}

p8::sdk::TributeSample tribute_sample(int round,std::int64_t bag,bool channel=true) {
    return {{round,1,0},{4,0},bag,true,false,channel,false,true};
}
void causal_own_tribute() {
    using p8::sdk::OwnTributeWatch;
    OwnTributeWatch own;
    check(own.begin(tribute_sample(10,12),7,2),"own receipt starts only from a real nonempty legal own channel");
    check(!own.observe(tribute_sample(10,12)).current({10,1,0},1),"duplicate start snapshot is not own conversion");
    check(!own.observe(tribute_sample(11,12)).current({11,1,0},1),"unfinished channel supplies no bank receipt");
    check(!own.observe(tribute_sample(12,24)).current({12,1,0},1),"capture entering an existing channel remains unconverted inventory");
    auto bank=own.observe(tribute_sample(13,0,false));
    check(bank.current({13,1,0},1) && bank.value.own_bank && bank.value.group==7 && bank.value.bank_positive_lower==12,
        "continuous actual own tribute completion yields its justified starting-bag lower bound without score attribution");
    check(!own.observe(tribute_sample(13,0,false)).current({13,1,0},1),"a completed own conversion cannot be replayed");
    for (int fault=0;fault<9;++fault) {
        OwnTributeWatch invalid; check(invalid.begin(tribute_sample(10,12),7,2),"negative own tribute trace starts from same legal channel");
        auto later=tribute_sample(11,12);
        if (fault==0) later.alive=false;
        if (fault==1) later.stunned=true;
        if (fault==2) later.cancelled=true;
        if (fault==3) later.altar_visible_legal=false;
        if (fault==4) later.clock.round=12;
        if (fault==5) later.position={5,0};
        if (fault==6) later.clock.self=2;
        if (fault==7) later.bag=6;
        if (fault==8) { later.tribute=false; later.bag=0; }
        check(!invalid.observe(later).value.own_bank && !invalid.active,
            "death stun cancel unknown lock missed turn displacement foreign actor other bag loss or early endpoint censors own attribution");
        check(!invalid.observe(tribute_sample(13,0,false)).value.own_bank,"later bag disappearance cannot repair a broken causal channel");
    }
    auto no_start=tribute_sample(10,12,false); OwnTributeWatch absent;
    check(!absent.begin(no_start,7,2) && !absent.observe(tribute_sample(13,0,false)).value.own_bank,
        "bag disappearance without an actual own started channel supplies no conversion receipt");
}
struct ObservedQueueFixture {
    p6::Frame frame{};
    p6::Schedule schedule{};
    std::array<p6::Point,2> approaches{{{4,0},{5,0}}};
    ObservedQueueFixture() {
        frame.clock={10,1,3};frame.own.actor.id=1;
        frame.own.actor.team=p6::Team::ally;frame.own.actor.alive=true;
        frame.own.actor.position={0,0};schedule.actor_count=1;schedule.actors[0].actor_id=1;
    }
    void seen(p6::Actor actor) {
        frame.actors[frame.actor_count++]={actor,frame.clock,actor.id,p6::Knowledge::exact};
    }
};
// Independent interpretation of the old live query: only the returned visible
// live ally list, represented actor IDs and exact approach positions matter.
bool legacy_visible_approaches_clear(const ObservedQueueFixture& f) {
    const auto clear=[&](const p6::Actor& actor) {
        if(actor.team!=p6::Team::ally||!actor.alive)return true;
        bool represented=false;
        for(int i=0;i<f.schedule.actor_count;++i)represented|=f.schedule.actors[i].actor_id==actor.id;
        if(represented)return true;
        for(const auto approach:f.approaches)if(actor.position==approach)return false;
        return true;
    };
    if(!clear(f.frame.own.actor))return false;
    for(int i=0;i<f.frame.actor_count;++i)if(!clear(f.frame.actors[i].value))return false;
    return true;
}
void observed_allied_queue_boundary() {
    const auto run=[](ObservedQueueFixture& f,bool expected) {
        auto before=std::make_unique<std::array<unsigned char,sizeof(ObservedQueueFixture)>>();
        std::memcpy(before->data(),&f,sizeof(f));
        check(sdk::exact_actor_observations(f.frame,f.frame.clock),"queue oracle uses exact current active observations");
        const bool actual=sdk::observed_allied_approaches_clear(f.frame,f.frame.clock,f.schedule,f.approaches);
        check(actual==expected&&actual==legacy_visible_approaches_clear(f),
            "observed occupancy matches the original visible alive unrepresented ally rule");
        check(std::memcmp(before->data(),&f,sizeof(f))==0,"observed queue guard never mutates borrowed frame or schedule");
    };
    auto f=std::make_unique<ObservedQueueFixture>();run(*f,true);
    p6::Actor other;other.id=2;other.team=p6::Team::ally;other.alive=true;other.position={4,0};
    f->seen(other);run(*f,false);
    for(const auto channel:{p6::Channel::none,p6::Channel::tribute,p6::Channel::recall,p6::Channel::teleport}) {
        f->frame.actors[0].value.channel=channel;f->frame.actors[0].value.channel_valid=true;
        f->frame.actors[0].value.can_move=false;f->frame.actors[0].value.completed_turn=true;
        run(*f,false);
    }
    f->schedule.actor_count=2;f->schedule.actors[1].actor_id=2;run(*f,true);
    f->schedule.actor_count=1;f->frame.actors[0].value.alive=false;run(*f,true);
    f->frame.actors[0].value.alive=true;f->frame.actors[0].value.team=p6::Team::enemy;run(*f,true);
    f->frame.actors[0].value.team=p6::Team::ally;f->frame.actors[0].value.position={5,0};run(*f,false);
    f->frame.actors[0].value.position={6,0};run(*f,true);
    f->frame.own.actor.position={4,0};f->schedule.actors[0].actor_id=2;run(*f,false);
    f->schedule.actors[0].actor_id=1;run(*f,true);
    // Hidden actors are absent from the live visible list too. An inactive tail
    // must neither supply a blocker nor invalidate a complete active prefix.
    f->frame.actor_count=0;f->frame.actors[0]={other,{-1,-1,-1},-1,p6::Knowledge::stale};run(*f,true);
    f=std::make_unique<ObservedQueueFixture>();
    for(int i=0;i<p6::max_actors;++i) {other.id=i+2;other.position={i+10,0};f->seen(other);}
    run(*f,true);
    f->frame.actors[p6::max_actors-1].value.position={5,0};run(*f,false);
    // Frame::valid deliberately permits stale/unknown entries. The observed
    // shortcut requires every retained active observation to be current.
    for(const auto knowledge:{p6::Knowledge::unknown,p6::Knowledge::stale}) {
        f->frame.actors[0].knowledge=knowledge;
        check(f->frame.valid()&&!sdk::exact_actor_observations(f->frame,f->frame.clock)&&
            !sdk::observed_allied_approaches_clear(f->frame,f->frame.clock,f->schedule,f->approaches),
            "noncurrent active entries cannot authenticate the observed shortcut");
    }
    f->frame.actors[0].knowledge=p6::Knowledge::exact;
    for(int field=0;field<3;++field) {
        auto clock=f->frame.clock;
        if(field==0)++clock.round;if(field==1)++clock.self;if(field==2)++clock.revision;
        check(!sdk::observed_allied_approaches_clear(f->frame,clock,f->schedule,f->approaches),
            "changed round self or revision rejects old observed occupancy");
    }
    ++f->frame.actors[0].clock.revision;
    check(!sdk::exact_actor_observations(f->frame,f->frame.clock),"changed observation revision cannot refresh an old forecast");
    f->frame.actors[0].clock=f->frame.clock;
    f->frame.actors[0].value.id=99;
    check(!sdk::exact_actor_observations(f->frame,f->frame.clock),"subject and actor identity must agree");
    f->frame.actors[0].value.id=2;f->frame.actors[0].value.team=p6::Team::unknown;
    check(!sdk::exact_actor_observations(f->frame,f->frame.clock),"unknown observed side cannot hide a blocking ally");
    f->frame.actors[0].value.team=p6::Team::ally;
    f->schedule.actor_count=p6::max_actors+1;
    check(!sdk::observed_allied_approaches_clear(f->frame,f->frame.clock,f->schedule,f->approaches),
        "invalid schedule count is rejected before accessing its active prefix");
}
} // namespace

int main() {
    {
        using p8::sdk::verified_spawn_shop_visit;
        check(verified_spawn_shop_visit({true,true,false,false,1}),
            "actual host item-shop permission at spawn is valid without a Tavi sale or affordable item");
        check(!verified_spawn_shop_visit({true,true,false,false,0}),"host denial cannot become a legal shop visit");
        check(!verified_spawn_shop_visit({true,false,false,false,1}),"Tavi field shopping is not a spawn visit");
        check(!verified_spawn_shop_visit({false,true,false,false,1}),"shopping while dead cannot qualify an actual spawn visit");
        check(!verified_spawn_shop_visit({true,true,true,false,1})&&
            !verified_spawn_shop_visit({true,true,false,true,1}),"stun or an active channel blocks the declared legal spawn endpoint");
    }
    exact_fractional_values();
    continuous_health_bands();
    range_validation();
    key_and_state_validation();
    envelope_validation();
    entry_and_transition_matching();
    complete_band_declarations();
    synthetic_episode();
    exact_integer_bags();
    causal_own_tribute();
    observed_allied_queue_boundary();
    std::printf("plan8 SDK-shaped numeric state: %d checks passed\n",checks);
}
