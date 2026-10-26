#include "../common/src/episode.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>

using namespace p8;
namespace {
int checks = 0;
void check(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
ContextDeclaration declaration(int source = 7, EpisodeKind kind = EpisodeKind::return_component) {
    ContextDeclaration d;
    d.key.settings_id = 17; d.key.route_hash = 1234;
    d.key.actor_id = 1; d.key.champion = 0; d.key.role = 0;
    d.key.source_group = source; d.key.route_template = 2;
    d.key.endpoint = 8; d.key.threat_regime = 0;
    d.key.participants[0] = 1; d.key.participant_count = 1;
    d.key.build = {99,100,20,10,1};
    d.envelope.health = {20,100}; d.envelope.max_health = {100,100};
    d.envelope.strength = {20,20}; d.envelope.focus = {10,10};
    d.envelope.growth = {1,1}; d.envelope.bag = {0,100};
    d.kind = kind;
    if (kind == EpisodeKind::full_organization) {
        d.key.participants[1] = 2; d.key.participant_count = 2;
        d.key.role = 1;
    }
    return d;
}
Premises premises(const ContextDeclaration& d) {
    return {d.key,{100,100,20,10,1,0,99},all_premises};
}
EpisodeRequest request(int slot, const ContextDeclaration& d, int start) {
    EpisodeRequest r;
    r.context = slot; r.issue_round = start; r.deadline = start + 30;
    r.action = 9; r.nominal_duration = 10; r.premises = premises(d);
    return r;
}
Observation success(const ContextDeclaration& d, int start, int event, int duration = 10) {
    Observation o;
    o.round = start + duration; o.kind = ObservationKind::completed;
    o.premises = premises(d);
    o.duration = {{duration,duration},DurationSource::personally_exact};
    auto& c = o.receipts;
    c.source = ReceiptSource::own_observation;
    c.production = c.payout_owned = c.conversion = c.renewed = true;
    c.attribution_excludes_other_bag_loss = true;
    c.payout_event = c.conversion_event = c.renewal_event = event;
    c.production_round = start + 1; c.payout_round = start + 2;
    c.conversion_round = start + 4; c.renewal_round = o.round;
    c.positive_reward_lower = 10;
    return o;
}
Resolution cycle(EvidenceLedger& l, int slot, const ContextDeclaration& d, int start, int event,
                 int duration = 10) {
    const auto serial = l.issue(request(slot,d,start));
    check(serial != 0, "fixture issues before own action");
    return l.resolve(serial,success(d,start,event,duration));
}
struct Qualified {
    ContextDeclaration solo = declaration(), supported = declaration(7,EpisodeKind::full_organization);
    EvidenceLedger ledger{1};
    int component = ledger.predeclare(solo), organization = ledger.predeclare(supported);
    Qualified() {
        check(cycle(ledger,component,solo,1,1).full_positive, "first solo full cycle");
        check(cycle(ledger,component,solo,12,2).full_positive, "second solo full cycle");
    }
    void commission() {
        CampaignAdmission a{premises(supported),true,true,true};
        check(ledger.begin_organization_campaign(component,organization,23,a), "component permits one actual H campaign");
        check(cycle(ledger,organization,supported,24,3).full_positive, "first H actual full cycle");
        check(cycle(ledger,organization,supported,35,4).full_positive, "second H actual full cycle");
        check(ledger.organization_qualified(organization,supported.key,45), "H now qualifies separately");
    }
};
__attribute__((noinline)) void card_endpoint_guards() {
    for (int guard=0;guard<3;++guard) {
        auto d=declaration(); d.require_spawn_shop=true;
        auto l=std::make_unique<EvidenceLedger>(1); const int slot=l->predeclare(d);
        const auto serial=l->issue(request(slot,d,1)); auto o=success(d,1,1);
        o.receipts.spawn_visited=o.receipts.spawn_shop_legal=true; o.receipts.spawn_round=8;
        if (guard==0) o.receipts.spawn_shop_legal=false;
        if (guard==1) { o.receipts.purchase_attempted=true; o.receipts.purchase_confirmed=false; }
        if (guard==2) o.receipts.spawn_round=3; // Before the confirmed own tribute at round5.
        check(serial && !l->resolve(serial,o).full_positive,
              "P8-02 component rejects unknown shop, unconfirmed attempted purchase or pre-bank spawn receipt");
    }
    auto d=declaration(), h=declaration(7,EpisodeKind::full_organization);
    d.require_spawn_shop=h.require_spawn_shop=true; h.require_supported_work=true;
    auto l=std::make_unique<EvidenceLedger>(1);
    const int component=l->predeclare(d), organization=l->predeclare(h);
    for (int i=0;i<2;++i) {
        const int start=1+i*11;
        const auto serial=l->issue(request(component,d,start)); auto o=success(d,start,i+1);
        o.receipts.spawn_visited=o.receipts.spawn_shop_legal=true; o.receipts.spawn_round=start+7;
        check(serial && l->resolve(serial,o).full_positive,"fresh attributed own spawn return can qualify its component");
    }
    check(l->begin_organization_campaign(component,organization,23,{premises(h),true,true,true}),
          "component qualification grants one new H trial without granting H success");
    const auto serial=l->issue(request(organization,h,24)); auto o=success(h,24,3);
    o.receipts.spawn_visited=o.receipts.spawn_shop_legal=true; o.receipts.spawn_round=31;
    o.receipts.supported_roles_witnessed=false;
    check(serial && !l->resolve(serial,o).full_positive,
          "H own payout and spawn return cannot qualify unwitnessed helper work or withdrawal");
}
}
int main() {
    card_endpoint_guards();
    check(health_band(75,100) == HealthBand::three_quarters_to_full &&
          health_band(74,100) == HealthBand::half_to_three_quarters &&
          health_band(50,100) == HealthBand::half_to_three_quarters &&
          health_band(49,100) == HealthBand::below_half, "numeric HP bands have exact boundaries");
    auto d = declaration();
    check(valid(d.key), "fully guarded identity is valid");
    auto duplicate = d; duplicate.key.participants[1] = 1; duplicate.key.participant_count = 2;
    check(!valid(duplicate.key), "duplicate participants cannot claim exact membership");
    auto p = premises(d); p.personally_observed &= ~membership_known;
    check(applicability(d.key,d.envelope,p,true) == Applicability::unknown, "unknown required membership never matches");
    p = premises(d); p.key.route_hash++;
    check(applicability(d.key,d.envelope,p,true) == Applicability::changed, "actual route hash guards applicability");
    {
        auto component=d.key,organization=d.key;
        component.return_route_hash=organization.return_route_hash=9876;
        organization.route_hash++;organization.role++;
        organization.participant_count=2;organization.participants[1]=2;
        check(route_component_matches(component,organization),"explicit unchanged return component permits a new assembly prefix, never full qualification");
        auto changed=organization;changed.return_route_hash++;
        check(!route_component_matches(component,changed),"different bank or renewal geometry cannot borrow component proof");
        changed=organization;changed.return_route_hash=0;
        check(!route_component_matches(component,changed),"missing explicit return identity cannot relax full-route equality");
        changed=organization;changed.build.strength++;
        check(!route_component_matches(component,changed),"new strength build still needs its own permission despite unchanged return route");
        auto observed=premises(d);observed.key.return_route_hash=9876;
        check(applicability(d.key,d.envelope,observed,true)==Applicability::changed,"observed return identity changes expire the guarded context");
    }
    p = premises(d); p.state.health = 74;
    check(applicability(d.key,d.envelope,p,true) == Applicability::changed, "second start cannot silently enter lower HP band");
    p = premises(d); p.state.health = 30;
    check(applicability(d.key,d.envelope,p,false) == Applicability::applicable, "represented damage follows within-episode envelope");
    p.state.focus = 11;
    check(applicability(d.key,d.envelope,p,false) == Applicability::changed, "unrepresented stat transition invalidates applicability");
    {
        EvidenceLedger l(1);
        for (int i = 0; i < max_contexts; ++i)
            check(l.predeclare(declaration(10+i)) == i, "fixed predeclared context allocation");
        check(l.predeclare(declaration(99)) == -1 && l.capacity_exclusions() == 1,
              "ninth context excluded without eviction");
        check(l.size() == max_contexts, "all eight original contexts remain");
    }
    {
        EvidenceLedger l(2);
        check(l.predeclare(d) == -1, "actor cannot ingest another actor's private context");
    }
    {
        EvidenceLedger l(1);
        auto narrow = d; narrow.envelope.health = {100,100};
        check(l.predeclare(narrow) == -1, "prediction envelope must cover entire 75-to-100 entry HP band");
        narrow = d; narrow.envelope.strength = {21,21};
        check(l.predeclare(narrow) == -1, "frozen envelope must include initial exact stats");
        narrow = d; narrow.key.hp_band = HealthBand::half_to_three_quarters;
        narrow.envelope.health = {50,74};
        check(l.predeclare(narrow) == -1, "integer-truncated HP envelope omits legal fractional states below 75 percent");
        narrow.envelope.health.upper=std::nextafter(75.,0.);
        check(l.predeclare(narrow) == 0, "half-open HP band covers every finite fractional entry below 75 percent");
    }
    {
        EvidenceLedger l(1); int s = l.predeclare(d);
        auto r = request(s,d,1); r.premises.state.bag = 1;
        check(!l.issue(r), "solo canary enters empty bag");
        check(!l.resolve(1,success(d,1,1)).accepted, "snapshot before issue is not an episode");
        const auto serial = l.issue(request(s,d,1));
        check(serial && !l.issue(request(s,d,2)), "only one globally active episode");
        check(l.predeclare(declaration(8)) == -1, "contexts freeze before first action");
        check(l.resolve(serial,success(d,1,1)).full_positive, "applicable observed full episode counts");
        check(!l.resolve(serial,success(d,1,1)).accepted, "same serial resolves once only");
        check(!l.component_qualified(s,d.key,11), "one positive is not qualification");
        check(!l.issue(request(s,d,11)), "second episode cannot overlap first terminal round");
        check(cycle(l,s,d,12,2).full_positive && l.component_qualified(s,d.key,22),
              "two nonoverlapping positive full episodes qualify component");
        check(!l.organization_qualified(s,d.key,22), "component status never masquerades as organization status");
        check(l.component_qualified(s,d.key,277) && !l.component_qualified(s,d.key,278),
              "qualification expires 256 rounds after latest completion");
        check(!l.issue(request(s,d,280)), "expiry and epoch rollover do not replenish solo campaign");
        check(l.state(s)->outcomes[0].episode.terminal.receipts.positive_reward_lower == 10,
              "outcome ring retains full issued prediction and terminal receipt");
    }
    {
        Qualified q;
        check(!q.ledger.organization_qualified(q.organization,q.supported.key,22), "new membership begins unqualified");
        check(q.ledger.observe_context(q.organization,premises(q.solo),22) == Applicability::changed &&
              !q.ledger.suspended(q.organization,0), "current solo work cannot expire an unstarted H template");
        CampaignAdmission a{premises(q.supported),false,true,true};
        check(!q.ledger.begin_organization_campaign(q.component,q.organization,23,a), "unseen helpers cannot commission");
        a.actual_members_visible = true; a.presently_affordable = false;
        check(!q.ledger.begin_organization_campaign(q.component,q.organization,23,a), "forecast money cannot commission");
        a.presently_affordable = true; a.individually_safe = false;
        check(!q.ledger.begin_organization_campaign(q.component,q.organization,23,a), "aggregate safety cannot commission");
        q.commission();
        a.individually_safe = true;
        check(!q.ledger.begin_organization_campaign(q.component,q.organization,46,a), "same context cannot start another campaign");
    }
    {
        Qualified q; q.commission();
        auto changed = premises(q.supported); changed.key.participants[1] = 3;
        check(q.ledger.observe_context(q.organization,changed,46) == Applicability::changed,
              "fresh material change observed outside episode");
        check(!q.ledger.organization_qualified(q.organization,q.supported.key,47),
              "restoring old membership cannot resurrect expired status");
        check(q.ledger.component_qualified(q.component,q.solo.key,47),
              "H membership failure does not rewrite independent route component");
    }
    {
        Qualified q; q.commission();
        const auto serial = q.ledger.issue(request(q.organization,q.supported,46));
        auto o = success(q.supported,46,5); o.premises.key.build.equipment_hash++;
        auto result = q.ledger.resolve(serial,o);
        check(result.status == Status::context_invalid && !result.eliminated,
              "changed build invalidates context without contradicting model");
        check(!q.ledger.organization_qualified(q.organization,q.supported.key,57),
              "within-episode material change expires prior full status");
    }
    {
        EvidenceLedger l(1); const int s = l.predeclare(d);
        auto serial = l.issue(request(s,d,1));
        auto o = success(d,1,1); o.premises.personally_observed &= ~threat_known;
        o.duration.interval = {100,100};
        const auto result = l.resolve(serial,o);
        check(!result.full_positive && result.eliminated == 0 && !result.model_set_failed,
              "unobserved required premise cannot eliminate predictors or qualify");
        check(l.campaign_exhausted(s) && !l.issue(request(s,d,65)), "unknown solo campaign returns fallback across epochs");
    }
    {
        EvidenceLedger l(1); const int s = l.predeclare(d);
        auto serial = l.issue(request(s,d,1));
        auto o = success(d,1,1); o.duration = {{0,std::numeric_limits<int>::max()},DurationSource::legally_bracketed};
        const auto result = l.resolve(serial,o);
        check(!result.full_positive && !result.prediction_supported && result.eliminated == 0,
              "wide ambiguous overlap retains consistency without confirming component");
    }
    {
        EvidenceLedger l(1); const int s = l.predeclare(d);
        const auto serial = l.issue(request(s,d,1));
        auto o = success(d,1,1); o.receipts.attribution_excludes_other_bag_loss = false;
        check(!l.resolve(serial,o).full_positive, "own bank flag without excluded alternate bag loss cannot prove reward");
    }
    {
        EvidenceLedger l(1); const int s = l.predeclare(d);
        const auto serial = l.issue(request(s,d,1));
        auto o = success(d,1,1); o.receipts.positive_reward_lower = 0;
        check(!l.resolve(serial,o).full_positive, "zero lower bound cannot qualify reward-based evidence");
    }
    {
        EvidenceLedger l(1); const int s = l.predeclare(d);
        const auto serial = l.issue(request(s,d,1));
        auto o = success(d,1,1); o.receipts.production_round = 0;
        check(!l.resolve(serial,o).full_positive, "preissue own receipts cannot be borrowed into a new episode");
    }
    {
        Qualified q; q.commission();
        const auto serial = q.ledger.issue(request(q.organization,q.supported,46));
        auto replayed = success(q.supported,46,3);
        check(!q.ledger.resolve(serial,replayed).full_positive, "nonconsecutive replay of first H payout cannot refresh status");
        check(q.ledger.state(q.organization)->latest_positive == 45, "replayed receipts leave expiry unchanged");
    }
    {
        EvidenceLedger l(1); const int s = l.predeclare(d);
        auto r = request(s,d,1); r.nominal_duration = 5;
        const auto serial = l.issue(r);
        Observation o; o.round = 7; o.kind = ObservationKind::censored; o.premises = premises(d);
        o.duration.source = DurationSource::personally_unfinished_through;
        o.duration.unfinished_through = 6; o.duration.continuous_personal_observation = true;
        const auto result = l.resolve(serial,o);
        check(result.status == Status::censored && result.eliminated == 1 && !result.full_positive,
              "personal unfinished-through censor contributes only its lower bound");
    }
    {
        EvidenceLedger l(1); const int s = l.predeclare(d);
        auto r = request(s,d,1); r.nominal_duration = 5; const auto serial = l.issue(r);
        Observation o; o.round = 7; o.kind = ObservationKind::censored; o.premises = premises(d);
        o.duration.source = DurationSource::personally_unfinished_through;
        o.duration.unfinished_through = 6;
        check(l.resolve(serial,o).eliminated == 0, "lost vision cannot prove continued unfinished action");
    }
    {
        EvidenceLedger l(1); const int s = l.predeclare(d);
        const auto serial = l.issue(request(s,d,1));
        auto o = success(d,1,1,20);
        const auto result = l.resolve(serial,o);
        check(result.model_set_failed && l.suspended(s,0), "empty hypothesis family suspends immediately");
        const auto failures = l.lifetime_failure_count();
        l.invalidate_active(100);
        check(l.suspended(s,100/64) && l.lifetime_failure_count() == failures,
              "death/fuel invalidation and epoch rollover preserve empty-family failures");
    }
    {
        EvidenceLedger l(1); const int s = l.predeclare(d);
        const auto serial = l.issue(request(s,d,1));
        Observation unsafe; unsafe.round = 2; unsafe.kind = ObservationKind::safety_failure;
        check(l.resolve(serial,unsafe).status == Status::safety_failed && l.suspended(s,0),
              "personally observed safety failure suspends despite unknown causal context");
    }
    {
        auto broad = d;
        for (int i = 0; i < 3; ++i) broad.hypotheses[i].offset = {i*2,i*2+2};
        EvidenceLedger l(1); int s = l.predeclare(broad);
        check(cycle(l,s,broad,1,1,13).contradiction, "one applicable nominal contradiction retains matching alternative");
        check(!l.suspended(s,0), "one nonsafety contradiction alone permits bounded second trial");
        check(cycle(l,s,broad,15,2,13).contradiction && l.suspended(s,0),
              "two contradictions among last three eligible episodes retire current latched epoch");
        check(l.lifetime_failure_count() == 2, "contradictions remain failures even if an alternative supports completion");
    }
    {
        Qualified q; q.commission();
        for (int i = 0; i < 3; ++i) {
            const int start = 46+i*2;
            const auto serial = q.ledger.issue(request(q.organization,q.supported,start));
            check(serial != 0, "qualified ordinary production has bounded unresolved attempts");
            check(q.ledger.invalidate_active(start+1).accepted, "fuel/death stale episode resolves unknown once");
        }
        check(q.ledger.state(q.organization)->unresolved_suspended && q.ledger.suspended(q.organization,1),
              "three unresolved attempts suspend across epochs");
        check(q.ledger.state(q.organization)->attempts == 5 && q.ledger.lifetime_failure_count() == 3,
              "failure lifetime survives overwritten outcome ring");
    }
    {
        EvidenceLedger l(1);const auto joint=declaration(7,EpisodeKind::full_organization);
        const int slot=l.predeclare(joint);CampaignAdmission admission{premises(joint),true,true,true};
        auto absent=admission;absent.actual_members_visible=false;
        check(!l.begin_initial_campaign(slot,1,absent),"initial commissioning needs currently visible actual members");
        auto unknown=admission;unknown.premises.personally_observed&=~route_known;
        check(!l.begin_initial_campaign(slot,1,unknown),"initial commissioning cannot borrow an unknown route premise");
        check(l.begin_initial_campaign(slot,1,admission),"explicit initial commissioning needs no fabricated component history");
        check(!l.organization_qualified(slot,joint.key,1),"initial permission is unqualified full organization");
        check(!l.begin_initial_campaign(slot,2,admission),"initial campaign cannot reset its lifetime episode allowance");
        check(cycle(l,slot,joint,2,1).full_positive&&!l.organization_qualified(slot,joint.key,12),"one initial matched cycle remains unqualified");
        check(cycle(l,slot,joint,15,2).full_positive&&l.organization_qualified(slot,joint.key,25),"two actual new organization cycles earn their own status");
    }
    std::uint16_t saturated = std::numeric_limits<std::uint16_t>::max();
    saturating_increment(saturated);
    check(saturated == std::numeric_limits<std::uint16_t>::max(), "16-bit counts saturate rather than wrap");
    std::printf("plan8-evidence: %d checks passed\n", checks);
}
